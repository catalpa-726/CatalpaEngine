#pragma once

#include "RHI/RHIHandle.h"
#include "RHI/RHIBufferDesc.h"
#include "RHI/RHICompletionPoint.h"
#include "RHI/RHIError.h"

// 声明静态 Buffer 上传调度器
namespace Catalpa {

    class RHIDevice;
    class RHICommandList;

    // 当前由主线程调用，只负责新建静态顶点/索引 Buffer 的上传。
    class UploadScheduler {
    public:
        explicit UploadScheduler(RHIDevice& device) : m_Device(device) {}
        ~UploadScheduler();

        UploadScheduler(const UploadScheduler&) = delete;
        UploadScheduler& operator=(const UploadScheduler&) = delete;

        // 立即复制 CPU 数据；目标所有权交给调用方，并须保留至请求提交或调度器关闭。
        [[nodiscard]] RHIStatus CreateBuffer(std::span<const std::byte> data, RHIBufferUsage usage, BufferHandle& buffer);

        // 只录制复制与屏障，不自行提交。
        [[nodiscard]] RHIStatus RecordPending(RHICommandList& commands);

        // 只接受本次真实绘制提交的完成点；提交成功不等于 GPU 已完成。
        void OnSubmitted(const RHICompletionPoint& completion) noexcept;

        // 原命令不会执行时撤销录制状态，保留 staging 内容供重录。
        void OnCancelled() noexcept;
        void Shutdown() noexcept;

        const RHICompletionPoint& GetLastSubmission() const { return m_LastSubmission; }

    private:
        struct PendingBuffer {
            BufferHandle destination{};
            BufferHandle staging{};
            uint64_t size = 0;
            RHIBufferUsage usage = RHIBufferUsage::None;
        };

        RHIDevice& m_Device;
        std::vector<PendingBuffer> m_Pending;

        // 仅本次录制的请求前缀属于此次提交。
        size_t m_RecordedCount = 0;
        bool m_Recorded = false;
        RHICompletionPoint m_LastSubmission{};
    };

}