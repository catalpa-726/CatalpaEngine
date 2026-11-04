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
        CAT_INFO("[S03.4 CPU] Previous CPU contracts, independent view extraction/history/packing and frustum visibility. No window/GPU initialization.");
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
        } catch (const std::exception& error) { context.Check(false, error.what()); }
        catch (...) { context.Check(false, "Unexpected non-standard exception"); }
        CAT_INFO("[S03.4 CPU] {0} checks, {1} failures.", context.GetCheckCount(), context.GetFailureCount());
        return context.GetFailureCount() == 0 ? 0 : 1;
    }
}
