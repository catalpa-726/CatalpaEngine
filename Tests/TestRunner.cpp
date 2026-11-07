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
            RunModelPreviewCameraTests(context);
        } catch (const std::exception& exception) {
            context.Check(false, exception.what());
        } catch (...) {
            context.Check(false, "Unexpected non-standard exception");
        }
        CAT_INFO("[S05/S06 CPU/IO] {0} checks, {1} failures.", context.GetCheckCount(),
                 context.GetFailureCount());
        return context.GetFailureCount() == 0 ? 0 : 1;
    }
    int RunS07S09Tests()
    {
        CAT_INFO("[S07-S09 CPU/IO] Lighting/IBL, frame ABI, offscreen/readback contracts and editor transactions. No window/GPU initialization.");
        TestContext context("S07-S09 CPU/IO");
        try {
            context.Check(RunS05S06Tests() == 0, "Existing CPU/IO regression remains valid");
            RunLightingTests(context);
            RunEnvironmentPreprocessTests(context);
            RunAdvancedGraphTests(context);
            RunSurfaceContractsTests(context);
            RunEditorCommandTests(context);
            RunSimulationLifecycleTests(context);
        } catch (const std::exception& error) { context.Check(false, error.what()); }
        catch (...) { context.Check(false, "Unexpected non-standard exception"); }
        CAT_INFO("[S07-S09 CPU/IO] {0} checks, {1} failures.", context.GetCheckCount(), context.GetFailureCount());
        return context.GetFailureCount() == 0 ? 0 : 1;
    }

    int RunS10S13Tests()
    {
        CAT_INFO("[S10-S13 CPU/IO] Author scenes/prefabs, graph semantics/source generation, real Lua, behavior execution and Play isolation. No window/GPU initialization.");
        TestContext context("S10-S13 CPU/IO");
        try {
            context.Check(RunS07S09Tests()==0,"Existing CPU/IO regression remains valid");
            RunSceneAuthoringTests(context);
            RunAuthorPipelineTests(context);
            RunMaterialGraphTests(context);
            RunScriptingTests(context);
        } catch(const std::exception& exception) {context.Check(false,exception.what());}
        catch(...) {context.Check(false,"Unexpected non-standard exception");}
        CAT_INFO("[S10-S13 CPU/IO] {0} checks, {1} failures.",context.GetCheckCount(),context.GetFailureCount());
        return context.GetFailureCount()==0?0:1;
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
    int RunS14FoundationTests() {
        CAT_INFO("Delivery/localization/dynamic-phase/Loopback CPU contracts. No window/GPU/audio device. "
                 "This is not full S14-S17 acceptance.");
        TestContext test("Delivery CPU/IO");
        try {
            test.Check(RunS10S13Tests() == 0, "Existing CPU/IO regressions remain valid");
            RunDeliveryContractsTests(test);
            RunLoopbackContractsTests(test);
            RunDynamicPhaseContractsTests(test);
        } catch (const std::exception& e) {
            test.Check(false, e.what());
        } catch (...) {
            test.Check(false, "Unexpected non-standard exception");
        }
        CAT_INFO("Delivery CPU/IO: {0} checks, {1} failures", test.GetCheckCount(), test.GetFailureCount());
        return test.GetFailureCount() == 0 ? 0 : 1;
    }
} // namespace Catalpa::Tests

namespace Catalpa::Tests {
    int RunS14S17Tests() {
        CAT_INFO("Delivery/dynamic domains CPU/IO/native-CPU contracts. No window/GPU/audio device. GPU, IME "
                 "and interactive acceptance remain separate.");
        TestContext context("Delivery and dynamic domains");
        try {
            context.Check(RunS14FoundationTests() == 0, "Existing regression and delivery contracts");
            RunOptimizationContractsTests(context);
            RunAssetInspectionTests(context);
            RunAssetChangeTests(context);
            RunAdvancedSchedulingTests(context);
            RunAnimationContractsTests(context);
            RunParticleContractsTests(context);
            RunPhysicsNavigationTests(context);
            RunTerrainTests(context);
            RunAudioDomainTests(context);
            RunUIDomainTests(context);
            RunScriptLifecycleTests(context);
            RunReplicationContractsTests(context);
        } catch (const std::exception& error) {
            context.Check(false, error.what());
        } catch (...) {
            context.Check(false, "Unexpected non-standard exception");
        }
        CAT_INFO("Delivery/dynamic domains: {0} checks, {1} failures", context.GetCheckCount(),
                 context.GetFailureCount());
        return context.GetFailureCount() == 0 ? 0 : 1;
    }
} // namespace Catalpa::Tests
