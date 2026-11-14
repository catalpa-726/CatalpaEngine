#include "TestRunner.h"

#include "ECS/World.h"
#include "ECS/EntityCommandBuffer.h"
#include "Scene/Components/IdentityComponent.h"

#include <new>

namespace Catalpa::Tests {

	void RunWorldTests(TestContext& context) {
		World first;
		World second;
		EntityId original;
		context.Check(first.Create(original).IsSuccess(), "Create a live entity");
		context.Check(first.IsAlive(original), "Created entity belongs to its World");
		context.Check(!second.IsAlive(original), "An entity cannot cross Worlds");
		context.Check(second.Destroy(original).code == WorldError::ForeignWorld, "Cross-World destruction is rejected");
		context.Check(first.IsAlive(original), "Rejected destruction leaves the owner unchanged");

		EntityId sentinel{99, 88, 77};
		const EntityId unchanged = sentinel;
		WorldStatus insideCreate, insideDestroy;
		context.Check(first
		                  .ForEach([&](EntityId id) {
			                  insideCreate = first.Create(sentinel);
			                  insideDestroy = first.Destroy(id);
			                  return WorldStatus{};
		                  })
		                  .IsSuccess(),
		    "Entity iteration completes");
		context.Check(insideCreate.code == WorldError::Busy && insideDestroy.code == WorldError::Busy,
		    "Structural writes are rejected during iteration");
		context.Check(
		    sentinel == unchanged && first.IsAlive(original), "Rejected iteration writes preserve output and entity");

		context.Check(first.Destroy(original).IsSuccess(), "Destroy a live entity");
		EntityId replacement;
		context.Check(first.Create(replacement).IsSuccess(), "Create after destruction");
		context.Check(!first.IsAlive(original), "Old generation does not become live after reuse");
		context.Check(
		    first.Destroy(original).code == WorldError::InvalidEntity, "Stale entity destruction is rejected");
		context.Check(first.IsAlive(replacement), "Stale destruction cannot destroy a replacement");

		IdentityComponent authored{PersistentEntityId::FromWords(1, 1), "Original"};
		context.Check(first.Add(replacement, authored).IsSuccess(), "Add a registered data component");
		context.Check(first.Has<IdentityComponent>(replacement), "Added component is present");
		context.Check(
		    first.Add(replacement, authored).code == WorldError::AlreadyExists, "Duplicate component add is rejected");
		IdentityComponent copied;
		context.Check(
		    first.Get(replacement, copied).IsSuccess() && copied.name == "Original", "Read a component by value");
		copied.name = "Edited copy";
		IdentityComponent originalCopy;
		context.Check(first.Get(replacement, originalCopy).IsSuccess() && originalCopy.name == "Original",
		    "Editing a returned copy does not mutate World storage");
		WorldStatus guardedSet, guardedRemove;
		context.Check(first
		                  .Read<IdentityComponent>(replacement,
		                      [&](const IdentityComponent& value) {
			                      guardedSet = first.Set(replacement, copied);
			                      guardedRemove = first.Remove<IdentityComponent>(replacement);
			                      context.Check(value.name == "Original",
			                          "Borrowed component stays valid through rejected writes");
			                      return WorldStatus{};
		                      })
		                  .IsSuccess(),
		    "Read callback completes");
		context.Check(guardedSet.code == WorldError::Busy && guardedRemove.code == WorldError::Busy,
		    "Component writes are blocked while a reference is borrowed");
		context.Check(first.Set(replacement, copied).IsSuccess() && first.Get(replacement, originalCopy).IsSuccess() &&
		        originalCopy.name == "Edited copy",
		    "Set commits a copied component outside read scope");
		EntityCommandBuffer componentCommands;
		context.Check(componentCommands.QueueRemove<IdentityComponent>(replacement).IsSuccess() &&
		        componentCommands.QueueAdd(replacement, authored).IsSuccess(),
		    "Queue ordered component replacement");
		authored.name = "Changed after enqueue";
		context.Check(componentCommands.Apply(first).IsSuccess() && first.Get(replacement, originalCopy).IsSuccess() &&
		        originalCopy.name == "Original",
		    "Component command owns its payload and applies FIFO");
		context.Check(first.Remove<IdentityComponent>(replacement).IsSuccess() &&
		        first.Remove<IdentityComponent>(replacement).code == WorldError::MissingComponent,
		    "Removing a missing component reports failure");
		copied.name = "Output sentinel";
		context.Check(
		    first.Get(replacement, copied).code == WorldError::MissingComponent && copied.name == "Output sentinel",
		    "Missing component read preserves output");

		EntityCommandBuffer commands;
		const auto before = first.GetEntityCount();
		context.Check(commands
		                  .QueueCreate([](World&, EntityId) {
			                  return WorldStatus{WorldError::InvalidArgument, "Deliberate initializer failure"};
		                  })
		                  .IsSuccess(),
		    "Queue failing initialization");
		context.Check(commands.QueueCreate([](World&, EntityId) -> WorldStatus { throw std::bad_alloc{}; }).IsSuccess(),
		    "Queue allocation failure initialization");
		context.Check(commands.QueueCreate().IsSuccess(), "Queue successful creation after failures");
		context.Check(first.GetEntityCount() == before, "Queued creates are not visible before the safe point");
		auto applied = commands.Apply(first);
		context.Check(!applied.IsSuccess() && applied.commands.size() == 3, "Each FIFO command reports its own result");
		if (applied.commands.size() == 3) {
			context.Check(!applied.commands[0].created.HasValue() && !applied.commands[1].created.HasValue(),
			    "Failed creates do not publish handles");
			context.Check(applied.commands[2].status.IsSuccess() && first.IsAlive(applied.commands[2].created),
			    "Later independent command can succeed");
		}
		context.Check(first.GetEntityCount() == before + 1, "Failed initialization rolls back entity storage");
		context.Check(commands.Size() == 0, "Applied commands are consumed exactly once");

		context.Check(commands.QueueDestroy(replacement).IsSuccess(), "Queue a deferred destroy");
		EntityCommandApplyResult blocked;
		context.Check(first
		                  .ForEach([&](EntityId) {
			                  blocked = commands.Apply(first);
			                  return WorldStatus{};
		                  })
		                  .IsSuccess(),
		    "Inspect while a command is pending");
		context.Check(
		    blocked.status.code == WorldError::Busy && commands.Size() == 1, "Busy Apply retains pending commands");
		context.Check(first.IsAlive(replacement), "Deferred destroy waits for a safe point");
		context.Check(commands.Apply(first).IsSuccess() && !first.IsAlive(replacement),
		    "Safe-point destroy invalidates its handle");

		EntityCommandBuffer nested;
		context.Check(nested.QueueCreate().IsSuccess(), "Prepare nested command");
		WorldStatus recursiveStatus;
		context.Check(commands
		                  .Enqueue([&](World& world) {
			                  recursiveStatus = nested.Apply(world).status;
			                  return WorldStatus{};
		                  })
		                  .IsSuccess(),
		    "Prepare reentrant submission attempt");
		context.Check(commands.Apply(first).IsSuccess(), "Outer command finishes");
		context.Check(recursiveStatus.code == WorldError::Busy && nested.Size() == 1,
		    "Reentrant Apply is rejected without consuming nested work");
		context.Check(nested.Apply(first).IsSuccess(), "Previously blocked commands remain applicable");

		EntityId destroyedWorldEntity;
		{
			World temporary;
			context.Check(temporary.Create(destroyedWorldEntity).IsSuccess(), "Create in a temporary World");
		}
		World recreated;
		EntityId recreatedEntity;
		context.Check(recreated.Create(recreatedEntity).IsSuccess(), "Create after another World was destroyed");
		context.Check(recreated.GetIdentity() != destroyedWorldEntity.world && !recreated.IsAlive(destroyedWorldEntity),
		    "World lifetime cannot revive a previous identity");
	}
}
