#include "catpch.h"
#include "Runtime/Systems/AnimationAuthoring.h"
#include "Scene/Components/AnimatorComponent.h"
#include "Scene/Components/SkinnedMeshRendererComponent.h"
#include "Scene/SceneDynamicComponents.h"
#include "ECS/Detail/WorldComponentType.h"

namespace Catalpa {
    const ComponentType& ComponentTraits<AnimatorComponent>::Get() {
        return Detail::MakeComponentType<AnimatorComponent>();
    }
    const ComponentType& ComponentTraits<SkinnedMeshRendererComponent>::Get() {
        return Detail::MakeComponentType<SkinnedMeshRendererComponent>();
    }
    namespace {
        const ArchiveValue& Field(const ArchiveValue& value, const char* name) {
            const auto* item = value.Find(name);
            if (!item)
                throw std::invalid_argument(std::string("Missing animation component field: ") + name);
            return *item;
        }
        template <class T> const T& As(const ArchiveValue& value) {
            const auto* data = std::get_if<T>(&value.data);
            if (!data)
                throw std::invalid_argument("Animation component field has the wrong type");
            return *data;
        }
        double Number(const ArchiveValue& value) {
            double result;
            if (const auto* number = std::get_if<double>(&value.data))
                result = *number;
            else if (const auto* number = std::get_if<int64_t>(&value.data))
                result = double(*number);
            else if (const auto* number = std::get_if<uint64_t>(&value.data))
                result = double(*number);
            else
                throw std::invalid_argument("Expected animation numeric value");
            if (!std::isfinite(result))
                throw std::invalid_argument("Non-finite animation component field");
            return result;
        }
        void Shape(const ArchiveValue& value, std::initializer_list<std::string_view> fields) {
            const auto& object = As<ArchiveValue::Object>(value);
            for (const auto& [key, item] : object)
                if (std::find(fields.begin(), fields.end(), key) == fields.end())
                    throw std::invalid_argument("Unknown animation component field: " + key);
            uint64_t version = 0;
            if (!Field(value, "schemaVersion").ReadUnsigned(version) || version != 1)
                throw std::invalid_argument("Unsupported animation component version");
        }
        AnimatorComponent Animator(const ArchiveValue& value) {
            Shape(value,
                  {"schemaVersion", "animation", "speed", "enabled", "loop", "rootMotion", "parameters"});
            AnimatorComponent result;
            if (!AssetId::TryParse(As<std::string>(Field(value, "animation")), result.animation))
                throw std::invalid_argument("Invalid animator asset identity");
            result.speed = Number(Field(value, "speed"));
            if (result.speed < -8 || result.speed > 8)
                throw std::invalid_argument("Animator speed outside [-8,8]");
            result.enabled = As<bool>(Field(value, "enabled"));
            result.loop = As<bool>(Field(value, "loop"));
            result.rootMotion = As<bool>(Field(value, "rootMotion"));
            const auto& parameters = As<ArchiveValue::Object>(Field(value, "parameters"));
            if (parameters.size() > 128)
                throw std::invalid_argument("Animator parameter budget");
            for (const auto& [key, item] : parameters) {
                const auto number = Number(item);
                if (key.empty() || key.size() > 128 || key.find('\0') != key.npos ||
                    std::abs(number) > std::numeric_limits<float>::max())
                    throw std::invalid_argument("Invalid animator parameter");
                result.parameters.emplace(key, float(number));
            }
            return result;
        }
        SkinnedMeshRendererComponent Skin(const ArchiveValue& value) {
            Shape(value, {"schemaVersion", "binding", "animator", "enabled"});
            SkinnedMeshRendererComponent result;
            if (!AssetId::TryParse(As<std::string>(Field(value, "binding")), result.binding))
                throw std::invalid_argument("Invalid skin binding identity");
            const auto& owner = Field(value, "animator");
            if (!std::holds_alternative<std::nullptr_t>(owner.data) &&
                !PersistentEntityId::TryParse(As<std::string>(owner), result.animator))
                throw std::invalid_argument("Invalid animator entity reference");
            result.enabled = As<bool>(Field(value, "enabled"));
            return result;
        }
    } // namespace
    bool CaptureAnimationComponents(const World& world, EntityId entity,
                                    std::map<std::string, ArchiveValue, std::less<>>& fields,
                                    std::string& error) try {
        auto result = fields;
        if (world.Has<AnimatorComponent>(entity)) {
            AnimatorComponent value;
            const auto status = world.Get(entity, value);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            ArchiveValue::Object parameters;
            for (const auto& [name, number] : value.parameters)
                parameters.emplace(name, ArchiveValue{double(number)});
            result["catalpa.animator"] =
                ArchiveValue{ArchiveValue::Object{{"schemaVersion", ArchiveValue{uint64_t(1)}},
                                                  {"animation", ArchiveValue{value.animation.ToString()}},
                                                  {"speed", ArchiveValue{value.speed}},
                                                  {"enabled", ArchiveValue{value.enabled}},
                                                  {"loop", ArchiveValue{value.loop}},
                                                  {"rootMotion", ArchiveValue{value.rootMotion}},
                                                  {"parameters", ArchiveValue{std::move(parameters)}}}};
        }
        if (world.Has<SkinnedMeshRendererComponent>(entity)) {
            SkinnedMeshRendererComponent value;
            const auto status = world.Get(entity, value);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            result["catalpa.skinned_mesh"] = ArchiveValue{ArchiveValue::Object{
                {"schemaVersion", ArchiveValue{uint64_t(1)}},
                {"binding", ArchiveValue{value.binding.ToString()}},
                {"animator",
                 value.animator.HasValue() ? ArchiveValue{value.animator.ToString()} : ArchiveValue{}},
                {"enabled", ArchiveValue{value.enabled}}}};
        }
        fields.swap(result);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool ValidateAnimationComponent(std::string_view key, const ArchiveValue& fields,
                                    std::string& error) try {
        if (key == "catalpa.animator")
            (void)Animator(fields);
        else if (key == "catalpa.skinned_mesh")
            (void)Skin(fields);
        else {
            error = "Unknown animation component key";
            return false;
        }
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    std::vector<AssetId> AnimationComponentDependencies(std::string_view key, const ArchiveValue& value) {
        if (key == "catalpa.animator")
            return {Animator(value).animation};
        if (key == "catalpa.skinned_mesh")
            return {Skin(value).binding};
        return {};
    }
    bool BindAnimationComponentVersions(World& world, EntityId entity,
                                        std::span<const AssetDependencyValue> dependencies,
                                        std::string& error) try {
        if (world.Has<AnimatorComponent>(entity)) {
            AnimatorComponent value;
            auto status = world.Get(entity, value);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            bool found = false;
            for (const auto& dependency : dependencies)
                if (dependency.id == value.animation) {
                    if (dependency.cppType == typeid(AnimationClip))
                        found = AssetVersionRef<AnimationClip>::TryCreate(
                            dependency.id, dependency.version,
                            std::static_pointer_cast<const AnimationClip>(dependency.value),
                            value.clipVersion, error);
                    else if (dependency.cppType == typeid(BlendTreeAsset))
                        found = AssetVersionRef<BlendTreeAsset>::TryCreate(
                            dependency.id, dependency.version,
                            std::static_pointer_cast<const BlendTreeAsset>(dependency.value),
                            value.treeVersion, error);
                    else if (dependency.cppType == typeid(SkeletonAsset))
                        found = AssetVersionRef<SkeletonAsset>::TryCreate(
                            dependency.id, dependency.version,
                            std::static_pointer_cast<const SkeletonAsset>(dependency.value),
                            value.skeletonVersion, error);
                    break;
                }
            if (!found) {
                error = "Scene animator has no matching fixed animation dependency";
                return false;
            }
            status = world.Set(entity, value);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
        }
        if (world.Has<SkinnedMeshRendererComponent>(entity)) {
            SkinnedMeshRendererComponent value;
            auto status = world.Get(entity, value);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            bool found = false;
            for (const auto& dependency : dependencies)
                if (dependency.id == value.binding && dependency.cppType == typeid(MeshSkinBinding)) {
                    found = AssetVersionRef<MeshSkinBinding>::TryCreate(
                        dependency.id, dependency.version,
                        std::static_pointer_cast<const MeshSkinBinding>(dependency.value),
                        value.bindingVersion, error);
                    break;
                }
            if (!found) {
                error = "Scene skin has no matching fixed skin-binding dependency";
                return false;
            }
            status = world.Set(entity, value);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    bool RestoreAnimationComponents(World& world, EntityId entity,
                                    const std::map<std::string, ArchiveValue, std::less<>>& fields,
                                    std::string& error) try {
        std::optional<AnimatorComponent> animator;
        std::optional<SkinnedMeshRendererComponent> skin;
        if (const auto found = fields.find("catalpa.animator"); found != fields.end())
            animator = Animator(found->second);
        if (const auto found = fields.find("catalpa.skinned_mesh"); found != fields.end())
            skin = Skin(found->second);
        if (animator) {
            const auto status = world.Has<AnimatorComponent>(entity) ? world.Set(entity, *animator)
                                                                     : world.Add(entity, *animator);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
        } else if (world.Has<AnimatorComponent>(entity)) {
            const auto status = world.Remove<AnimatorComponent>(entity);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
        }
        WorldStatus status;
        if (skin)
            status = world.Has<SkinnedMeshRendererComponent>(entity) ? world.Set(entity, *skin)
                                                                     : world.Add(entity, *skin);
        else if (world.Has<SkinnedMeshRendererComponent>(entity))
            status = world.Remove<SkinnedMeshRendererComponent>(entity);
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    bool RestoreAnimationComponents(Scene& scene, EntityId entity,
                                    const std::map<std::string, ArchiveValue, std::less<>>& fields,
                                    std::string& error) try {
        std::optional<AnimatorComponent> animator;
        std::optional<SkinnedMeshRendererComponent> skin;
        if (const auto found = fields.find("catalpa.animator"); found != fields.end())
            animator = Animator(found->second);
        if (const auto found = fields.find("catalpa.skinned_mesh"); found != fields.end())
            skin = Skin(found->second);
        const auto status = QueueDynamicComponent(
            scene, entity, [entity, animator = std::move(animator), skin = std::move(skin)](World& world) {
                if (animator) {
                    const auto status = world.Has<AnimatorComponent>(entity) ? world.Set(entity, *animator)
                                                                             : world.Add(entity, *animator);
                    if (!status.IsSuccess())
                        return status;
                } else if (world.Has<AnimatorComponent>(entity)) {
                    const auto status = world.Remove<AnimatorComponent>(entity);
                    if (!status.IsSuccess())
                        return status;
                }
                if (skin)
                    return world.Has<SkinnedMeshRendererComponent>(entity) ? world.Set(entity, *skin)
                                                                           : world.Add(entity, *skin);
                return world.Has<SkinnedMeshRendererComponent>(entity)
                           ? world.Remove<SkinnedMeshRendererComponent>(entity)
                           : WorldStatus{};
            });
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool CollectAnimationComponentVersions(const World& world, EntityId entity,
                                           std::vector<AssetDependencyValue>& output,
                                           std::string& error) try {
        auto values = output;
        const auto append = [&]<class T>(const AssetVersionRef<T>& fixed) {
            if (!fixed.HasValue())
                return true;
            for (const auto& existing : values)
                if (existing.id == fixed.GetId()) {
                    if (existing.version != fixed.GetVersion() || existing.cppType != typeid(T)) {
                        error = "Scene contains incompatible fixed versions of one animation identity";
                        return false;
                    }
                    return true;
                }
            values.push_back({fixed.GetId(), fixed.GetVersion(), fixed.GetShared(), typeid(T)});
            return true;
        };
        if (world.Has<AnimatorComponent>(entity)) {
            AnimatorComponent component;
            const auto status = world.Get(entity, component);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            if (!append(component.clipVersion) || !append(component.treeVersion) ||
                !append(component.skeletonVersion))
                return false;
        }
        if (world.Has<SkinnedMeshRendererComponent>(entity)) {
            SkinnedMeshRendererComponent component;
            const auto status = world.Get(entity, component);
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            if (!append(component.bindingVersion))
                return false;
        }
        output.swap(values);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
} // namespace Catalpa
