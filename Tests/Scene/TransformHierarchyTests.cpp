#include "TestRunner.h"

#include "Core/Math/TransformMath.h"
#include "Core/Math/CoordinateConvention.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"
#include <cmath>
#include <limits>
#include <numbers>

namespace Catalpa::Tests {
    namespace {
        // 相对/绝对混合容差只用于验收比较，不改写作者输入或运算结果。
        bool Near(float a, float b, float tolerance = 1.0e-4f)
        {
            return std::isfinite(a) && std::isfinite(b)
                && std::abs(a - b) <= tolerance * (1.0f + std::abs(b));
        }

        bool NearMatrix(const Math::Mat4& a, const Math::Mat4& b)
        {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!Near(a[column][row], b[column][row])) return false;
            return true;
        }

        // 失败输出要求完全不变，不能用数值容差掩盖部分写入。
        bool SameMatrix(const Math::Mat4& a, const Math::Mat4& b)
        {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (a[column][row] != b[column][row]) return false;
            return true;
        }

        Math::Quat YRotation(float radians)
        {
            return { std::cos(radians / 2.0f), 0.0f, std::sin(radians / 2.0f), 0.0f };
        }

        PersistentEntityId Id(uint64_t low)
        {
            return PersistentEntityId::FromWords(0x5330333154455354ULL, low);
        }

