#pragma once

#include "Renderer/Readback/Picking.h"
#include "RHI/RHICompletionPoint.h"
#include "RHI/RHIError.h"
#include "RHI/RHIHandle.h"

namespace Catalpa {
	class RHIDevice;
	class RenderGraph;

	/*
	 * 每次点击只排队读取一个像素。每张票据独占读回缓冲区和对应显示帧的标识表。
	 * 渲染目标由渲染器持有，通过借用引用延长其生命周期。管理器不另行创建纹理所有者。
	 */
	class CATALPA_API ReadbackManager {
	public:
		explicit ReadbackManager(RHIDevice& device) : m_Device(device) {}
		~ReadbackManager();

		/**
		 * @brief 为已显示图像的指定像素登记异步拾取，保存该帧的实例编号表。
		 *
		 * @param[in] request 拾取所在表面、帧、目标代次及其像素坐标。
		 * @param[in] source 源整数 ID 图像视图，像素值对应当时显示帧的实例表。
		 * @param[in] keepAlive 保活源图像及其视图的共享引用，读回请求完成或取消时释放。
		 * @param[in] identities 显示帧的一基实例索引对应表；函数复制该表以免后续场景更新改变命中结果。
		 * @param[out] ticket 成功时输出唯一读回票号，交给 Poll 或 Cancel 使用。
		 *
		 * @return 返回请求入队状态；可用 ticket 查询一次结果，登记成功时尚未发生 GPU 复制。
		 */
		[[nodiscard]] RHIStatus Request(const PickingRequest&, TextureViewHandle, std::shared_ptr<void> keepAlive,
		    std::span<const RenderInstanceId> identities, uint64_t& ticket);
		/**
		 * @brief 为未提交的拾取请求登记从 ID 纹理到主机读回缓冲区的单像素复制。
		 *
		 * @param[in,out] graph 接收源视图、读回缓冲区及单像素复制任务的当前渲染图。
		 *
		 * @return 返回资源准备、导入及复制任务登记状态；成功后仍须等待真实提交完成。
		 */
		[[nodiscard]] RHIStatus AddPasses(RenderGraph&);
		/**
		 * @brief 关联读回命令和本次提交的完成点。
		 *
		 * @param[in] completion 提交这些读回任务的队列完成点。
		 */
		void OnSubmitted(RHICompletionPoint) noexcept;
		/**
		 * @brief 根据缓冲区最后一次使用和主机可读状态，认领跨队列批次中已提交的读回请求。
		 *
		 * @param[in] accepted 本帧实际已受理的队列完成点，须包含同一设备、同一队列且覆盖目标最后使用的序号。
		 */
		void OnAccepted(std::span<const RHICompletionPoint>) noexcept;
		void OnCancelled() noexcept;
		/**
		 * @brief 查询单个拾取请求，待 GPU 写入完成后读取像素并解析其显示帧实体。
		 *
		 * @param[in] ticket Request 返回的票号；产生最终结果后自动失效。
		 * @param[out] output 请求完成时接收票号、原始请求、状态及命中实体；背景像素产生无命中的就绪结果。
		 *
		 * @return 返回 true 表示票号已消费且 output 包含最终状态，包括读取失败；票号不存在或仍待执行时返回 false。
		 */
		[[nodiscard]] bool Poll(uint64_t ticket, PickingResult& output);
		/**
		 * @brief 放弃读回结果并释放对应缓冲区的逻辑所有权。
		 *
		 * @param[in] ticket 待取消的票号。
		 */
		void Cancel(uint64_t ticket) noexcept;
		/**
		 * @brief 取消属于同一预览表面的所有未消费读回。
		 *
		 * @param[in] surfaceId 被关闭或重新分配的预览表面标识。
		 */
		void CancelSurface(uint64_t surfaceId) noexcept;

	private:
		struct Pending {
			PickingRequest request;
			TextureViewHandle source{};
			BufferHandle destination{};
			std::shared_ptr<void> keepAlive;
			std::vector<RenderInstanceId> identities;
			RHICompletionPoint completion{};
			bool recorded = false;
		};

		RHIDevice& m_Device;
		std::map<uint64_t, Pending> m_Pending;
		uint64_t m_NextTicket = 1;
	};
}
