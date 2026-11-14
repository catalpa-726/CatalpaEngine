#pragma once

#include <cstddef>
#include <filesystem>
#include <source_location>
#include <memory>
#include <string_view>

namespace Catalpa {
	class Layer;
}
namespace Catalpa::Tests {

	std::unique_ptr<Layer> CreateBrowserThumbnailGpuTest();
	std::unique_ptr<Layer> CreateGroveGpuTest();
	std::unique_ptr<Layer> CreateWorkspaceLanguageTest(const std::filesystem::path& directory);
	std::unique_ptr<Layer> CreateWorkspaceDockingTest();
	std::unique_ptr<Layer> CreatePlatformWindowGpuTest();

	/* Box 开发模式中的 CPU 契约检查；失败累计并最终返回非零退出码。 */
	class TestContext {
	public:
		explicit TestContext(std::string_view group = "CPU") : m_Group(group) {}
		/**
		 * @brief 统计断言数量；条件不满足时记录失败要求和调用位置。
		 *
		 * @param[in] condition 本条断言的求值结果。
		 * @param[in] requirement 断言所描述的预期行为。
		 * @param[in] location 断言调用位置，用于在失败时报告源文件和行号。
		 */
		void Check(bool condition, std::string_view requirement,
		    std::source_location location = std::source_location::current());
		[[nodiscard]] std::size_t GetCheckCount() const { return m_Checks; }
		[[nodiscard]] std::size_t GetFailureCount() const { return m_Failures; }

	private:
		std::size_t m_Checks = 0;
		std::size_t m_Failures = 0;
		std::string_view m_Group;
	};