        // 覆盖列向量、单位、镜像、shear 和数值失败的输出保留契约。
        void CheckMath(TestContext& context)
        {
            context.Check(Math::UsesRightHandedCoordinates && Math::UsesColumnVectors && Math::UsesColumnMajorStorage
                && Math::WorldUnitsPerMeter == 1.0f && Math::Right.x == 1.0f && Math::Up.y == 1.0f && Math::Forward.z == -1.0f,
                "Coordinate constants agree with the declared right-handed meter convention");
            constexpr auto stable = StableId::FromWords(0x0102030405060708ULL, 0x1112131415161718ULL);
            context.Check(!StableId{}.HasValue() && stable.HasValue() && stable.bytes[0] == 0x01 && stable.bytes[7] == 0x08
                && stable.bytes[8] == 0x11 && stable.bytes[15] == 0x18, "Stable ID preserves explicit high-first author words without native-memory serialization");
            Math::TransformTRS parent;
            parent.translation = { 1.0f, 2.0f, 3.0f };
            parent.rotation = YRotation(std::numbers::pi_v<float> / 2.0f);
            parent.scale = { 2.0f, 3.0f, 4.0f };
            Math::TransformTRS child;
            child.translation = { 1.0f, 0.0f, 0.0f };
            Math::Mat4 parentMatrix{ 1.0f }, childMatrix{ 1.0f }, world{ 1.0f };
            context.Check(Math::ComposeTRS(parent, parentMatrix).IsSuccess(), "Compose a translated, rotated, nonuniform parent");
            context.Check(Math::ComposeTRS(child, childMatrix).IsSuccess(), "Compose child local translation");
            context.Check(Math::ComposeWorld(parentMatrix, childMatrix, world).IsSuccess(), "Compose parent * local");
            context.Check(Near(world[3].x, 1.0f) && Near(world[3].y, 2.0f) && Near(world[3].z, 1.0f), "Column-vector T*R*S maps child origin to (1,2,1)");
            context.Check(Near(parentMatrix[0].x, 0.0f) && Near(parentMatrix[0].z, -2.0f), "Positive Y rotation sends +X toward -Z in a right-handed frame");

            child.rotation = YRotation(std::numbers::pi_v<float> / 4.0f);
            context.Check(Math::ComposeTRS(child, childMatrix).IsSuccess() && Math::ComposeWorld(parentMatrix, childMatrix, world).IsSuccess(), "World composition retains affine shear");
            Math::TransformTRS output;
            output.translation = { 91.0f, 92.0f, 93.0f };
            context.Check(Math::DecomposeTRS(world, output).code == Math::MathError::NotTRS, "Sheared world matrix is not silently represented as local TRS");
            context.Check(output.translation.x == 91.0f && output.translation.y == 92.0f && output.translation.z == 93.0f, "Failed decomposition preserves output");

            parent.scale = { 2.0f, -3.0f, 4.0f };
            Math::Mat4 recomposed{ 1.0f };
            context.Check(Math::ComposeTRS(parent, parentMatrix).IsSuccess()
                && Math::DecomposeTRS(parentMatrix, output).IsSuccess()
                && Math::ComposeTRS(output, recomposed).IsSuccess()
                && NearMatrix(parentMatrix, recomposed), "Reflected TRS round-trips without changing its matrix");

            Math::TransformTRS nearUnit;
            nearUnit.rotation = YRotation(std::numbers::pi_v<float> / 2.0f);
            nearUnit.rotation.w *= 1.0f + 2.0e-6f;
            nearUnit.rotation.y *= 1.0f + 2.0e-6f;
            const Math::Quat authorRotation = nearUnit.rotation;
            Math::Mat4 nearUnitMatrix{ 7.0f };
            context.Check(Math::ComposeTRS(nearUnit, nearUnitMatrix).IsSuccess()
                && Near(nearUnitMatrix[0].x, 0.0f, 1.0e-6f) && Near(nearUnitMatrix[0].z, -1.0f, 1.0e-6f), "Near-unit quaternion calculation removes only in-tolerance norm error");
            context.Check(nearUnit.rotation.w == authorRotation.w && nearUnit.rotation.x == authorRotation.x
                && nearUnit.rotation.y == authorRotation.y && nearUnit.rotation.z == authorRotation.z, "Quaternion calculation never overwrites the author's source value");

            const Math::Mat4 sentinel(7.0f);
            Math::Mat4 untouched = sentinel;
            Math::TransformTRS invalid;
            invalid.rotation = { 0.0f, 0.0f, 0.0f, 0.0f };
            context.Check(Math::ComposeTRS(invalid, untouched).code == Math::MathError::InvalidRotation && SameMatrix(untouched, sentinel), "Zero quaternion is rejected without replacing output");
            invalid.rotation = { 2.0f, 0.0f, 0.0f, 0.0f };
            context.Check(!Math::ComposeTRS(invalid, untouched).IsSuccess() && SameMatrix(untouched, sentinel), "Author nonunit quaternion is not silently repaired");
            invalid = {};
            invalid.translation.x = std::numeric_limits<float>::quiet_NaN();
            context.Check(Math::ComposeTRS(invalid, untouched).code == Math::MathError::NonFinite && SameMatrix(untouched, sentinel), "Nonfinite translation is rejected");
            invalid = {};
            invalid.scale.y = std::numeric_limits<float>::infinity();
            context.Check(!Math::ComposeTRS(invalid, untouched).IsSuccess() && SameMatrix(untouched, sentinel), "Nonfinite scale is rejected");

            Math::TransformTRS singular;
            singular.scale.x = 0.0f;
            Math::Mat4 singularMatrix{ 1.0f };
            context.Check(Math::ComposeTRS(singular, singularMatrix).IsSuccess(), "Zero scale remains valid author data");
            context.Check(!Math::InverseAffine(singularMatrix, untouched).IsSuccess() && SameMatrix(untouched, sentinel), "Zero-scale inverse fails without changing output");
            context.Check(!Math::DecomposeTRS(singularMatrix, output).IsSuccess(), "Degenerate TRS decomposition is explicitly rejected");

            Math::TransformTRS small;
            small.scale = { 1.0e-20f, 1.0e-20f, 1.0e-20f };
            Math::Mat4 smallMatrix{ 1.0f }, inverse{ 1.0f }, product{ 1.0f };
            context.Check(Math::ComposeTRS(small, smallMatrix).IsSuccess() && Math::InverseAffine(smallMatrix, inverse).IsSuccess(), "Uniform small scale remains invertible despite tiny determinant");
            context.Check(Math::ComposeWorld(smallMatrix, inverse, product).IsSuccess() && NearMatrix(product, Math::Mat4(1.0f)), "Small-scale inverse has an identity product");
            context.Check(Math::ComposeWorld(inverse, smallMatrix, product).IsSuccess() && NearMatrix(product, Math::Mat4(1.0f)), "Small-scale inverse has a reverse identity product");
            small.scale = { 1.0f, 1.0e-9f, 1.0f };
            context.Check(Math::ComposeTRS(small, smallMatrix).IsSuccess() && !Math::InverseAffine(smallMatrix, untouched).IsSuccess()
                && SameMatrix(untouched, sentinel), "Ill-conditioned inverse is rejected without changing output");
            small.scale = { 1.0e-30f, 1.0e-30f, 1.0e-30f };
            context.Check(Math::ComposeTRS(small, smallMatrix).IsSuccess()
                && Math::ComposeWorld(smallMatrix, smallMatrix, untouched).code == Math::MathError::OutOfRange
                && SameMatrix(untouched, sentinel), "Composition rejects underflow that would erase nonzero basis columns");
            Math::Mat4 huge{ 1.0f };
            huge[0][0] = std::numeric_limits<float>::max();
            context.Check(Math::ComposeWorld(huge, huge, untouched).code == Math::MathError::OutOfRange
                && SameMatrix(untouched, sentinel), "Composition rejects float overflow without publishing a partial result");
            Math::Mat4 projective(1.0f);
            projective[0][3] = 0.1f;
            context.Check(Math::ComposeWorld(projective, Math::Mat4(1.0f), untouched).code == Math::MathError::NonAffine && SameMatrix(untouched, sentinel), "Projective matrix is not accepted as an affine transform");
        }

