#include "catpch.h"
#include "Animation/AnimationPlayer.h"
#include "Animation/Skinning.h"
#include "Geometry/MeshValidation.h"
#include <glm/gtc/matrix_inverse.hpp>

namespace Catalpa {
    namespace {
        bool Finite(const Math::Vec3& value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
        bool Finite(const Math::Quat& value) {
            return std::isfinite(value.w) && std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }
        bool Unit(const Math::Quat& value) {
            return Finite(value) &&
                   std::abs(double(glm::dot(value, value)) - 1) <= Math::RotationNormTolerance;
        }
        bool Affine(const Math::Mat4& value) {
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    if (!std::isfinite(value[c][r]))
                        return false;
            return value[0][3] == 0 && value[1][3] == 0 && value[2][3] == 0 && value[3][3] == 1;
        }
        template <class Key>
        bool ChannelValid(const AnimationChannel<Key>& channel, double duration, std::string& error) {
            if (channel.interpolation < AnimationInterpolation::Step ||
                channel.interpolation > AnimationInterpolation::CubicSpline ||
                channel.keys.size() > 1000000) {
                error = "Animation interpolation or key budget is invalid";
                return false;
            }
            double previous = -1;
            for (const auto& key : channel.keys) {
                if (!std::isfinite(key.time) || key.time < 0 || key.time > duration || key.time <= previous ||
                    !Finite(key.value) || !Finite(key.inTangent) || !Finite(key.outTangent)) {
                    error = "Animation keys must be finite, strictly ordered and within clip duration";
                    return false;
                }
                if constexpr (std::is_same_v<Key, RotationKeyframe>)
                    if (!Unit(key.value)) {
                        error = "Animation rotation key is not a unit quaternion";
                        return false;
                    }
                previous = key.time;
            }
            return true;
        }
        template <class Key>
        auto SampleChannel(const AnimationChannel<Key>& channel, double time,
                           const decltype(Key{}.value)& fallback) {
            if (channel.keys.empty())
                return fallback;
            if (time <= channel.keys.front().time)
                return channel.keys.front().value;
            if (time >= channel.keys.back().time)
                return channel.keys.back().value;
            const auto right = std::upper_bound(channel.keys.begin(), channel.keys.end(), time,
                                                [](double t, const auto& key) { return t < key.time; });
            const auto& b = *right;
            const auto& a = *(right - 1);
            if (channel.interpolation == AnimationInterpolation::Step)
                return a.value;
            const float span = float(b.time - a.time), t = float((time - a.time) / (b.time - a.time));
            if (channel.interpolation == AnimationInterpolation::CubicSpline) {
                const float t2 = t * t, t3 = t2 * t;
                auto value = a.value * (2 * t3 - 3 * t2 + 1) + a.outTangent * ((t3 - 2 * t2 + t) * span) +
                             b.value * (-2 * t3 + 3 * t2) + b.inTangent * ((t3 - t2) * span);
                if constexpr (std::is_same_v<Key, RotationKeyframe>)
                    return glm::normalize(value);
                else
                    return value;
            }
            if constexpr (std::is_same_v<Key, RotationKeyframe>)
                return glm::normalize(glm::slerp(a.value, b.value, t));
            else
                return a.value * (1 - t) + b.value * t;
        }
        bool Models(const SkeletonAsset& skeleton, std::span<const Math::TransformTRS> local,
                    std::vector<Math::Mat4>& result, std::string& error) {
            const auto count = skeleton.joints.size();
            if (!count || count > MaximumSkeletonJoints || local.size() != count) {
                error = "Pose and skeleton joint counts differ";
                return false;
            }
            std::vector<Math::Mat4> candidate(count, Math::Mat4{1});
            std::vector<uint8_t> states(count, 0);
            for (size_t i = 0; i < count; ++i) {
                std::vector<size_t> chain;
                int32_t cursor = int32_t(i);
                while (cursor >= 0) {
                    if (size_t(cursor) >= count) {
                        error = "Skeleton parent is out of bounds";
                        return false;
                    }
                    if (states[size_t(cursor)] == 2)
                        break;
                    if (states[size_t(cursor)] == 1) {
                        error = "Skeleton parent cycle";
                        return false;
                    }
                    states[size_t(cursor)] = 1;
                    chain.push_back(size_t(cursor));
                    cursor = skeleton.joints[size_t(cursor)].parent;
                    if (cursor < -1) {
                        error = "Skeleton parent must be -1 or a joint index";
                        return false;
                    }
                }
                for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                    Math::Mat4 matrix;
                    if (const auto status = Math::ComposeTRS(local[*it], matrix); !status.IsSuccess()) {
                        error = status.message;
                        return false;
                    }
                    const auto parent = skeleton.joints[*it].parent;
                    if (parent >= 0) {
                        Math::Mat4 composed;
                        if (const auto status =
                                Math::ComposeWorld(candidate[size_t(parent)], matrix, composed);
                            !status.IsSuccess()) {
                            error = status.message;
                            return false;
                        }
                        matrix = composed;
                    }
                    candidate[*it] = matrix;
                    states[*it] = 2;
                }
            }
            result.swap(candidate);
            error.clear();
            return true;
        }
    } // namespace

