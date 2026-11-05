#include "TestRunner.h"

#include "Log/Log.h"
#include <exception>

namespace Catalpa::Tests {

    void TestContext::Check(bool condition, std::string_view requirement, std::source_location location)
    {
        ++m_Checks;
        if (condition) return;
        ++m_Failures;
        CAT_ERROR("[{0} FAIL] {1} ({2}:{3})", m_Group, requirement, location.file_name(), location.line());
    }

    int RunS031Tests()
    {
        CAT_INFO("[S03.1] CPU contracts: math, World, commands, Scene and hierarchy. No window/GPU initialization.");
        TestContext context("S03.1");
        try {
            RunWorldTests(context);
            RunTransformHierarchyTests(context);
        }
        catch (const std::exception& error) {
            context.Check(false, error.what());
        }
        catch (...) {
            context.Check(false, "Unexpected non-standard exception");
        }
        CAT_INFO("[S03.1] {0} checks, {1} failures.", context.GetCheckCount(), context.GetFailureCount());
        return context.GetFailureCount() == 0 ? 0 : 1;
    }

    int RunS032Tests()
    {
        CAT_INFO("[S03.2] CPU contracts: S03.1 regression, scheduling/time, input, projection and owned snapshots. No window/GPU initialization.");
        TestContext context("S03.2");
        try {
            RunWorldTests(context);
            RunTransformHierarchyTests(context);
            RunSystemSchedulerTests(context);
            RunInputRoutingTests(context);
            RunProjectionFrustumTests(context);
            RunSimulationPipelineTests(context);
        } catch (const std::exception& error) { context.Check(false, error.what()); }
        catch (...) { context.Check(false, "Unexpected non-standard exception"); }
        CAT_INFO("[S03.2] {0} checks, {1} failures.", context.GetCheckCount(), context.GetFailureCount());
        return context.GetFailureCount() == 0 ? 0 : 1;
    }
    int RunS033Tests()
    {
        CAT_INFO("[S03.3 CPU] S03.1/S03.2 regression, geometry, owned instances, frame packing and renderer profiles. No window/GPU initialization.");
        TestContext context("S03.3 CPU");
        try {
            RunWorldTests(context);
            RunTransformHierarchyTests(context);
            RunSystemSchedulerTests(context);
            RunInputRoutingTests(context);
            RunProjectionFrustumTests(context);
            RunSimulationPipelineTests(context);
            RunMeshValidationTests(context);
            RunGeometrySnapshotTests(context);
            RunFramePackingTests(context);
        } catch (const std::exception& error) { context.Check(false, error.what()); }
        catch (...) { context.Check(false, "Unexpected non-standard exception"); }
        CAT_INFO("[S03.3 CPU] {0} checks, {1} failures.", context.GetCheckCount(), context.GetFailureCount());
        return context.GetFailureCount() == 0 ? 0 : 1;
    }
    int RunS034Tests()
    {
        CAT_INFO("[S03.4 CPU] Previous CPU contracts, view history/visibility, graph dependencies and frame submission/completion. No window/GPU initialization.");
        TestContext context("S03.4 CPU");
        try {
            RunWorldTests(context);
            RunTransformHierarchyTests(context);
            RunSystemSchedulerTests(context);
            RunInputRoutingTests(context);
            RunProjectionFrustumTests(context);
            RunSimulationPipelineTests(context);
            RunMeshValidationTests(context);
            RunGeometrySnapshotTests(context);
            RunFramePackingTests(context);
            RunFrustumVisibilityTests(context);
            RunMultiViewTests(context);
            RunMultiViewPackingTests(context);
            RunMultiViewExtractionTests(context);
            RunRenderGraphHazardTests(context);
            RunCompletionTests(context);
            RunFrameLifetimeTests(context);
        } catch (const std::exception& error) { context.Check(false, error.what()); }
        catch (...) { context.Check(false, "Unexpected non-standard exception"); }
        CAT_INFO("[S03.4 CPU] {0} checks, {1} failures.", context.GetCheckCount(), context.GetFailureCount());
        return context.GetFailureCount() == 0 ? 0 : 1;
    }

    int RunS041Tests()
    {
        CAT_INFO("[S04.1 CPU/IO] Stable IDs, read-only mounts, asset registry and project sessions. No window/GPU initialization.");
        TestContext context("S04.1 CPU/IO");
        try {
            RunStableIdTests(context);
            RunVirtualFileSystemTests(context);
            RunAssetRegistryTests(context);
            RunProjectConfigTests(context);
            // 新身份协议也须保留既有World与场景层级行为。
            RunWorldTests(context);
            RunTransformHierarchyTests(context);
        } catch (const std::exception& error) { context.Check(false, error.what()); }
        catch (...) { context.Check(false, "Unexpected non-standard exception"); }
        CAT_INFO("[S04.1 CPU/IO] {0} checks, {1} failures.", context.GetCheckCount(), context.GetFailureCount());
        return context.GetFailureCount() == 0 ? 0 : 1;
    }

    int RunS05S06Tests() {
        CAT_INFO("[S05/S06 CPU/IO] Read-only glTF/GLB/FBX/OBJ, image decode, Cook/DDC, runtime loading and "
                 "scene transactions. No window/GPU initialization.");
        TestContext context("S05/S06 CPU/IO");
        try {
            context.Check(RunS04Tests() == 0, "Existing identity/domain/job CPU regression remains valid");
            RunAssetPipelineTests(context);
        } catch (const std::exception& exception) {
            context.Check(false, exception.what());
        } catch (...) {
            context.Check(false, "Unexpected non-standard exception");
        }
        CAT_INFO("[S05/S06 CPU/IO] {0} checks, {1} failures.", context.GetCheckCount(),
                 context.GetFailureCount());
        return context.GetFailureCount() == 0 ? 0 : 1;
    }
    int RunS04Tests()
    {
        CAT_INFO("[S04 CPU/IO] Unified identity, schemas, cooked shader ABI, owned GPU-cache contracts, jobs and async versions. No window/GPU initialization.");
        TestContext context("S04 CPU/IO");
        try {
            RunWorldTests(context); RunTransformHierarchyTests(context);
            RunSystemSchedulerTests(context); RunInputRoutingTests(context); RunProjectionFrustumTests(context);
            RunSimulationPipelineTests(context); RunMeshValidationTests(context); RunGeometrySnapshotTests(context);
            RunFramePackingTests(context); RunFrustumVisibilityTests(context); RunMultiViewTests(context);
            RunMultiViewPackingTests(context); RunMultiViewExtractionTests(context);
            RunRenderGraphHazardTests(context); RunCompletionTests(context); RunFrameLifetimeTests(context);
            RunStableIdTests(context); RunVirtualFileSystemTests(context); RunAssetRegistryTests(context); RunProjectConfigTests(context);
            RunComponentRegistryTests(context); RunGeometryAssetTests(context); RunImageValidationTests(context); RunMaterialSchemaTests(context);
            RunShaderMetadataTests(context); RunGpuCacheTests(context);
            RunMemoryBudgetTests(context); RunAssetLifecycleTests(context); RunAssetBudgetTests(context);
        } catch(const std::exception& error) {context.Check(false,error.what());}
        catch(...) {context.Check(false,"Unexpected non-standard exception");}
        CAT_INFO("[S04 CPU/IO] {0} checks, {1} failures.",context.GetCheckCount(),context.GetFailureCount());
        return context.GetFailureCount()==0?0:1;
    }
}
