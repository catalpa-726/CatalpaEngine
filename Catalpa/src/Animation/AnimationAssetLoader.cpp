#include "catpch.h"
#include "Animation/AnimationAssetLoader.h"
#include "Assets/AssetManager.h"
#include "Core/Serialization/AuthorJson.h"
#include "Core/Serialization/BinaryArchive.h"

namespace Catalpa {
    namespace {
        using Json = AuthorJson::Json;
        Json ParseSkin(std::string_view text) {
            // 权重流有独立的大数组上界；避免先构造一份 ArchiveValue 再复制成 JSON DOM。
            if (text.size() > 96 * 1024 * 1024)
                throw std::invalid_argument("Skin author source exceeds 96 MiB");
            size_t nodes = 0;
            std::vector<std::set<std::string>> objects;
            const auto callback = [&](int depth, Json::parse_event_t event, Json& value) {
                if (depth > 12 || ++nodes > 24000000)
                    throw std::invalid_argument("Skin author depth/node budget exceeded");
                if (event == Json::parse_event_t::object_start)
                    objects.emplace_back();
                else if (event == Json::parse_event_t::object_end)
                    objects.pop_back();
                else if (event == Json::parse_event_t::key) {
                    const auto& key = value.get_ref<const std::string&>();
                    if (objects.empty() || !objects.back().insert(key).second)
                        throw std::invalid_argument("Duplicate skin author key: " + key);
                }
                return true;
            };
            return Json::parse(text, callback);
        }
        Math::Quat Quat(const Json& value) {
            if (!value.is_array() || value.size() != 4)
                throw std::invalid_argument("Quaternion requires w/x/y/z");
            return {float(AuthorJson::Number(value[0])), float(AuthorJson::Number(value[1])),
                    float(AuthorJson::Number(value[2])), float(AuthorJson::Number(value[3]))};
        }
        Json Array(const Math::Quat& q) {
            return Json::array({q.w, q.x, q.y, q.z});
        }
        Json Array(const Math::Mat4& matrix) {
            Json values = Json::array();
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    values.push_back(matrix[c][r]);
            return values;
        }
        Math::Mat4 Matrix(const Json& value) {
            if (!value.is_array() || value.size() != 16)
                throw std::invalid_argument("Matrix requires 16 column-major scalars");
            Math::Mat4 matrix{1};
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    matrix[c][r] = float(AuthorJson::Number(value[c * 4 + r]));
            return matrix;
        }
        Math::TransformTRS Transform(const Json& value) {
            AuthorJson::Fields(value, {"translation", "rotation", "scale"});
            return {AuthorJson::Vec3(value.at("translation")), Quat(value.at("rotation")),
                    AuthorJson::Vec3(value.at("scale"))};
        }
        Json Transform(const Math::TransformTRS& value) {
            return {{"translation", AuthorJson::Array(value.translation)},
                    {"rotation", Array(value.rotation)},
                    {"scale", AuthorJson::Array(value.scale)}};
        }
        void Version(const Json& value) {
            if (AuthorJson::Unsigned(value.at("schemaVersion")) != 1)
                throw std::invalid_argument("Unsupported animation schemaVersion");
        }
        AnimationInterpolation Interpolation(const Json& value) {
            const auto text = value.get<std::string>();
            if (text == "step")
                return AnimationInterpolation::Step;
            if (text == "linear")
                return AnimationInterpolation::Linear;
            if (text == "cubic")
                return AnimationInterpolation::CubicSpline;
            throw std::invalid_argument("Unsupported animation interpolation");
        }
        const char* Interpolation(AnimationInterpolation value) {
            switch (value) {
            case AnimationInterpolation::Step:
                return "step";
            case AnimationInterpolation::Linear:
                return "linear";
            case AnimationInterpolation::CubicSpline:
                return "cubic";
            }
            throw std::invalid_argument("Invalid interpolation");
        }
        template <class Key> AnimationChannel<Key> Channel(const Json& value) {
            AuthorJson::Fields(value, {"interpolation", "keys"});
            AnimationChannel<Key> channel;
            channel.interpolation = Interpolation(value.at("interpolation"));
            const auto& keys = value.at("keys");
            if (!keys.is_array() || keys.size() > 1000000)
                throw std::invalid_argument("Invalid animation key array");
            for (const auto& item : keys) {
                AuthorJson::Fields(item, {"time", "value", "in", "out"});
                Key key;
                key.time = AuthorJson::Number(item.at("time"));
                if constexpr (std::is_same_v<Key, RotationKeyframe>) {
                    key.value = Quat(item.at("value"));
                    if (channel.interpolation == AnimationInterpolation::CubicSpline) {
                        key.inTangent = Quat(item.at("in"));
                        key.outTangent = Quat(item.at("out"));
                    }
                } else {
                    key.value = AuthorJson::Vec3(item.at("value"));
                    if (channel.interpolation == AnimationInterpolation::CubicSpline) {
                        key.inTangent = AuthorJson::Vec3(item.at("in"));
                        key.outTangent = AuthorJson::Vec3(item.at("out"));
                    }
                }
                channel.keys.push_back(key);
            }
            return channel;
        }
        template <class Key> Json Channel(const AnimationChannel<Key>& value) {
            Json keys = Json::array();
            for (const auto& key : value.keys) {
                Json item{{"time", key.time}};
                if constexpr (std::is_same_v<Key, RotationKeyframe>)
                    item["value"] = Array(key.value);
                else
                    item["value"] = AuthorJson::Array(key.value);
                if (value.interpolation == AnimationInterpolation::CubicSpline) {
                    if constexpr (std::is_same_v<Key, RotationKeyframe>) {
                        item["in"] = Array(key.inTangent);
                        item["out"] = Array(key.outTangent);
                    } else {
                        item["in"] = AuthorJson::Array(key.inTangent);
                        item["out"] = AuthorJson::Array(key.outTangent);
                    }
                }
                keys.push_back(std::move(item));
            }
            return {{"interpolation", Interpolation(value.interpolation)}, {"keys", std::move(keys)}};
        }
        template <class T>
        bool Fixed(AssetId id, const AssetDecodeContext& context, AssetVersionRef<T>& value,
                   std::string& error) {
            for (const auto& dep : context.dependencies)
                if (dep.id == id && dep.cppType == typeid(T))
                    return AssetVersionRef<T>::TryCreate(
                        id, dep.version, std::static_pointer_cast<const T>(dep.value), value, error);
            error = "Animation dependency is missing or has the wrong domain: " + id.ToString();
            return false;
        }
        bool Bind(SkeletonAsset& value, const AssetDecodeContext& context, std::string& error) {
            if (!context.dependencies.empty()) {
                error = "Skeleton has unexpected dependencies";
                return false;
            }
            return ValidateSkeleton(value, error);
        }
        bool Bind(AnimationClip& value, const AssetDecodeContext& context, std::string& error) {
            if (context.dependencies.size() != 1) {
                error = "Animation clip requires exactly one skeleton dependency";
                return false;
            }
            if (!Fixed(value.skeleton, context, value.skeletonVersion, error))
                return false;
            return ValidateAnimationClip(value, value.skeletonVersion.Get(), error);
        }
        bool Bind(MeshSkinBinding& value, const AssetDecodeContext& context, std::string& error) {
            if (context.dependencies.size() != 2) {
                error = "Skin binding requires exactly one skeleton and one mesh dependency";
                return false;
            }
            if (!Fixed(value.skeleton, context, value.skeletonVersion, error) ||
                !Fixed(value.mesh, context, value.meshVersion, error))
                return false;
            return ValidateMeshSkinBinding(value, error);
        }
        bool Bind(BlendTreeAsset& value, const AssetDecodeContext& context, std::string& error) {
            if (!Fixed(value.skeleton, context, value.skeletonVersion, error))
                return false;
            std::set<AssetId> dependencies{value.skeleton};
            for (auto& node : value.nodes)
                if (node.kind == BlendNodeKind::Clip) {
                    dependencies.insert(node.clip);
                    if (!Fixed(node.clip, context, node.clipVersion, error))
                        return false;
                }
            if (dependencies.size() != context.dependencies.size()) {
                error = "Blend tree has undeclared/extra dependency records";
                return false;
            }
            return ValidateBlendTree(value, error);
        }
        template <class T>
        bool Loader(AssetManager& assets, const char* type, bool (*read)(std::string_view, T&, std::string&),
                    std::string& error) {
            AssetLoader loader;
            if (!AssetType::TryParse(type, loader.type))
                return false;
            loader.cppType = typeid(T);
            loader.decode = [read](const AssetContainer& container, const AssetDecodeContext& context,
                                   AssetLoadResult& output, std::string& error) {
                size_t factor = 12;
                if constexpr (std::is_same_v<T, MeshSkinBinding>) {
                    const std::string_view bytes(reinterpret_cast<const char*>(container.payload.data()),
                                                 container.payload.size());
                    if (bytes.starts_with("CSKN1") || bytes.starts_with("CSKN2"))
                        factor = 3;
                }
                if constexpr (std::is_same_v<T, AnimationClip>) {
                    const std::string_view bytes(reinterpret_cast<const char*>(container.payload.data()),
                                                 container.payload.size());
                    if (bytes.starts_with("CANI1"))
                        factor = 4;
                }
                if (container.payloadVersion != 1 || context.cancellation.IsCancellationRequested() ||
                    container.payload.size() > context.maxResidentBytes / factor) {
                    error = "Animation decode version, cancellation or working budget rejected";
                    return false;
                }
                T value;
                if (!read({reinterpret_cast<const char*>(container.payload.data()), container.payload.size()},
                          value, error) ||
                    !Bind(value, context, error))
                    return false;
                size_t resident = sizeof(T) + container.payload.size() * 8;
                if constexpr (std::is_same_v<T, MeshSkinBinding>)
                    resident = sizeof(T) + value.influences.capacity() * sizeof(SkinInfluence) +
                               value.perJointInverseBind.capacity() * sizeof(Math::Mat4);
                if constexpr (std::is_same_v<T, AnimationClip>) {
                    resident = sizeof(T) + value.tracks.capacity() * sizeof(AnimationTrack) +
                               value.events.capacity() * sizeof(AnimationEvent);
                    for (const auto& track : value.tracks)
                        resident += track.translation.keys.capacity() * sizeof(VectorKeyframe) +
                                    track.scale.keys.capacity() * sizeof(VectorKeyframe) +
                                    track.rotation.keys.capacity() * sizeof(RotationKeyframe);
                    for (const auto& event : value.events)
                        resident += event.name.capacity() + event.payload.capacity();
                }
                output = MakeAssetLoadResult<T>(std::make_shared<const T>(std::move(value)), resident);
                error.clear();
                return true;
            };
            return assets.RegisterLoader(std::move(loader), error);
        }
    } // namespace

