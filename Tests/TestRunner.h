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
    // 多view CPU请求/独立历史/逐view UBO及视锥可见性；离屏GPU与UI验收另外执行。
    void RunFrustumVisibilityTests(TestContext& context);
    void RunMultiViewTests(TestContext& context);
    void RunMultiViewPackingTests(TestContext& context);
    void RunMultiViewExtractionTests(TestContext& context);
    // 单队列图依赖、完成集合和真实计划调度的CPU替身检查，不创建GPU或证明原生退休。
    void RunRenderGraphHazardTests(TestContext& context);
    void RunCompletionTests(TestContext& context);
    void RunFrameLifetimeTests(TestContext& context);
    int RunS034Tests();
    // 身份/VFS/资产登记/作者项目契约；文件用例仅写独占临时目录。
    void RunStableIdTests(TestContext& context);
    void RunVirtualFileSystemTests(TestContext& context);
    void RunAssetRegistryTests(TestContext& context);
    void RunProjectConfigTests(TestContext& context);
    int RunS041Tests();
    // 整章集中入口：包含已有CPU/IO回归、领域schema、真实cooked ABI及异步生命周期。
    void RunMaterialSchemaTests(TestContext&);
    void RunImageValidationTests(TestContext&);
    void RunGeometryAssetTests(TestContext&);
    void RunComponentRegistryTests(TestContext&);
    void RunMemoryBudgetTests(TestContext&);
    void RunAssetLifecycleTests(TestContext&);
    void RunAssetBudgetTests(TestContext&);
    void RunShaderMetadataTests(TestContext&);
    void RunGpuCacheTests(TestContext&);
    int RunS04Tests();
    void RunAssetPipelineTests(TestContext&);
    void RunModelPreviewCameraTests(TestContext&);
    int RunS05S06Tests(); // CPU/IO：真实解析器、Cook/DDC、加载/实例化和关闭协议。
    void RunLightingTests(TestContext&);
    void RunEnvironmentPreprocessTests(TestContext&);
    void RunAdvancedGraphTests(TestContext&);
    void RunSurfaceContractsTests(TestContext&);
    void RunEditorCommandTests(TestContext&);
    void RunSimulationLifecycleTests(TestContext&);
    int RunS07S09Tests(); // 联合 CPU/IO 契约；实际 GPU 画面和读回由窗口验收。
}