    bool BuildModelPose(const SkeletonAsset& skeleton, std::span<const Math::TransformTRS> local,
                        std::vector<Math::Mat4>& output, std::string& error) try {
        return Models(skeleton, local, output, error);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    bool ValidateSkeleton(const SkeletonAsset& asset, std::string& error) try {
        if (asset.joints.empty() || asset.joints.size() > MaximumSkeletonJoints) {
            error = "Skeleton requires 1..256 joints";
            return false;
        }
        std::set<StableId> ids;
        std::vector<Math::TransformTRS> locals;
        for (const auto& joint : asset.joints) {
            if (!joint.id.HasValue() || !ids.insert(joint.id).second || joint.name.size() > 1024 ||
                joint.name.find('\0') != joint.name.npos || !Affine(joint.inverseBind)) {
                error = "Skeleton identity, name or inverse-bind matrix is invalid";
                return false;
            }
            locals.push_back(joint.bindLocal);
        }
        std::vector<Math::Mat4> model;
        if (!Models(asset, locals, model, error))
            return false;
        for (size_t i = 0; i < model.size(); ++i) {
            Math::Mat4 inverse;
            if (const auto status = Math::InverseAffine(model[i], inverse); !status.IsSuccess()) {
                error = status.message;
                return false;
            }
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    if (std::abs(double(inverse[c][r]) - asset.joints[i].inverseBind[c][r]) >
                        5e-5 * (1 + std::abs(double(inverse[c][r])))) {
                        error = "Skeleton inverse-bind matrix disagrees with bind hierarchy at joint " +
                                asset.joints[i].id.ToString();
                        return false;
                    }
        }
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    bool ValidateAnimationClip(const AnimationClip& clip, const SkeletonAsset* skeleton,
                               std::string& error) try {
        if (!clip.skeleton.HasValue() || !std::isfinite(clip.duration) || clip.duration <= 0 ||
            clip.duration > 86400 || clip.tracks.size() > MaximumSkeletonJoints ||
            clip.events.size() > 4096) {
            error = "Animation identity, duration or track/event budget is invalid";
            return false;
        }
        if (skeleton && !ValidateSkeleton(*skeleton, error))
            return false;
        std::set<StableId> known, tracks;
        if (skeleton)
            for (const auto& joint : skeleton->joints)
                known.insert(joint.id);
        size_t keys = 0;
        for (const auto& track : clip.tracks) {
            if (!track.joint.HasValue() || !tracks.insert(track.joint).second ||
                (skeleton && !known.contains(track.joint))) {
                error = "Animation track joint is unknown or duplicated";
                return false;
            }
            if (!ChannelValid(track.translation, clip.duration, error) ||
                !ChannelValid(track.rotation, clip.duration, error) ||
                !ChannelValid(track.scale, clip.duration, error))
                return false;
            keys += track.translation.keys.size() + track.rotation.keys.size() + track.scale.keys.size();
            if (keys > 1000000) {
                error = "Animation key count exceeds one million";
                return false;
            }
        }
        double previous = -1;
        for (const auto& event : clip.events) {
            if (!std::isfinite(event.time) || event.time < 0 || event.time >= clip.duration ||
                event.time < previous || event.name.empty() || event.name.size() > 128 ||
                event.payload.size() > 4096 || event.name.find('\0') != event.name.npos ||
                event.payload.find('\0') != event.payload.npos) {
                error = "Animation event time/order/text is invalid";
                return false;
            }
            previous = event.time;
        }
        if (skeleton && clip.rootMotionJoint.HasValue()) {
            const auto found = std::find_if(skeleton->joints.begin(), skeleton->joints.end(),
                                            [&](const auto& j) { return j.id == clip.rootMotionJoint; });
            if (found == skeleton->joints.end()) {
                error = "Root-motion joint is missing from the selected skeleton";
                return false;
            }
        }
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    namespace {
        std::vector<uint32_t> TrackJoints(const SkeletonAsset& skeleton, const AnimationClip& clip) {
            std::map<StableId, uint32_t> indices;
            for (size_t i = 0; i < skeleton.joints.size(); ++i)
                indices.emplace(skeleton.joints[i].id, uint32_t(i));
            std::vector<uint32_t> result;
            result.reserve(clip.tracks.size());
            for (const auto& track : clip.tracks)
                result.push_back(indices.at(track.joint));
            return result;
        }
        bool SamplePose(const SkeletonAsset& skeleton, const AnimationClip& clip, double seconds,
                        AnimationPose& out, std::string& error, std::span<const uint32_t> trackJoints) try {
            if (!std::isfinite(seconds)) {
                error = "Animation sample time must be finite seconds";
                return false;
            }
            AnimationPose pose;
            for (const auto& joint : skeleton.joints)
                pose.local.push_back(joint.bindLocal);
            if (trackJoints.size() != clip.tracks.size()) {
                error = "Animation evaluator track mapping differs";
                return false;
            }
            const double time = std::clamp(seconds, 0.0, clip.duration);
            for (size_t i = 0; i < clip.tracks.size(); ++i) {
                const auto& track = clip.tracks[i];
                if (trackJoints[i] >= pose.local.size()) {
                    error = "Animation evaluator joint mapping is invalid";
                    return false;
                }
                auto& local = pose.local[trackJoints[i]];
                local.translation = SampleChannel(track.translation, time, local.translation);
                local.rotation = SampleChannel(track.rotation, time, local.rotation);
                local.scale = SampleChannel(track.scale, time, local.scale);
            }
            if (!Models(skeleton, pose.local, pose.model, error))
                return false;
            out = std::move(pose);
            error.clear();
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }

    } // namespace

    bool SampleAnimation(const SkeletonAsset& skeleton, const AnimationClip& clip, double seconds,
                         AnimationPose& output, std::string& error) try {
        if (!ValidateAnimationClip(clip, &skeleton, error))
            return false;
        const auto indices = TrackJoints(skeleton, clip);
        return SamplePose(skeleton, clip, seconds, output, error, indices);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    bool BlendPoses(const SkeletonAsset& skeleton, const AnimationPose& a, const AnimationPose& b,
                    float weight, AnimationPose& out, std::string& error) try {
        if (!std::isfinite(weight) || weight < 0 || weight > 1 || a.local.size() != skeleton.joints.size() ||
            b.local.size() != a.local.size()) {
            error = "Pose blend weight or skeleton mismatch";
            return false;
        }
        AnimationPose pose;
        pose.local.resize(a.local.size());
        for (size_t i = 0; i < a.local.size(); ++i) {
            if (!Math::ValidateTRS(a.local[i]).IsSuccess() || !Math::ValidateTRS(b.local[i]).IsSuccess()) {
                error = "Pose blend source transform is invalid";
                return false;
            }
            pose.local[i].translation = glm::mix(a.local[i].translation, b.local[i].translation, weight);
            pose.local[i].scale = glm::mix(a.local[i].scale, b.local[i].scale, weight);
            pose.local[i].rotation =
                glm::normalize(glm::slerp(a.local[i].rotation, b.local[i].rotation, weight));
        }
        if (!Models(skeleton, pose.local, pose.model, error))
            return false;
        out = std::move(pose);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    bool ValidateMeshSkinBinding(const MeshSkinBinding& binding, std::string& error) try {
        if (!binding.skeleton.HasValue() || !binding.mesh.HasValue() || binding.influences.empty() ||
            binding.influences.size() > 2000000 || !Affine(binding.meshToSkeleton)) {
            error = "Skin binding identity, influences or transform is invalid";
            return false;
        }
        Math::Mat4 inverse;
        if (!Math::InverseAffine(binding.meshToSkeleton, inverse).IsSuccess()) {
            error = "Skin mesh-to-skeleton transform must be invertible";
            return false;
        }
        if (binding.skeletonVersion.HasValue()) {
            if (binding.skeletonVersion.GetId() != binding.skeleton) {
                error = "Skin fixed skeleton identity mismatch";
                return false;
            }
            if (!ValidateSkeleton(*binding.skeletonVersion.Get(), error))
                return false;
        }
        if (binding.meshVersion.HasValue()) {
            if (binding.meshVersion.GetId() != binding.mesh) {
                error = "Skin fixed mesh identity mismatch";
                return false;
            }
            if (!ValidateMeshAsset(*binding.meshVersion.Get(), error))
                return false;
        }
        const auto count = binding.skeletonVersion.HasValue() ? binding.skeletonVersion.Get()->joints.size()
                                                              : MaximumSkeletonJoints;
        if (!binding.perJointInverseBind.empty() &&
            (binding.perJointInverseBind.size() > MaximumSkeletonJoints ||
             (binding.skeletonVersion.HasValue() && binding.perJointInverseBind.size() != count))) {
            error = "Skin per-joint inverse-bind count differs from skeleton";
            return false;
        }
        for (const auto& matrix : binding.perJointInverseBind) {
            Math::Mat4 inverse;
            if (!Affine(matrix) || !Math::InverseAffine(matrix, inverse).IsSuccess()) {
                error = "Source inverse-bind must be a finite invertible affine matrix";
                return false;
            }
        }
        for (const auto& influence : binding.influences) {
            double sum = 0;
            for (size_t i = 0; i < 4; ++i) {
                if (influence.joints[i] >= count || !std::isfinite(influence.weights[i]) ||
                    influence.weights[i] < 0 || influence.weights[i] > 1) {
                    error = "Skin joint index or weight is invalid";
                    return false;
                }
                sum += influence.weights[i];
            }
            if (std::abs(sum - 1) > 1e-4) {
                error = "Skin weights must sum to one; author data is not silently normalized";
                return false;
            }
        }
        if (binding.meshVersion.HasValue() &&
            binding.influences.size() != binding.meshVersion.Get()->mesh->GetData().vertexCount) {
            error = "Skin influence count differs from derived mesh vertices";
            return false;
        }
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    bool BuildSkinningPalette(const MeshSkinBinding& binding, const AnimationPose& pose,
                              std::vector<Math::Mat4>& output, std::string& error) try {
        if (!binding.skeletonVersion.HasValue() || !ValidateMeshSkinBinding(binding, error)) {
            if (error.empty())
                error = "Skin palette requires a fixed skeleton";
            return false;
        }
        const auto& skeleton = *binding.skeletonVersion.Get();
        if (binding.perJointInverseBind.empty())
            return BuildSkinningPalette(skeleton, pose, binding.meshToSkeleton, output, error);
        if (pose.model.size() != skeleton.joints.size()) {
            error = "Skin pose count mismatch";
            return false;
        }
        Math::Mat4 inverse;
        if (!Math::InverseAffine(binding.meshToSkeleton, inverse).IsSuccess()) {
            error = "Skin mesh-to-skeleton transform is singular";
            return false;
        }
        std::vector<Math::Mat4> palette;
        palette.reserve(pose.model.size());
        for (size_t i = 0; i < pose.model.size(); ++i) {
            const auto matrix = inverse * pose.model[i] * binding.perJointInverseBind[i];
            if (!Affine(matrix)) {
                error = "Source bind-shape produced a non-finite skin palette";
                return false;
            }
            palette.push_back(matrix);
        }
        output.swap(palette);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    bool BuildSkinningPalette(const SkeletonAsset& skeleton, const AnimationPose& pose,
                              const Math::Mat4& meshToSkeleton, std::vector<Math::Mat4>& out,
                              std::string& error) try {
        if (pose.model.size() != skeleton.joints.size()) {
            error = "Skinning pose differs from skeleton";
            return false;
        }
        Math::Mat4 inverse;
        if (!Math::InverseAffine(meshToSkeleton, inverse).IsSuccess()) {
            error = "Skin mesh-to-skeleton transform is singular";
            return false;
        }
        std::vector<Math::Mat4> palette;
        palette.reserve(pose.model.size());
        for (size_t i = 0; i < pose.model.size(); ++i) {
            const auto matrix = inverse * pose.model[i] * skeleton.joints[i].inverseBind * meshToSkeleton;
            if (!Affine(matrix)) {
                error = "Non-finite skin palette";
                return false;
            }
            palette.push_back(matrix);
        }
        out.swap(palette);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    bool ValidatedSkinEvaluator::Bind(AssetVersionRef<MeshSkinBinding> binding, std::string& error) try {
        if (!binding.HasValue() || !binding.Get()->skeletonVersion.HasValue() ||
            !binding.Get()->meshVersion.HasValue()) {
            error = "Skin evaluator requires exact binding, skeleton and mesh versions";
            return false;
        }
        if (m_Binding.HasValue() && m_Binding.GetId() == binding.GetId() &&
            m_Binding.GetVersion() == binding.GetVersion()) {
            if (m_Binding.Get() != binding.Get()) {
                error = "Skin evaluator version identity collides with different content";
                return false;
            }
            error.clear();
            return true;
        }
        if (!ValidateMeshSkinBinding(*binding.Get(), error))
            return false;
        Math::Mat4 inverse;
        if (!Math::InverseAffine(binding.Get()->meshToSkeleton, inverse).IsSuccess()) {
            error = "Skin evaluator mesh-to-skeleton transform is singular";
            return false;
        }
        const auto& skeleton = *binding.Get()->skeletonVersion.Get();
        std::vector<Math::Mat4> binds;
        binds.reserve(skeleton.joints.size());
        for (size_t i = 0; i < skeleton.joints.size(); ++i) {
            const auto matrix = binding.Get()->perJointInverseBind.empty()
                                    ? skeleton.joints[i].inverseBind * binding.Get()->meshToSkeleton
                                    : binding.Get()->perJointInverseBind[i];
            if (!Affine(matrix)) {
                error = "Skin bind-space matrix is non-finite";
                return false;
            }
            binds.push_back(matrix);
        }
        m_Binding = std::move(binding);
        m_MeshFromSkeleton = inverse;
        m_BindFromMesh = std::move(binds);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    bool ValidatedSkinEvaluator::BuildPalette(const AnimationPose& pose, std::vector<Math::Mat4>& output,
                                              std::string& error) const try {
        if (!m_Binding.HasValue() || pose.model.size() != m_BindFromMesh.size()) {
            error = "Skin evaluator is unbound or pose joint count differs";
            return false;
        }
        std::vector<Math::Mat4> palette;
        palette.reserve(pose.model.size());
        for (size_t i = 0; i < pose.model.size(); ++i) {
            if (!Affine(pose.model[i])) {
                error = "Skin pose model matrix is non-finite or non-affine";
                return false;
            }
            const auto matrix = m_MeshFromSkeleton * pose.model[i] * m_BindFromMesh[i];
            if (!Affine(matrix)) {
                error = "Skin palette exceeds finite affine range";
                return false;
            }
            palette.push_back(matrix);
        }
        output.swap(palette);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    bool SkinMeshReference(const MeshData& source, std::span<const SkinInfluence> influences,
                           std::span<const Math::Mat4> palette, MeshData& out, std::string& error) try {
        if (!ValidateMesh(source).IsSuccess() || influences.size() != source.vertexCount || palette.empty() ||
            palette.size() > MaximumSkeletonJoints) {
            error = "Invalid skin source geometry or palette";
            return false;
        }
        for (const auto& matrix : palette)
            if (!Affine(matrix)) {
                error = "Skin palette must contain finite affine matrices";
                return false;
            }
        const VertexAttribute *position = nullptr, *normal = nullptr, *tangent = nullptr;
        for (const auto& a : source.attributes) {
            if (a.semantic == VertexSemantic::Position)
                position = &a;
            if (a.semantic == VertexSemantic::Normal)
                normal = &a;
            if (a.semantic == VertexSemantic::Tangent)
                tangent = &a;
        }
        if (!position || position->format != VertexFormat::Float3 ||
            (normal && normal->format != VertexFormat::Float3) ||
            (tangent && tangent->format != VertexFormat::Float4)) {
            error = "Skinning requires Float3 position/normal and optional Float4 tangent";
            return false;
        }
        MeshData result = source;
        std::vector<Math::Vec3> points;
        points.reserve(source.vertexCount);
        for (size_t vertex = 0; vertex < source.vertexCount; ++vertex) {
            const auto& influence = influences[vertex];
            Math::Mat4 matrix{0};
            double sum = 0;
            for (size_t k = 0; k < 4; ++k) {
                if (influence.joints[k] >= palette.size() || !std::isfinite(influence.weights[k]) ||
                    influence.weights[k] < 0 || influence.weights[k] > 1) {
                    error = "Skin influence is out of range";
                    return false;
                }
                sum += influence.weights[k];
            }
            if (std::abs(sum - 1) > 1e-4) {
                error = "Skin weights are not normalized";
                return false;
            }
            // 只校正已通过单位和容差检查的求值舍入；资产权重原值不变。
            for (size_t k = 0; k < 4; ++k)
                matrix += palette[influence.joints[k]] * float(influence.weights[k] / sum);
            // 归一化后的线性组合底行有舍入差，数学只消费前三行，显式重建 affine 底行。
            matrix[0][3] = matrix[1][3] = matrix[2][3] = 0;
            matrix[3][3] = 1;
            const auto* src = source.vertexData.data() + vertex * source.vertexStride;
            auto* dst = result.vertexData.data() + vertex * source.vertexStride;
            Math::Vec3 p, n{0}, t{0};
            float hand = 1;
            std::memcpy(&p, src + position->offset, 12);
            p = Math::Vec3(matrix * Math::Vec4(p, 1));
            if (!Finite(p)) {
                error = "Skinned position is non-finite";
                return false;
            }
            std::memcpy(dst + position->offset, &p, 12);
            points.push_back(p);
            const Math::Mat3 linear{matrix};
            double magnitude = 0;
            for (int c = 0; c < 3; ++c)
                for (int r = 0; r < 3; ++r)
                    magnitude = std::max(magnitude, std::abs(double(linear[c][r])));
            const glm::dmat3 normalized = magnitude > 0 ? glm::dmat3(linear) / magnitude : glm::dmat3(0);
            const double determinant = glm::determinant(normalized);
            if (normal) {
                if (!std::isfinite(determinant) || magnitude == 0 || std::abs(determinant) <= 1e-12) {
                    error = "Blended skin linear transform is singular";
                    return false;
                }
                std::memcpy(&n, src + normal->offset, 12);
                const auto direction = glm::transpose(glm::inverse(normalized)) * glm::dvec3(n);
                const double length = std::hypot(direction.x, direction.y, direction.z);
                if (!std::isfinite(length) || length == 0) {
                    error = "Skinned normal is invalid";
                    return false;
                }
                n = Math::Vec3(direction / length);
                std::memcpy(dst + normal->offset, &n, 12);
            }
            if (tangent) {
                Math::Vec4 v;
                std::memcpy(&v, src + tangent->offset, 16);
                auto direction = normalized * glm::dvec3(v);
                if (normal)
                    direction -= glm::dvec3(n) * glm::dot(glm::dvec3(n), direction);
                const double length = std::hypot(direction.x, direction.y, direction.z);
                if (!std::isfinite(length) || length == 0) {
                    error = "Skinned tangent is invalid";
                    return false;
                }
                t = Math::Vec3(direction / length);
                hand = v.w * (determinant < 0 ? -1.0f : 1.0f);
                v = Math::Vec4(t, hand);
                std::memcpy(dst + tangent->offset, &v, 16);
            }
        }
        if (!ComputeBounds(points, result.localBounds).IsSuccess() || !ValidateMesh(result).IsSuccess()) {
            error = "Skinned geometry validation failed";
            return false;
        }
        out = std::move(result);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    namespace {
        bool AdvanceClip(const SkeletonAsset& skeleton, const AnimationClip& clip, double dt, double speed,
                         bool loop, bool extract, AnimatorState& state, AnimationAdvance& out,
                         std::string& error, std::span<const uint32_t> trackJoints) try {
            if (!std::isfinite(dt) || dt < 0 || dt > 1 || !std::isfinite(speed) || speed < -64 ||
                speed > 64) {
                if (error.empty())
                    error = "Animation dt/speed exceeds the fixed-step budget";
                return false;
            }
            AnimatorState next = state;
            if (!next.initialized) {
                next.time = 0;
                next.loopCount = 0;
                next.initialized = true;
            }
            if (!std::isfinite(next.time) || next.time < 0 || next.time > clip.duration) {
                error = "Animator time is invalid";
                return false;
            }
            double remaining = dt * speed;
            if (std::abs(remaining) / clip.duration > 16) {
                error = "Animation step crosses more than 16 loops";
                return false;
            }
            AnimationAdvance result;
            const bool motionEnabled = extract && clip.rootMotionJoint.HasValue();
            size_t motionRoot = 0;
            Math::TransformTRS bindRoot;
            if (motionEnabled) {
                motionRoot =
                    size_t(std::find_if(skeleton.joints.begin(), skeleton.joints.end(),
                                        [&](const auto& joint) { return joint.id == clip.rootMotionJoint; }) -
                           skeleton.joints.begin());
                std::vector<Math::TransformTRS> locals;
                for (const auto& joint : skeleton.joints)
                    locals.push_back(joint.bindLocal);
                std::vector<Math::Mat4> model;
                if (!Models(skeleton, locals, model, error))
                    return false;
                const auto status = Math::DecomposeTRS(model[motionRoot], bindRoot);
                if (!status.IsSuccess()) {
                    error = "Root-motion bind model transform is not a nondegenerate TRS";
                    return false;
                }
            }
            const auto motion = [&](const AnimationPose& pose, Math::TransformTRS& value) {
                Math::TransformTRS root;
                const auto status = Math::DecomposeTRS(pose.model[motionRoot], root);
                if (!status.IsSuccess()) {
                    error = "Root-motion model transform contains shear or a degenerate scale";
                    return false;
                }
                value.rotation = glm::normalize(root.rotation * glm::inverse(bindRoot.rotation));
                value.translation = root.translation - value.rotation * bindRoot.translation;
                value.scale = Math::Vec3(1);
                return true;
            };
            size_t segments = 0;
            auto collect = [&](double a, double b, bool includeStart) {
                if (b >= a)
                    for (const auto& event : clip.events) {
                        if ((event.time > a || (includeStart && event.time == a)) && event.time <= b)
                            result.events.push_back(event);
                    }
                else
                    for (auto it = clip.events.rbegin(); it != clip.events.rend(); ++it)
                        if (it->time >= b && it->time < a)
                            result.events.push_back(*it);
            };
            bool includeStart = !state.initialized && remaining > 0;
            while (std::abs(remaining) > 1e-12) {
                if (++segments > 18) {
                    error = "Animation traversal budget exceeded";
                    return false;
                }
                const double a = next.time;
                const double step =
                    remaining > 0 ? std::min(remaining, clip.duration - a) : std::max(remaining, -a);
                const double b = a + step;
                if (step != 0)
                    collect(a, b, includeStart);
                includeStart = false;
                if (result.events.size() > 256) {
                    error = "Animation event budget exceeded";
                    return false;
                }
                if (motionEnabled && step != 0) {
                    AnimationPose before, after;
                    if (!SamplePose(skeleton, clip, a, before, error, trackJoints) ||
                        !SamplePose(skeleton, clip, b, after, error, trackJoints))
                        return false;
                    Math::TransformTRS left, right;
                    if (!motion(before, left) || !motion(after, right))
                        return false;
                    const auto rotation = glm::inverse(left.rotation) * right.rotation;
                    const auto delta = glm::inverse(left.rotation) * (right.translation - left.translation);
                    result.rootTranslation += result.rootRotation * delta;
                    result.rootRotation = glm::normalize(result.rootRotation * rotation);
                }
                next.time = b;
                remaining -= step;
                if (std::abs(remaining) <= 1e-12)
                    break;
                if (!loop) {
                    remaining = 0;
                    break;
                }
                if (next.loopCount == std::numeric_limits<uint64_t>::max()) {
                    error = "Animator loop counter exhausted";
                    return false;
                }
                ++next.loopCount;
                if (remaining > 0) {
                    next.time = 0;
                    includeStart = true;
                } else
                    next.time = clip.duration;
            }
            if (!SamplePose(skeleton, clip, next.time, result.pose, error, trackJoints))
                return false;
            if (motionEnabled) {
                Math::TransformTRS compensation;
                if (!motion(result.pose, compensation))
                    return false;
                const auto inverse = glm::inverse(compensation.rotation);
                // 在 Skeleton 顶层施加刚性逆运动，保留每个关节 local/model 的层级一致性。
                // 因而 FBX/glTF 的独立单位转换根不会阻止真实根骨骼的 motion 提取。
                for (size_t i = 0; i < skeleton.joints.size(); ++i)
                    if (skeleton.joints[i].parent == -1) {
                        auto& root = result.pose.local[i];
                        root.translation = inverse * (root.translation - compensation.translation);
                        root.rotation = glm::normalize(inverse * root.rotation);
                    }
                if (!Models(skeleton, result.pose.local, result.pose.model, error))
                    return false;
            }
            state = next;
            out = std::move(result);
            error.clear();
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    } // namespace

    bool AdvanceAnimation(const SkeletonAsset& skeleton, const AnimationClip& clip, double dt, double speed,
                          bool loop, bool extract, AnimatorState& state, AnimationAdvance& output,
                          std::string& error) try {
        if (!ValidateAnimationClip(clip, &skeleton, error))
            return false;
        const auto indices = TrackJoints(skeleton, clip);
        return AdvanceClip(skeleton, clip, dt, speed, loop, extract, state, output, error, indices);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    bool ValidatedClipEvaluator::Bind(AssetVersionRef<AnimationClip> clip, std::string& error) try {
        if (!clip.HasValue() || !clip.Get()->skeletonVersion.HasValue() ||
            clip.Get()->skeletonVersion.GetId() != clip.Get()->skeleton) {
            error = "Animation evaluator requires exact clip and skeleton versions";
            return false;
        }
        if (m_Clip.HasValue() && m_Clip.GetId() == clip.GetId() && m_Clip.GetVersion() == clip.GetVersion()) {
            if (m_Clip.Get() != clip.Get()) {
                error = "Animation evaluator version identity collides with different content";
                return false;
            }
            error.clear();
            return true;
        }
        if (!ValidateAnimationClip(*clip.Get(), clip.Get()->skeletonVersion.Get(), error))
            return false;
        auto indices = TrackJoints(*clip.Get()->skeletonVersion.Get(), *clip.Get());
        m_Clip = std::move(clip);
        m_TrackJoints = std::move(indices);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    bool ValidatedClipEvaluator::Sample(double seconds, AnimationPose& output, std::string& error) const {
        if (!m_Clip.HasValue()) {
            error = "Animation evaluator is not bound";
            return false;
        }
        return SamplePose(*m_Clip.Get()->skeletonVersion.Get(), *m_Clip.Get(), seconds, output, error,
                          m_TrackJoints);
    }
    bool ValidatedClipEvaluator::Advance(double dt, double speed, bool loop, bool extract,
                                         AnimatorState& state, AnimationAdvance& output,
                                         std::string& error) const {
        if (!m_Clip.HasValue()) {
            error = "Animation evaluator is not bound";
            return false;
        }
        return AdvanceClip(*m_Clip.Get()->skeletonVersion.Get(), *m_Clip.Get(), dt, speed, loop, extract,
                           state, output, error, m_TrackJoints);
    }
} // namespace Catalpa
