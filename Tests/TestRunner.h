#pragma once

#include <cstddef>
#include <source_location>
#include <string_view>

namespace Catalpa::Tests {

    // Box 开发模式中的 CPU 契约检查；失败累计并最终返回非零退出码。
    class TestContext {
    public:
        void Check(bool condition, std::string_view requirement,
            std::source_location location = std::source_location::current());
        [[nodiscard]] std::size_t GetCheckCount() const { return m_Checks; }
        [[nodiscard]] std::size_t GetFailureCount() const { return m_Failures; }

    private:
        std::size_t m_Checks = 0;
        std::size_t m_Failures = 0;
    };

    // 用例实现分属 ECS 与 Scene；不引入第二个 main 或 GPU 初始化。
    void RunWorldTests(TestContext& context);
    void RunTransformHierarchyTests(TestContext& context);
    int RunS031Tests();
}
