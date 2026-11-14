#pragma once

#include "catpch.h"

#include "Core/Base.h"
#include "RHI/RHIError.h"
#include "RHI/RHIHandle.h"
#include "RHI/RHITypes.h"
#include "Images/PixelFormat.h"
#include "Renderer/RenderFramePacket.h"
#include "Renderer/Visibility/VisibilitySystem.h"

namespace Catalpa {
	class RHIDevice;
	struct ShaderMetadata;

	/* 各独立视图共用的显式表现配置；正式画质分档在后续扩展同一入口。 */
	struct RendererProfile {
		PixelFormat depthFormat = PixelFormat::D32_Float;
		bool depthPrepass = true;
		bool normalDebug = false;
		std::array<float, 4> clearColor{0.02f, 0.02f, 0.025f, 1.0f};
		bool asyncQueues = true; /* false保留同一状态/完成协议，全部批次交给graphics串行执行。 */
	};

	/**
	 * @brief 校验实际部署的 JSON。失败时保留原输出，校验独立于进程工作目录和 GPU 对象。
	 *
	 * @param[in] json 配置文件的 JSON 文本。
	 * @param[out] output 成功时接收解析后的配置。
	 *
	 * @return 返回渲染配置的解析和校验结果；失败时保留原输出。
	 */
	[[nodiscard]] CATALPA_API RHIStatus ParseRendererProfile(std::string_view json, RendererProfile& output);

	/* GPU ABI由float4列显式定义；与Common/Bindings.slang逐字段对应，不直接上传GLM对象。 */
	struct alignas(16) FrameGpuParameters {
		std::array<float, 4>
		    time{}; /* 四个分量依次为仿真时间对 4096 取模、实际帧间隔、固定步长和插值系数。前三项以秒计，插值系数无量纲。 */
		std::array<float, 4> options{}; /* x=normalDebug，y=Blend拾取alpha阈值，z=4096秒epoch，w保留。 */
	};
	struct alignas(16) ViewGpuParameters {
		std::array<std::array<float, 4>, 4> viewProjectionColumns{};
		std::array<float, 4> cameraPosition{};
		std::array<std::array<float, 4>, 4> inverseViewProjectionColumns{};
		std::array<std::array<float, 4>, 4> viewColumns{};
	};
	struct alignas(16) DrawGpuParameters {
		std::array<std::array<float, 4>, 4> worldColumns{};
		std::array<std::array<float, 4>, 3> normalColumns{};
		std::array<float, 4> color{};
		std::array<uint32_t, 4> ids{}; /* x为一基实例token，y为镜像手性bit；0 token专属背景。 */
	};

	static_assert(sizeof(float) == 4);
	static_assert(sizeof(FrameGpuParameters) == 32 && offsetof(FrameGpuParameters, options) == 16);
	static_assert(sizeof(ViewGpuParameters) == 208 && offsetof(ViewGpuParameters, cameraPosition) == 64 &&
	    offsetof(ViewGpuParameters, inverseViewProjectionColumns) == 80 &&
	    offsetof(ViewGpuParameters, viewColumns) == 144);
	static_assert(sizeof(DrawGpuParameters) == 144 && offsetof(DrawGpuParameters, normalColumns) == 64 &&
	    offsetof(DrawGpuParameters, color) == 112 && offsetof(DrawGpuParameters, ids) == 128);
	static_assert(std::is_trivially_copyable_v<FrameGpuParameters> && std::is_trivially_copyable_v<ViewGpuParameters> &&
	    std::is_trivially_copyable_v<DrawGpuParameters>);

	/**
	 * @brief 将帧、视图和绘制参数的主机数据布局与着色器入口反射信息逐项比对。
	 * 在 CPU 上核对前向着色器中 Frame（片元）、View（顶点）及 Draw（顶点和片元）的布局与主机打包结果。
	 * 支持直接 float4 数组，以及 Slang 生成的单 data 成员包装。保留原始元数据并检查两层布局。
	 *
	 * @param[in] metadata 着色器反射记录。
	 * @param[in] vertexEntry 顶点着色器入口名称。
	 * @param[in] fragmentEntry 片元着色器入口名称。
	 * @param[out] error 不匹配时接收具体布局错误。
	 *
	 * @return 反射得到的入口和着色器绑定布局与当前主机端数据布局一致时返回 true。
	 */
	[[nodiscard]] CATALPA_API bool ValidateFrameShaderInterface(const ShaderMetadata& metadata,
	    std::string_view vertexEntry, std::string_view fragmentEntry, std::string& error);