        // 同步安全点、父链、持久身份和SceneHandle都通过公开接口验收。
        void CheckScenes(TestContext& context)
        {
            SceneManager manager;
            SceneHandle handle;
            if (!manager.CreateScene("Hierarchy contracts", handle).IsSuccess()) {
                context.Check(false, "Create Scene");
                return;
            }
            context.Check(manager.SetActive(handle).IsSuccess(), "Select active Scene");
            Scene& scene = *manager.GetScene(handle);
            Math::TransformTRS parent;
            parent.translation = { 1.0f, 2.0f, 3.0f };
            parent.rotation = YRotation(std::numbers::pi_v<float> / 2.0f);
            parent.scale = { 2.0f, 3.0f, 4.0f };
            Math::TransformTRS child;
            child.translation = { 1.0f, 0.0f, 0.0f };
            context.Check(scene.QueueCreate(Id(1), "Parent", parent).IsSuccess() && scene.QueueCreate(Id(2), "Child", child).IsSuccess(), "Queue two scene entities");
            context.Check(!scene.Resolve(Id(1)).HasValue() && scene.GetWorld().GetEntityCount() == 0, "Creation does not publish before CommitPending");
            context.Check(manager.UpdateActive().IsSuccess(), "Commit creation and update transforms");
            const EntityId parentId = scene.Resolve(Id(1));
            const EntityId childId = scene.Resolve(Id(2));
            if (!parentId.HasValue() || !childId.HasValue()) {
                context.Check(false, "Resolve committed persistent identities");
                return;
            }
            context.Check(scene.QueueSetParent(childId, parentId).IsSuccess() && manager.UpdateActive().IsSuccess(), "Reparent child at a safe point");
            TransformComponent childTransform;
            context.Check(scene.GetTransform(childId, childTransform).IsSuccess() && childTransform.worldValid
                && Near(childTransform.worldMatrix[3].x, 1.0f) && Near(childTransform.worldMatrix[3].y, 2.0f) && Near(childTransform.worldMatrix[3].z, 1.0f), "TransformSystem publishes the expected hierarchy result");
            const EntityReference childReference = scene.MakeReference(childId);
            context.Check(scene.Resolve(childReference) == childId, "Persistent reference resolves in the owning Scene");

            // 入队只收集值，关系合法性在提交时判断；失败不得污染原有父链。
            context.Check(scene.QueueSetParent(parentId, childId).IsSuccess() && !manager.UpdateActive().IsSuccess(), "Cycle is rejected at command commit");
            RelationshipComponent relationship;
            context.Check(scene.GetRelationship(parentId, relationship).IsSuccess() && !relationship.parent.HasValue(), "Failed cycle leaves parent at root");
            context.Check(scene.GetRelationship(childId, relationship).IsSuccess() && relationship.parent == parentId, "Failed cycle leaves child parent unchanged");
            Scene foreign("Foreign");
            context.Check(foreign.QueueCreate(Id(1), "Other parent", {}).IsSuccess() && foreign.CommitPending().IsSuccess(), "Create a foreign Scene entity");
            context.Check(scene.QueueSetParent(childId, foreign.Resolve(Id(1))).IsSuccess() && !manager.UpdateActive().IsSuccess(), "Cross-World parent is rejected");
            context.Check(!foreign.Resolve(childReference).HasValue(), "Reference cannot cross Scenes even with equal persistent ID");

            parent.translation.x = 5.0f;
            context.Check(scene.QueueSetLocal(parentId, parent).IsSuccess() && manager.UpdateActive().IsSuccess(), "Edit parent local transform");
            context.Check(scene.GetTransform(childId, childTransform).IsSuccess() && Near(childTransform.worldMatrix[3].x, 5.0f), "Parent edit propagates to child world matrix");
            const Math::Mat4 beforeDetach = childTransform.worldMatrix;
            context.Check(scene.QueueSetParent(childId, {}, true).IsSuccess() && manager.UpdateActive().IsSuccess(), "Keep-world detach accepts representable TRS");
            context.Check(scene.GetTransform(childId, childTransform).IsSuccess() && NearMatrix(childTransform.worldMatrix, beforeDetach), "Keep-world detach preserves the world matrix");

            // 非均匀缩放父节点下旋转的子节点产生shear，不能无损转成根TRS。
            child.rotation = YRotation(std::numbers::pi_v<float> / 4.0f);
            context.Check(scene.QueueSetLocal(childId, child).IsSuccess() && scene.QueueSetParent(childId, parentId).IsSuccess() && manager.UpdateActive().IsSuccess(), "Create a sheared world hierarchy");
            context.Check(scene.GetTransform(childId, childTransform).IsSuccess(), "Read sheared world transform");
            const Math::Mat4 sheared = childTransform.worldMatrix;
            context.Check(scene.QueueSetParent(childId, {}, true).IsSuccess() && !manager.UpdateActive().IsSuccess(), "Keep-world detach rejects nonrepresentable shear");
            context.Check(scene.GetRelationship(childId, relationship).IsSuccess() && relationship.parent == parentId, "Failed keep-world edit preserves parent relationship");
            context.Check(scene.GetTransform(childId, childTransform).IsSuccess() && NearMatrix(childTransform.worldMatrix, sheared), "Failed keep-world edit preserves cached world matrix");

            const auto count = scene.GetWorld().GetEntityCount();
            context.Check(scene.QueueCreate(Id(1), "Duplicate", {}).IsSuccess() && !manager.UpdateActive().IsSuccess(), "Duplicate persistent identity is rejected at commit");
            context.Check(scene.GetWorld().GetEntityCount() == count && scene.Resolve(Id(1)) == parentId, "Failed duplicate creation preserves entity storage and identity map");
            context.Check(scene.QueueDestroySubtree(parentId).IsSuccess() && manager.UpdateActive().IsSuccess(), "Destroy parent and descendants at one safe point");
            context.Check(!scene.GetWorld().IsAlive(parentId) && !scene.GetWorld().IsAlive(childId)
                && !scene.Resolve(Id(1)).HasValue() && !scene.Resolve(Id(2)).HasValue() && !scene.Resolve(childReference).HasValue(), "Subtree deletion invalidates entity and persistent references");
            context.Check(scene.QueueCreate(Id(2), "Replacement", {}).IsSuccess() && manager.UpdateActive().IsSuccess(), "Reuse an author ID after deletion");
            context.Check(scene.Resolve(Id(2)).HasValue() && !scene.Resolve(childReference).HasValue(), "Old runtime reference cannot revive after ID reuse");

            WorldStatus unloadDuringRead;
            context.Check(scene.GetWorld().ForEach([&](EntityId) { unloadDuringRead = manager.Unload(handle); return WorldStatus{}; }).IsSuccess(), "Visit Scene-owned World");
            context.Check(unloadDuringRead.code == WorldError::Busy && manager.GetScene(handle) != nullptr, "Scene cannot unload inside its World read callback");
            SceneManager otherManager;
            context.Check(otherManager.GetScene(handle) == nullptr && !otherManager.SetActive(handle).IsSuccess(), "SceneHandle is isolated between managers");
            context.Check(manager.Unload(handle).IsSuccess() && manager.GetScene(handle) == nullptr && manager.GetActive() == nullptr, "Unloading invalidates active SceneHandle");
            SceneHandle replacementHandle;
            context.Check(manager.CreateScene("Replacement scene", replacementHandle).IsSuccess() && manager.GetScene(handle) == nullptr, "Scene slot reuse does not revive a stale handle");
            Scene* replacementScene = manager.GetScene(replacementHandle);
            context.Check(replacementScene && replacementScene->QueueCreate(Id(10), "Clear guard", {}).IsSuccess()
                && replacementScene->CommitPending().IsSuccess(), "Prepare a Scene for guarded Clear");
            if (replacementScene) {
                WorldStatus guardedClear;
                context.Check(replacementScene->GetWorld().ForEach([&](EntityId) { guardedClear = manager.Clear(); return WorldStatus{}; }).IsSuccess(), "Read while Clear is requested");
                context.Check(guardedClear.code == WorldError::Busy && manager.GetScene(replacementHandle) == replacementScene, "Rejected Clear preserves all Scene instances");
            }
            context.Check(manager.Clear().IsSuccess() && manager.GetScene(replacementHandle) == nullptr, "Clear invalidates Scene handles outside a read scope");
            SceneHandle afterClear;
            context.Check(manager.CreateScene("After Clear", afterClear).IsSuccess()
                && manager.GetScene(replacementHandle) == nullptr && manager.GetScene(handle) == nullptr, "Clear then create cannot revive previously issued handles");
        }

