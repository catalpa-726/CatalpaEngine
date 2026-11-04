#pragma once

#include <cstddef>
#include <source_location>
#include <string_view>

namespace Catalpa::Tests {

    // Box 开发模式中的 CPU 契约检查；失败累计并最终返回非零退出码。
    class TestContext {
    public:
        explicit TestContext(std::string_view group = "CPU") : m_Group(group) {}
        void Check(bool condition, std::string_view requirement,
            std::source_location location = std::source_location::current());
        [[nodiscard]] std::size_t GetCheckCount() const { return m_Checks; }
        [[nodiscard]] std::size_t GetFailureCount() const { return m_Failures; }

    private:
        std::size_t m_Checks = 0;
        std::size_t m_Failures = 0;
        std::string_view m_Group;
    };

    // 用例实现分属 ECS 与 Scene；不引入第二个 main 或 GPU 初始化。
    void RunWorldTests(TestContext& context);
    void RunTransformHierarchyTests(TestContext& context);
    int RunS031Tests();
    void RunSystemSchedulerTests(TestContext& context);
    void RunInputRoutingTests(TestContext& context);
    void RunProjectionFrustumTests(TestContext& context);
    void RunSimulationPipelineTests(TestContext& context);
    int RunS032Tests();
    // CPU几何/边界及场景快照契约；本批尚不覆盖GPU深度/UBO。
    void RunMeshValidationTests(TestContext& context);
    void RunGeometrySnapshotTests(TestContext& context);
    // Frame/View/Draw字节布局、对齐与配置解析；此入口不创建窗口或GPU。
    void RunFramePackingTests(TestContext& context);
    int RunS033Tests();
}