    bool ReadSkeleton(std::string_view text, SkeletonAsset& output, std::string& error) try {
        const auto doc = AuthorJson::Parse(text);
        AuthorJson::Fields(doc, {"schemaVersion", "joints"});
        Version(doc);
        const auto& joints = doc.at("joints");
        if (!joints.is_array() || joints.empty() || joints.size() > MaximumSkeletonJoints)
            throw std::invalid_argument("Skeleton joint budget");
        SkeletonAsset value;
        for (const auto& item : joints) {
            AuthorJson::Fields(item, {"id", "name", "parent", "bind", "inverseBind"});
            SkeletonJoint joint;
            joint.id = AuthorJson::Identity<StableId>(item.at("id"));
            joint.name = item.at("name").get<std::string>();
            const auto& parent = item.at("parent");
            if (!parent.is_number_integer())
                throw std::invalid_argument("Joint parent must be an integer");
            const auto index = parent.get<int64_t>();
            if (index < -1 || index >= int64_t(joints.size()))
                throw std::invalid_argument("Joint parent is out of range");
            joint.parent = int32_t(index);
            joint.bindLocal = Transform(item.at("bind"));
            joint.inverseBind = Matrix(item.at("inverseBind"));
            value.joints.push_back(std::move(joint));
        }
        if (!ValidateSkeleton(value, error))
            return false;
        output = std::move(value);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool WriteSkeleton(const SkeletonAsset& value, std::string& output, std::string& error) try {
        if (!ValidateSkeleton(value, error))
            return false;
        Json doc{{"schemaVersion", 1}, {"joints", Json::array()}};
        for (const auto& j : value.joints)
            doc["joints"].push_back({{"id", j.id.ToString()},
                                     {"name", j.name},
                                     {"parent", j.parent},
                                     {"bind", Transform(j.bindLocal)},
                                     {"inverseBind", Array(j.inverseBind)}});
        output = doc.dump(2);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    bool ReadAnimationClip(std::string_view text, AnimationClip& output, std::string& error) try {
        if (text.starts_with("CANI1")) {
            if (text.size() > 96 * 1024 * 1024)
                throw std::invalid_argument("Cooked clip exceeds 96 MiB");
            BinaryReader reader(std::as_bytes(std::span(text.data() + 5, text.size() - 5)));
            AnimationClip value;
            const auto identity = [&](StableId& id) {
                std::span<const std::byte> bytes;
                if (!reader.Bytes(16, bytes))
                    return false;
                std::memcpy(id.bytes.data(), bytes.data(), 16);
                return true;
            };
            const auto number = [&](double& value) {
                uint64_t bits = 0;
                if (!reader.U64(bits))
                    return false;
                std::memcpy(&value, &bits, 8);
                return std::isfinite(value);
            };
            uint32_t tracks = 0;
            if (!identity(value.skeleton.value) || !number(value.duration) ||
                !identity(value.rootMotionJoint) || !reader.U32(tracks) || tracks > MaximumSkeletonJoints)
                throw std::invalid_argument("Invalid cooked clip header");
            size_t total = 0;
            const auto scalar = [&](auto& vector) {
                if constexpr (std::is_same_v<std::decay_t<decltype(vector)>, Math::Quat>)
                    return reader.F32(vector.w) && reader.F32(vector.x) && reader.F32(vector.y) &&
                           reader.F32(vector.z);
                else
                    return reader.F32(vector.x) && reader.F32(vector.y) && reader.F32(vector.z);
            };
            const auto channel = [&](auto& channel) {
                uint32_t mode = 0, count = 0;
                if (!reader.U32(mode) || mode > 2 || !reader.U32(count) || count > 1000000 - total)
                    return false;
                using Key = typename std::decay_t<decltype(channel.keys)>::value_type;
                constexpr size_t components = std::is_same_v<Key, RotationKeyframe> ? 4 : 3;
                const size_t keyBytes =
                    8 + components * 4 * (mode == uint32_t(AnimationInterpolation::CubicSpline) ? 3 : 1);
                if (count > reader.Remaining() / keyBytes)
                    return false;
                total += count;
                channel.interpolation = AnimationInterpolation(mode);
                channel.keys.resize(count);
                for (auto& key : channel.keys) {
                    if (!number(key.time) || !scalar(key.value))
                        return false;
                    if (channel.interpolation == AnimationInterpolation::CubicSpline &&
                        (!scalar(key.inTangent) || !scalar(key.outTangent)))
                        return false;
                }
                return true;
            };
            for (uint32_t i = 0; i < tracks; ++i) {
                AnimationTrack track;
                if (!identity(track.joint) || !channel(track.translation) || !channel(track.rotation) ||
                    !channel(track.scale))
                    throw std::invalid_argument("Truncated cooked clip channels or key budget exceeded");
                value.tracks.push_back(std::move(track));
            }
            uint32_t events = 0;
            if (!reader.U32(events) || events > 4096)
                throw std::invalid_argument("Cooked clip event budget exceeded");
            for (uint32_t i = 0; i < events; ++i) {
                AnimationEvent event;
                if (!number(event.time) || !reader.String(event.name, 128) ||
                    !reader.String(event.payload, 4096))
                    throw std::invalid_argument("Truncated cooked clip event");
                value.events.push_back(std::move(event));
            }
            if (!reader.AtEnd() || !ValidateAnimationClip(value, nullptr, error)) {
                if (error.empty())
                    error = "Trailing cooked clip bytes";
                return false;
            }
            output = std::move(value);
            error.clear();
            return true;
        }
        const auto doc = AuthorJson::Parse(text);
        AuthorJson::Fields(doc,
                           {"schemaVersion", "skeleton", "duration", "tracks", "events", "rootMotionJoint"});
        Version(doc);
        AnimationClip value;
        value.skeleton = AuthorJson::Identity<AssetId>(doc.at("skeleton"));
        value.duration = AuthorJson::Number(doc.at("duration"));
        if (doc.contains("rootMotionJoint") && !doc["rootMotionJoint"].is_null())
            value.rootMotionJoint = AuthorJson::Identity<StableId>(doc["rootMotionJoint"]);
        const auto& tracks = doc.at("tracks");
        if (!tracks.is_array() || tracks.size() > MaximumSkeletonJoints)
            throw std::invalid_argument("Clip track budget");
        for (const auto& item : tracks) {
            AuthorJson::Fields(item, {"joint", "translation", "rotation", "scale"});
            AnimationTrack track;
            track.joint = AuthorJson::Identity<StableId>(item.at("joint"));
            if (item.contains("translation"))
                track.translation = Channel<VectorKeyframe>(item["translation"]);
            if (item.contains("rotation"))
                track.rotation = Channel<RotationKeyframe>(item["rotation"]);
            if (item.contains("scale"))
                track.scale = Channel<VectorKeyframe>(item["scale"]);
            value.tracks.push_back(std::move(track));
        }
        if (doc.contains("events")) {
            if (!doc["events"].is_array() || doc["events"].size() > 4096)
                throw std::invalid_argument("Clip event budget");
            for (const auto& item : doc["events"]) {
                AuthorJson::Fields(item, {"time", "name", "payload"});
                value.events.push_back({AuthorJson::Number(item.at("time")),
                                        item.at("name").get<std::string>(),
                                        item.value("payload", std::string{})});
            }
        }
        if (!ValidateAnimationClip(value, nullptr, error))
            return false;
        output = std::move(value);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool WriteAnimationClip(const AnimationClip& value, std::string& output, std::string& error) try {
        if (!ValidateAnimationClip(value, value.skeletonVersion.Get(), error))
            return false;
        Json doc{{"schemaVersion", 1},
                 {"skeleton", value.skeleton.ToString()},
                 {"duration", value.duration},
                 {"tracks", Json::array()},
                 {"events", Json::array()},
                 {"rootMotionJoint",
                  value.rootMotionJoint.HasValue() ? Json(value.rootMotionJoint.ToString()) : Json(nullptr)}};
        for (const auto& track : value.tracks)
            doc["tracks"].push_back({{"joint", track.joint.ToString()},
                                     {"translation", Channel(track.translation)},
                                     {"rotation", Channel(track.rotation)},
                                     {"scale", Channel(track.scale)}});
        for (const auto& event : value.events)
            doc["events"].push_back({{"time", event.time}, {"name", event.name}, {"payload", event.payload}});
        output = doc.dump(2);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    bool EncodeAnimationClip(const AnimationClip& value, std::string& output, std::string& error) try {
        if (!ValidateAnimationClip(value, value.skeletonVersion.Get(), error))
            return false;
        BinaryWriter writer(96 * 1024 * 1024);
        const auto identity = [&](StableId id) { return writer.Bytes(std::as_bytes(std::span(id.bytes))); };
        const auto number = [&](double value) {
            uint64_t bits;
            std::memcpy(&bits, &value, 8);
            return writer.U64(bits);
        };
        const auto scalar = [&](const auto& vector) {
            if constexpr (std::is_same_v<std::decay_t<decltype(vector)>, Math::Quat>)
                return writer.F32(vector.w) && writer.F32(vector.x) && writer.F32(vector.y) &&
                       writer.F32(vector.z);
            else
                return writer.F32(vector.x) && writer.F32(vector.y) && writer.F32(vector.z);
        };
        const auto channel = [&](const auto& channel) {
            if (!writer.U32(uint32_t(channel.interpolation)) || !writer.U32(uint32_t(channel.keys.size())))
                return false;
            for (const auto& key : channel.keys) {
                if (!number(key.time) || !scalar(key.value))
                    return false;
                if (channel.interpolation == AnimationInterpolation::CubicSpline &&
                    (!scalar(key.inTangent) || !scalar(key.outTangent)))
                    return false;
            }
            return true;
        };
        if (!identity(value.skeleton.value) || !number(value.duration) || !identity(value.rootMotionJoint) ||
            !writer.U32(uint32_t(value.tracks.size()))) {
            error = writer.Error();
            return false;
        }
        for (const auto& track : value.tracks)
            if (!identity(track.joint) || !channel(track.translation) || !channel(track.rotation) ||
                !channel(track.scale)) {
                error = writer.Error();
                return false;
            }
        if (!writer.U32(uint32_t(value.events.size()))) {
            error = writer.Error();
            return false;
        }
        for (const auto& event : value.events)
            if (!number(event.time) || !writer.String(event.name) || !writer.String(event.payload)) {
                error = writer.Error();
                return false;
            }
        std::string bytes = "CANI1";
        bytes.append(reinterpret_cast<const char*>(writer.Data().data()), writer.Data().size());
        output.swap(bytes);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    bool ReadMeshSkinBinding(std::string_view text, MeshSkinBinding& output, std::string& error) try {
        if (text.starts_with("CSKN1") || text.starts_with("CSKN2")) {
            const bool bindShape = text.starts_with("CSKN2");
            size_t cursor = 5;
            const auto take = [&](size_t count) {
                if (cursor > text.size() || count > text.size() - cursor)
                    throw std::invalid_argument("Truncated cooked skin binding");
                const auto* result = text.data() + cursor;
                cursor += count;
                return result;
            };
            const auto integer = [&]() {
                const auto* bytes = take(4);
                uint32_t value = 0;
                for (size_t i = 0; i < 4; ++i)
                    value |= uint32_t(uint8_t(bytes[i])) << (8 * i);
                return value;
            };
            const auto scalar = [&]() {
                const auto bits = integer();
                float value;
                std::memcpy(&value, &bits, 4);
                return value;
            };
            MeshSkinBinding value;
            std::memcpy(value.skeleton.value.bytes.data(), take(16), 16);
            std::memcpy(value.mesh.value.bytes.data(), take(16), 16);
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    value.meshToSkeleton[c][r] = scalar();
            const auto count = integer();
            if (bindShape) {
                const auto binds = integer();
                if (binds > MaximumSkeletonJoints || binds > (text.size() - cursor) / 64)
                    throw std::invalid_argument("Skin inverse-bind matrix budget exceeded");
                value.perJointInverseBind.resize(binds);
                for (auto& matrix : value.perJointInverseBind)
                    for (int c = 0; c < 4; ++c)
                        for (int r = 0; r < 4; ++r)
                            matrix[c][r] = scalar();
            }
            if (count == 0 || count > 2000000 || text.size() - cursor != size_t(count) * 32)
                throw std::invalid_argument("Cooked skin count/byte length mismatch");
            value.influences.resize(count);
            for (auto& influence : value.influences) {
                for (auto& joint : influence.joints)
                    joint = integer();
                for (auto& weight : influence.weights)
                    weight = scalar();
            }
            if (!ValidateMeshSkinBinding(value, error))
                return false;
            output = std::move(value);
            error.clear();
            return true;
        }
        const auto doc = ParseSkin(text);
        AuthorJson::Fields(
            doc, {"schemaVersion", "skeleton", "mesh", "meshToSkeleton", "influences", "inverseBind"});
        Version(doc);
        MeshSkinBinding value;
        value.skeleton = AuthorJson::Identity<AssetId>(doc.at("skeleton"));
        value.mesh = AuthorJson::Identity<AssetId>(doc.at("mesh"));
        value.meshToSkeleton = Matrix(doc.at("meshToSkeleton"));
        if (doc.contains("inverseBind")) {
            const auto& binds = doc.at("inverseBind");
            if (!binds.is_array() || binds.size() > MaximumSkeletonJoints)
                throw std::invalid_argument("Skin inverse-bind budget exceeded");
            for (const auto& matrix : binds)
                value.perJointInverseBind.push_back(Matrix(matrix));
        }
        const auto& weights = doc.at("influences");
        if (!weights.is_array() || weights.size() > 2000000)
            throw std::invalid_argument("Skin influence budget");
        for (const auto& item : weights) {
            if (!item.is_array() || item.size() != 8)
                throw std::invalid_argument("Skin influence requires four indices and four weights");
            SkinInfluence influence;
            for (size_t i = 0; i < 4; ++i) {
                const auto index = AuthorJson::Unsigned(item[i]);
                if (index >= MaximumSkeletonJoints)
                    throw std::invalid_argument("Skin joint index exceeds palette budget");
                influence.joints[i] = uint32_t(index);
                influence.weights[i] = float(AuthorJson::Number(item[i + 4]));
            }
            value.influences.push_back(influence);
        }
        if (!ValidateMeshSkinBinding(value, error))
            return false;
        output = std::move(value);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool WriteMeshSkinBinding(const MeshSkinBinding& value, std::string& output, std::string& error) try {
        if (!ValidateMeshSkinBinding(value, error))
            return false;
        Json doc{{"schemaVersion", 1},
                 {"skeleton", value.skeleton.ToString()},
                 {"mesh", value.mesh.ToString()},
                 {"meshToSkeleton", Array(value.meshToSkeleton)},
                 {"influences", Json::array()},
                 {"inverseBind", Json::array()}};
        for (const auto& matrix : value.perJointInverseBind)
            doc["inverseBind"].push_back(Array(matrix));
        for (const auto& w : value.influences)
            doc["influences"].push_back(
                Json::array({w.joints[0], w.joints[1], w.joints[2], w.joints[3], w.weights[0], w.weights[1],
                             w.weights[2], w.weights[3]}));
        output = doc.dump();
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    bool EncodeMeshSkinBinding(const MeshSkinBinding& value, std::string& output, std::string& error) try {
        if (!ValidateMeshSkinBinding(value, error))
            return false;
        std::string bytes = "CSKN2";
        bytes.reserve(109 + value.influences.size() * 32 + value.perJointInverseBind.size() * 64);
        const auto integer = [&](uint32_t number) {
            for (size_t i = 0; i < 4; ++i)
                bytes.push_back(char((number >> (8 * i)) & 255));
        };
        const auto scalar = [&](float number) {
            uint32_t bits;
            std::memcpy(&bits, &number, 4);
            integer(bits);
        };
        bytes.append(reinterpret_cast<const char*>(value.skeleton.value.bytes.data()), 16);
        bytes.append(reinterpret_cast<const char*>(value.mesh.value.bytes.data()), 16);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                scalar(value.meshToSkeleton[c][r]);
        integer(uint32_t(value.influences.size()));
        integer(uint32_t(value.perJointInverseBind.size()));
        for (const auto& matrix : value.perJointInverseBind)
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    scalar(matrix[c][r]);
        for (const auto& influence : value.influences) {
            for (auto joint : influence.joints)
                integer(joint);
            for (auto weight : influence.weights)
                scalar(weight);
        }
        output.swap(bytes);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    bool ReadBlendTree(std::string_view text, BlendTreeAsset& output, std::string& error) try {
        const auto doc = AuthorJson::Parse(text);
        AuthorJson::Fields(doc, {"schemaVersion", "skeleton", "nodes", "states", "transitions",
                                 "initialState", "parameters", "layout"});
        Version(doc);
        BlendTreeAsset value;
        value.skeleton = AuthorJson::Identity<AssetId>(doc.at("skeleton"));
        value.initialState = AuthorJson::Identity<StableId>(doc.at("initialState"));
        if (!doc.at("parameters").is_object() || doc["parameters"].size() > 128)
            throw std::invalid_argument("Blend parameter budget");
        for (const auto& [key, number] : doc["parameters"].items())
            value.parameters.emplace(key, float(AuthorJson::Number(number)));
        if (!doc.at("nodes").is_array() || doc["nodes"].size() > 256 || !doc.at("states").is_array() ||
            doc["states"].size() > 128 || !doc.at("transitions").is_array() ||
            doc["transitions"].size() > 512)
            throw std::invalid_argument("Blend tree node/state budget");
        for (const auto& item : doc["nodes"]) {
            AuthorJson::Fields(item, {"id", "kind", "clip", "parameter", "children", "thresholds", "speed"});
            BlendTreeNode node;
            node.id = AuthorJson::Identity<StableId>(item.at("id"));
            const auto kind = item.at("kind").get<std::string>();
            if (kind == "clip")
                node.kind = BlendNodeKind::Clip;
            else if (kind == "blend1d")
                node.kind = BlendNodeKind::Blend1D;
            else
                throw std::invalid_argument("Unsupported blend node kind");
            node.speed = item.contains("speed") ? AuthorJson::Number(item["speed"]) : 1;
            if (node.kind == BlendNodeKind::Clip)
                node.clip = AuthorJson::Identity<AssetId>(item.at("clip"));
            else {
                node.parameter = item.at("parameter").get<std::string>();
                for (const auto& child : item.at("children"))
                    node.children.push_back(AuthorJson::Identity<StableId>(child));
                for (const auto& threshold : item.at("thresholds"))
                    node.thresholds.push_back(float(AuthorJson::Number(threshold)));
            }
            value.nodes.push_back(std::move(node));
        }
        for (const auto& item : doc["states"]) {
            AuthorJson::Fields(item, {"id", "node", "name"});
            value.states.push_back({AuthorJson::Identity<StableId>(item.at("id")),
                                    AuthorJson::Identity<StableId>(item.at("node")),
                                    item.at("name").get<std::string>()});
        }
        for (const auto& item : doc["transitions"]) {
            AuthorJson::Fields(item, {"from", "to", "parameter", "threshold", "greaterEqual", "duration"});
            value.transitions.push_back(
                {AuthorJson::Identity<StableId>(item.at("from")),
                 AuthorJson::Identity<StableId>(item.at("to")), item.at("parameter").get<std::string>(),
                 float(AuthorJson::Number(item.at("threshold"))), item.value("greaterEqual", true),
                 AuthorJson::Number(item.at("duration"))});
        }
        if (doc.contains("layout")) {
            if (!doc["layout"].is_object() || doc["layout"].size() > 256)
                throw std::invalid_argument("Blend layout budget exceeded");
            for (const auto& [key, position] : doc["layout"].items()) {
                StableId id;
                if (!StableId::TryParse(key, id) || !position.is_array() || position.size() != 2)
                    throw std::invalid_argument("Invalid blend layout entry");
                value.layout.emplace(id, std::pair{float(AuthorJson::Number(position[0])),
                                                   float(AuthorJson::Number(position[1]))});
            }
        }
        if (!ValidateBlendTree(value, error))
            return false;
        output = std::move(value);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool WriteBlendTree(const BlendTreeAsset& value, std::string& output, std::string& error) try {
        if (!ValidateBlendTree(value, error))
            return false;
        Json doc{{"schemaVersion", 1},
                 {"skeleton", value.skeleton.ToString()},
                 {"initialState", value.initialState.ToString()},
                 {"parameters", value.parameters},
                 {"nodes", Json::array()},
                 {"states", Json::array()},
                 {"transitions", Json::array()}};
        for (const auto& n : value.nodes) {
            Json node{{"id", n.id.ToString()},
                      {"kind", n.kind == BlendNodeKind::Clip ? "clip" : "blend1d"},
                      {"speed", n.speed}};
            if (n.kind == BlendNodeKind::Clip)
                node["clip"] = n.clip.ToString();
            else {
                node["parameter"] = n.parameter;
                node["children"] = Json::array();
                for (auto child : n.children)
                    node["children"].push_back(child.ToString());
                node["thresholds"] = n.thresholds;
            }
            doc["nodes"].push_back(std::move(node));
        }
        for (const auto& s : value.states)
            doc["states"].push_back({{"id", s.id.ToString()}, {"node", s.node.ToString()}, {"name", s.name}});
        for (const auto& t : value.transitions)
            doc["transitions"].push_back({{"from", t.from.ToString()},
                                          {"to", t.to.ToString()},
                                          {"parameter", t.parameter},
                                          {"threshold", t.threshold},
                                          {"greaterEqual", t.greaterEqual},
                                          {"duration", t.duration}});
        doc["layout"] = Json::object();
        for (const auto& [id, position] : value.layout)
            doc["layout"][id.ToString()] = Json::array({position.first, position.second});
        output = doc.dump(2);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    bool RegisterAnimationAssetLoaders(AssetManager& assets, std::string& error) {
        return Loader<SkeletonAsset>(assets, "skeleton", ReadSkeleton, error) &&
               Loader<AnimationClip>(assets, "animation_clip", ReadAnimationClip, error) &&
               Loader<MeshSkinBinding>(assets, "mesh_skin_binding", ReadMeshSkinBinding, error) &&
               Loader<BlendTreeAsset>(assets, "blend_tree", ReadBlendTree, error);
    }
} // namespace Catalpa