        // 同批重挂接消费最新作者值；世界矩阵溢出时禁止发布半更新缓存。
        void CheckTransformPublication(TestContext& context)
        {
            SceneManager manager;
            SceneHandle handle;
            if (!manager.CreateScene("Publication contracts", handle).IsSuccess() || !manager.SetActive(handle).IsSuccess()) {
                context.Check(false, "Prepare Scene for transform publication");
                return;
            }
            Scene& scene = *manager.GetScene(handle);
            Math::TransformTRS parent;
            parent.translation = { 1.0f, 0.0f, 0.0f };
            Math::TransformTRS child;
            child.translation = { 2.0f, 0.0f, 0.0f };
            context.Check(scene.QueueCreate(Id(20), "Parent", parent).IsSuccess()
                && scene.QueueCreate(Id(21), "Child", child).IsSuccess() && manager.UpdateActive().IsSuccess(), "Create publication hierarchy entities");
            const EntityId parentId = scene.Resolve(Id(20)), childId = scene.Resolve(Id(21));
            if (!parentId.HasValue() || !childId.HasValue()) { context.Check(false, "Resolve publication entities"); return; }
            context.Check(scene.QueueSetParent(childId, parentId).IsSuccess() && manager.UpdateActive().IsSuccess(), "Publish initial hierarchy");
            parent.translation.x = 10.0f;
            context.Check(scene.QueueSetLocal(parentId, parent).IsSuccess() && scene.QueueSetParent(childId, {}, true).IsSuccess()
                && manager.UpdateActive().IsSuccess(), "SetLocal followed by keep-world detach in one FIFO batch");
            TransformComponent beforeParent, beforeChild;
            context.Check(scene.GetTransform(parentId, beforeParent).IsSuccess() && scene.GetTransform(childId, beforeChild).IsSuccess()
                && beforeChild.worldValid && Near(beforeChild.local.translation.x, 12.0f) && Near(beforeChild.worldMatrix[3].x, 12.0f), "Keep-world uses the latest authored parent, not the old cache");

            // 每个local都有限，但相乘后的世界矩阵超出float；两实体都保留旧派生矩阵。
            parent.scale = { std::numeric_limits<float>::max(), 1.0f, 1.0f };
            child.translation = { 0.0f, 0.0f, 0.0f };
            child.scale = { 2.0f, 1.0f, 1.0f };
            context.Check(scene.QueueSetLocal(parentId, parent).IsSuccess() && scene.QueueSetLocal(childId, child).IsSuccess()
                && scene.QueueSetParent(childId, parentId).IsSuccess() && !manager.UpdateActive().IsSuccess(), "World transform overflow reports failure");
            TransformComponent failedParent, failedChild;
            context.Check(scene.GetTransform(parentId, failedParent).IsSuccess() && scene.GetTransform(childId, failedChild).IsSuccess()
                && !failedParent.worldValid && !failedChild.worldValid
                && SameMatrix(failedParent.worldMatrix, beforeParent.worldMatrix) && SameMatrix(failedChild.worldMatrix, beforeChild.worldMatrix), "Failed transform update preserves all previous matrices and marks them invalid");
            parent.scale = { 1.0f, 1.0f, 1.0f };
            context.Check(scene.QueueSetLocal(parentId, parent).IsSuccess() && manager.UpdateActive().IsSuccess()
                && scene.GetTransform(childId, failedChild).IsSuccess() && failedChild.worldValid, "Corrected author data recovers transform publication");
        }
    }

    void RunTransformHierarchyTests(TestContext& context)
    {
        CheckMath(context);
        CheckScenes(context);
        CheckTransformPublication(context);
    }
}
