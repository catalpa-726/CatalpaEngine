#include "TestRunner.h"

#include "Log/Log.h"
#include <exception>

namespace Catalpa::Tests {

    void TestContext::Check(bool condition, std::string_view requirement, std::source_location location)
    {
        ++m_Checks;
        if (condition) return;
        ++m_Failures;
        CAT_ERROR("[S03.1 FAIL] {0} ({1}:{2})", requirement, location.file_name(), location.line());
    }

    int RunS031Tests()
    {
        CAT_INFO("[S03.1] CPU contracts: math, World, commands, Scene and hierarchy. No window/GPU initialization.");
        TestContext context;
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
}