	/**
	 * @brief 验证身份隔离、陈旧代次、延迟发布和失败回滚；不依赖渲染设备。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunWorldTests(TestContext& context);
	void RunTransformHierarchyTests(TestContext& context);
	/**
	 * @brief 运行 S031 回归检查。
	 *
	 * @return 全部检查通过时退出码为 0，至少一项失败时为 1。
	 */
	int RunS031Tests();
	/**
	 * @brief 验证调度依赖和固定步时间契约；不创建设备、窗口或 Application。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunSystemSchedulerTests(TestContext& context);
	/**
	 * @brief 只注入引擎事件，不创建窗口、模拟硬件或依赖 Box 类型。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunInputRoutingTests(TestContext& context);
	/**
	 * @brief 既有投影/相机回归入口；Frustum与多view剔除由独立分组接续。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunProjectionFrustumTests(TestContext& context);
	/**
	 * @brief 验证真实模块注册/场景安全点/提取所有权的连接，使用 CPU 服务而非模拟 GPU。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunSimulationPipelineTests(TestContext& context);
	/**
	 * @brief 运行 S032 回归检查。
	 *
	 * @return 全部检查通过时退出码为 0，至少一项失败时为 1。
	 */
	int RunS032Tests();
	/**
	 * @brief CPU 几何/资源冻结/包围体契约；不创建 GPU、窗口或另一个测试入口。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunMeshValidationTests(TestContext& context);
	/**
	 * @brief 以真实Scene命令/提取/消费链验证共享CPU几何，不创建窗口或GPU。
	 * 非均匀与镜像变换使用独立的已知解析值、切线正交和包围端点作为数学证据。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunGeometrySnapshotTests(TestContext& context);
	/**
	 * @brief 无设备/窗口的CPU契约：按独立期望字节读取显式列，不只比较两个相同packing实现。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunFramePackingTests(TestContext& context);
	/**
	 * @brief 运行 S033 回归检查。
	 *
	 * @return 全部检查通过时退出码为 0，至少一项失败时为 1。
	 */
	int RunS033Tests();
	/**
	 * @brief 独立核算可见范围/平面方向，覆盖归一化、裁剪误差和逐view的稳定可见列表。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunFrustumVisibilityTests(TestContext& context);
	/**
	 * @brief 仅CPU视图值与提交事务；GPU surface/UI纹理退休另由后续图和用户验收覆盖。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunMultiViewTests(TestContext& context);
	/**
	 * @brief 多view用各自range，共享的Frame/Draw数据不按view重复；预期偏移和系数独立写出。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunMultiViewPackingTests(TestContext& context);
	/**
	 * @brief 真实Scene安全点到多view帧包：各自选择/尺寸、共享实例、缺失/关闭及来源卸载。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunMultiViewExtractionTests(TestContext& context);
	/**
	 * @brief 同生产 Compile 使用的范围/依赖编译器；不创建窗口、GPU 或测试专用生产入口。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunRenderGraphHazardTests(TestContext& context);
	/**
	 * @brief 纯 CPU 完成要求集合；不初始化设备，不把合并成功当成 GPU 已完成。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunCompletionTests(TestContext& context);
	/**
	 * @brief 整批接受/失败/完成身份契约；真实GPU帧槽与资源退休仍须用户运行验收。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunFrameLifetimeTests(TestContext& context);
	/**
	 * @brief 运行 S034 回归检查。
	 *
	 * @return 全部检查通过时退出码为 0，至少一项失败时为 1。
	 */
	int RunS034Tests();
	/**
	 * @brief 检查持久文本协议、随机生成失败边界及真实Scene身份消费，不初始化窗口/GPU。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunStableIdTests(TestContext& context);
	/**
	 * @brief 运行虚拟文件系统回归检查。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunVirtualFileSystemTests(TestContext& context);
	/**
	 * @brief 身份、整表事务、缺失依赖诊断和版本引用；不访问磁盘、窗口或 GPU。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunAssetRegistryTests(TestContext& context);
	/**
	 * @brief 运行项目配置回归检查。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunProjectConfigTests(TestContext& context);
	/**
	 * @brief 运行 S041 回归检查。
	 *
	 * @return 全部检查通过时退出码为 0，至少一项失败时为 1。
	 */
	int RunS041Tests();
	/**
	 * @brief 检查定义、实例、共享版本、受限覆盖和最终着色器 ABI，校验独立于 RHI 和 GPU 对象。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunMaterialSchemaTests(TestContext&);
	/**
	 * @brief 以真实布局错误覆盖mip/array/cube/block/pitch与用途约束，输入保持原样。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunImageValidationTests(TestContext&);
	/**
	 * @brief 检查共享几何、模型局部身份与材质槽映射，不在World生成实体。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunGeometryAssetTests(TestContext&);
	/**
	 * @brief 执行真实的运行时注册并检验归档值树，避免将反射模式误用为 C++ 对象内存布局。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunComponentRegistryTests(TestContext&);
	/**
	 * @brief 运行内存预算回归检查。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunMemoryBudgetTests(TestContext&);
	/**
	 * @brief 运行资产生命周期回归检查。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunAssetLifecycleTests(TestContext&);
	/**
	 * @brief 运行资产预算回归检查。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunAssetBudgetTests(TestContext&);
	/**
	 * @brief 运行着色器元数据回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunShaderMetadataTests(TestContext&);
	/**
	 * @brief 运行 GPU 缓存回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunGpuCacheTests(TestContext&);
	/**
	 * @brief 运行 S04 的 CPU 和 I/O 回归检查。
	 *
	 * @return 全部检查通过时退出码为 0，至少一项失败时为 1。
	 */
	int RunS04Tests();
	/**
	 * @brief 运行资产管线回归检查。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunAssetPipelineTests(TestContext&);
	/**
	 * @brief 复用编辑器的取景计算，再以真实帧提取检查头盔大小的模型在横屏、竖屏下的相机。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunModelPreviewCameraTests(TestContext&);
	/**
	 * @brief 运行 S05 和 S06 的资产管线回归检查。
	 *
	 * @return 全部检查通过时退出码为 0，至少一项失败时为 1。
	 */
	int RunS05S06Tests(); /* CPU/IO：真实解析器、Cook/DDC、加载/实例化和关闭协议。 */
	/**
	 * @brief 运行光照回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunLightingTests(TestContext&);
	/**
	 * @brief 运行环境预处理回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunEnvironmentPreprocessTests(TestContext&);
	/**
	 * @brief 使用生产Builder/Compiler的CPU状态替身，只证明图契约，不冒称真实GPU执行。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunAdvancedGraphTests(TestContext&);
	/**
	 * @brief 运行渲染表面契约回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunSurfaceContractsTests(TestContext&);
	/**
	 * @brief 运行编辑器命令回归检查。
	 *
	 * @param[in,out] t 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunEditorCommandTests(TestContext&);
	/**
	 * @brief 运行模拟生命周期回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunSimulationLifecycleTests(TestContext&);
	/**
	 * @brief 运行 S07 至 S09 的 CPU 和 I/O 回归检查。
	 *
	 * @return 全部检查通过时退出码为 0，至少一项失败时为 1。
	 */
	int RunS07S09Tests(); /* 联合 CPU/IO 契约；实际 GPU 画面和读回由窗口验收。 */
	/**
	 * @brief 运行场景场景创作回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunSceneAuthoringTests(TestContext&);
	/**
	 * @brief 运行创作管线回归检查。
	 *
	 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunAuthorPipelineTests(TestContext&);
	/**
	 * @brief 验证材质图的序列化、类型和环路诊断，以及编译与编辑命令。
	 *
	 * @param[out] test 接收材质图断言和失败诊断的测试上下文。
	 */
	void RunMaterialGraphTests(TestContext&);
	/**
	 * @brief 运行脚本回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunScriptingTests(TestContext&);
	/**
	 * @brief 运行 S10 至 S13 的 CPU 和 I/O 回归检查。
	 *
	 * @return 全部检查通过时退出码为 0，至少一项失败时为 1。
	 */
	int RunS10S13Tests();
	/**
	 * @brief 验证部署交付和打包相关的契约。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunDeliveryContractsTests(TestContext&);
	/**
	 * @brief 运行资产检查回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunAssetInspectionTests(TestContext&);
	/**
	 * @brief 运行本地回环契约回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunLoopbackContractsTests(TestContext&);
	/**
	 * @brief 运行动态阶段契约回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunDynamicPhaseContractsTests(TestContext&);
	/**
	 * @brief 运行动画契约回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunAnimationContractsTests(TestContext&);
	/**
	 * @brief 运行粒子契约回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunParticleContractsTests(TestContext&);
	/**
	 * @brief 运行优化契约回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunOptimizationContractsTests(TestContext&);
	/**
	 * @brief 运行物理导航回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunPhysicsNavigationTests(TestContext&);
	/**
	 * @brief 运行地形回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunTerrainTests(TestContext&);
	/**
	 * @brief 运行音频领域回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunAudioDomainTests(TestContext&);
	/**
	 * @brief 运行界面领域回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunUIDomainTests(TestContext&);
	/**
	 * @brief 运行脚本生命周期回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunScriptLifecycleTests(TestContext&);
	/**
	 * @brief 运行状态复制契约回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunReplicationContractsTests(TestContext&);
	/**
	 * @brief 运行 S14 至 S17 的 CPU 和 I/O 验收，并汇总前置阶段的回归结果。
	 *
	 * @return 全部检查通过时退出码为 0，至少一项失败时为 1。
	 */
	int RunS14S17Tests();
	/**
	 * @brief 运行高级调度回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunAdvancedSchedulingTests(TestContext&);
	/**
	 * @brief 运行资产修改回归检查。
	 *
	 * @param[in,out] test 用于记录断言、失败原因和检查数量的测试上下文。
	 */
	void RunAssetChangeTests(TestContext&);
	/**
	 * @brief 运行 S14 基础回归检查。
	 *
	 * @return 全部检查通过时退出码为 0，至少一项失败时为 1。
	 */
	int RunS14FoundationTests(); /* 仅已接通的交付/语言/调度/Loopback 契约，不代表 S14-S17 全部完成。 */
}
