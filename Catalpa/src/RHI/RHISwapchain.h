#pragma once

#include "Core/Base.h"
#include "RHIError.h"
#include "RHISwapchainDesc.h"
#include "RHIHandle.h"
#include "RHICompletionPoint.h"

/* 提供上层能够持有的交换链接口 */
namespace Catalpa {

	class CATALPA_API RHISwapchain {
	public:
		virtual ~RHISwapchain() = default;

		virtual const RHISwapchainInfo& GetInfo() const = 0;

		/**
		 * @brief 在旧交换链相关命令及呈现操作收尾后按新描述重建资源。
		 *
		 * @param[in] desc 期望的图像尺寸、格式和呈现配置。
		 *
		 * @return 返回在已有同步条件下重建交换链及其图像的状态。
		 */
		[[nodiscard]] virtual RHIStatus Recreate(const RHISwapchainDesc& desc) = 0;
		/**
		 * @brief 释放交换链的图像视图、同步对象和呈现资源。
		 */
		virtual void Shutdown() = 0;

		/**
		 * @brief 按当前代次内的图像索引取得用于渲染的交换链视图。
		 *
		 * @param[in] imageIndex 当前交换链图像范围内的零基索引。
		 *
		 * @return 返回由交换链管理的借用视图句柄；交换链重建后必须重新取得。
		 */
		virtual TextureViewHandle GetImageViewHandle(uint32_t imageIndex) const = 0;

		/**
		 * @brief 使用已准备的帧槽获取交换链图像及配套同步标记。
		 *
		 * @param[in] frame 调用方已准备的帧槽令牌。
		 * @param[in] timeoutNanoseconds 等待相关同步条件成立的最长时间，单位为纳秒。
		 * @param[out] acquired 接收视图、图像索引和本次同步标识。
		 *
		 * @return 返回获取操作状态，图像可用性与交换链变化由 acquired 的结果字段给出。
		 */
		[[nodiscard]] virtual RHIStatus Acquire(
		    RHIFrameToken frame, uint64_t timeoutNanoseconds, RHIAcquireResult& acquired) = 0;

		/**
		 * @brief 将已经完成渲染提交且已转为呈现布局的图像交给呈现队列。
		 *
		 * @param[in] acquired 先前取得的交换链图像及获取标识。
		 * @param[out] outcome 输出呈现是否已进入队列，调用开始时设为 NotQueued。
		 *
		 * @return 返回呈现调用的状态，具体呈现结果另写入 outcome。
		 */
		[[nodiscard]] virtual RHIStatus Present(const RHIAcquireResult& acquired, RHIPresentOutcome& outcome) = 0;

		/**
		 * @brief 等待交换链先前登记的呈现操作完成，供安全重建或释放。
		 *
		 * @return 返回全部已用呈现栅栏的等待状态；设备未提供该能力时返回 Unsupported。
		 */
		[[nodiscard]] virtual RHIStatus WaitForPresentCompletion() = 0;

		/**
		 * @brief 在未提交渲染的情况下收尾已获取的图像和同步信号。
		 *
		 * @param[in,out] acquired 要收尾的图像及其获取凭证，取消完成后清空。
		 * @param[out] completion 成功时返回图形队列此次收尾操作的完成点，调用开始时置空。
		 *
		 * @return 返回取消获取的提交结果；成功时用 completion 等待信号量收尾操作完成。
		 */
		[[nodiscard]] virtual RHIStatus CancelAcquire(RHIAcquireResult& acquired, RHICompletionPoint& completion) = 0;
	};

}