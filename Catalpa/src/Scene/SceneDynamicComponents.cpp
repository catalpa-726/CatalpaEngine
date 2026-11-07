#include "catpch.h"
#include "Scene/SceneDynamicComponents.h"

namespace Catalpa {
    class DynamicComponentAccess final {
      public:
        static WorldStatus Enqueue(Scene& scene, EntityId entity,
                                   std::function<WorldStatus(World&)> operation) {
            if (!operation)
                return {WorldError::InvalidArgument, "Dynamic component operation is empty"};
            if (const auto status = scene.ValidateEntity(entity); !status.IsSuccess())
                return status;
            return scene.m_Commands.Enqueue([entity, operation = std::move(operation)](World& world) {
                if (!world.IsAlive(entity))
                    return WorldStatus{WorldError::InvalidEntity, "Dynamic component entity expired"};
                return operation(world);
            });
        }
    };
    WorldStatus QueueDynamicComponent(Scene& scene, EntityId entity,
                                      std::function<WorldStatus(World&)> operation) {
        try {
            return DynamicComponentAccess::Enqueue(scene, entity, std::move(operation));
        } catch (const std::bad_alloc&) {
            return {WorldError::AllocationFailed, "Cannot capture dynamic component command"};
        }
    }
} // namespace Catalpa