	/* data包含已清零的对齐间隙；每个binding使用自己的精确range，不把整个buffer误作一个UBO。 */
	struct PackedFrameUniforms {
		std::vector<std::byte> data;
		/* 与packet.views逐项对应；Frame和共享Draw只保存一份，各view拥有精确独立range。 */
		std::vector<uint64_t> viewOffsets;
		std::vector<uint64_t> drawOffsets;
	};

	/**
	 * @brief 按实际展示目标尺寸重建投影/逆与最终VP；不回写Scene或来源视图，Y翻转仍仅在投影一次。
	 *
	 * @param[in] source 保留相机姿态和镜头参数的来源视图。
	 * @param[in] extent 实际渲染目标的像素尺寸。
	 * @param[out] output 接收匹配目标宽高比的视图。
	 *
	 * @return 返回按展示目标尺寸重建视图矩阵的状态。
	 */
	[[nodiscard]] CATALPA_API RHIStatus BuildPresentationView(
	    const RenderView& source, Extent2D extent, RenderView& output) noexcept;

	/**
	 * @brief 按设备的对齐和读取范围限制打包帧、视图和绘制常量。
	 * 在 CPU 上打包已由 RenderScene 核实身份及几何的输入，并另外检查上传数值、布局和范围。
	 * 对齐与范围上限来自设备记录的能力，失败时保留输出。
	 *
	 * @param[in] packet 提供时间、相机和实例信息的场景帧。
	 * @param[in] profile 表现和调试选项。
	 * @param[in] alignment 设备要求的统一缓冲区偏移对齐，以字节计。
	 * @param[in] maxRange 单个统一缓冲区绑定允许的最大范围，以字节计。
	 * @param[out] output 接收精确偏移和已填充的字节数组。
	 *
	 * @return 返回帧常量的打包状态；数据或设备范围无效时保留原输出。
	 */
	[[nodiscard]] CATALPA_API RHIStatus BuildFrameUniformData(const RenderFramePacket& packet,
	    const RendererProfile& profile, uint64_t alignment, uint64_t maxRange, PackedFrameUniforms& output);

	/* 每帧槽独占一个Upload UBO；调用者必须先成功PrepareFrame(slot)再覆写，图导入发生在写入之后。 */
	class RendererFrameContext {
	public:
		explicit RendererFrameContext(RHIDevice& device) : m_Device(device) {}
		~RendererFrameContext();
		RendererFrameContext(const RendererFrameContext&) = delete;
		RendererFrameContext& operator=(const RendererFrameContext&) = delete;

		/**
		 * @brief 为帧槽生成展示视图、可见性结果及对齐后的统一缓冲数据。
		 *
		 * @param[in] slot 要更新的帧槽索引。
		 * @param[in] packet 当前场景的渲染快照。
		 * @param[in] profile 表现与深度配置。
		 * @param[in] targetExtent 实际渲染目标像素尺寸。
		 *
		 * @return 返回为当前帧槽准备视图、可见列表及常量缓冲的结果。
		 */
		[[nodiscard]] RHIStatus Prepare(
		    uint32_t slot, const RenderFramePacket& packet, const RendererProfile& profile, Extent2D targetExtent);
		[[nodiscard]] BufferHandle GetBuffer(uint32_t slot) const noexcept;
		[[nodiscard]] const PackedFrameUniforms* GetPacking(uint32_t slot) const noexcept;
		/**
		 * @brief 借用该帧槽已准备的实际目标相机/可见清单；不存在的槽或view返回nullptr。
		 *
		 * @param[in] slot 已准备视图数据的帧槽索引。
		 * @param[in] index 该帧槽中要读取的视图索引，越界时返回空指针。
		 *
		 * @return 返回该帧槽内指定视图的借用指针，索引越界时返回空指针。
		 */
		[[nodiscard]] const RenderView* GetView(uint32_t slot, size_t index = 0) const noexcept;
		[[nodiscard]] const VisibilityResult* GetVisibility(uint32_t slot, size_t index = 0) const noexcept;
		void OnSubmitted() noexcept { m_ViewHistory.OnSubmitted(); }
		void OnCancelled() noexcept { m_ViewHistory.OnCancelled(); }
		void ClearHistory() noexcept { m_ViewHistory.Clear(); }

	private:
		struct Slot {
			BufferHandle buffer{};
			uint64_t capacity = 0;
			PackedFrameUniforms packing;
			std::vector<RenderView> views;
			std::vector<VisibilityResult> visibility;
		};
		RHIDevice& m_Device;
		std::vector<Slot> m_Slots;
		/* 队列接受本帧提交后才推进历史；获取、录制或提交失败时丢弃待提交值。 */
		RenderViewHistory m_ViewHistory;
	};
}
