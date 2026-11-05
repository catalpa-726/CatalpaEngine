#pragma once

#include "Core/Base.h"
#include "RHI/RHIHandle.h"
#include "RHI/RHIBufferDesc.h"
#include "RHI/RHICompletionPoint.h"
#include "RHI/RHIError.h"
#include "RHI/RHITextureDesc.h"
#include "RHI/RHICommandTypes.h"

// 声明静态 Buffer 和颜色纹理上传调度器
namespace Catalpa {

    class RHIDevice;
    class RenderGraph;

    // 当前由主线程调用，负责新建静态顶点/索引 Buffer 和整幅颜色纹理的上传。
    // 导出统一上传服务，供DLL外的GPU缓存契约测试复用同一实现与关闭路径。
    class CATALPA_API UploadScheduler {
    public:
        explicit UploadScheduler(RHIDevice& device) : m_Device(device) {}
        ~UploadScheduler();

        UploadScheduler(const UploadScheduler&) = delete;
        UploadScheduler& operator=(const UploadScheduler&) = delete;

        // 立即复制 CPU 数据；目标所有权交给调用方，并须保留至请求提交或调度器关闭。
        [[nodiscard]] RHIStatus CreateBuffer(std::span<const std::byte> data, RHIBufferUsage usage, BufferHandle& buffer);

        // 立即复制 CPU 数据；自动补充 TransferDestination。
        // 纹理和 View 的所有权交给调用方，须保留至请求提交或调度器关闭。
        [[nodiscard]] RHIStatus CreateTexture(std::span<const std::byte> data, const RHITextureDesc& desc, TextureHandle& texture, TextureViewHandle& view);
        // 多mip/层逐项描述紧密载荷；偏移指向同一CPU副本，调度器不重排源字节。
        [[nodiscard]] RHIStatus CreateTexture(std::span<const std::byte> data, const RHITextureDesc& desc,
            std::span<const RHIBufferTextureCopy> copies, TextureHandle& texture, TextureViewHandle& view);

        // 冻结本次请求前缀并加入图，复制和屏障由图统一生成。
        [[nodiscard]] RHIStatus AddPasses(RenderGraph& graph);

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

        // 保存尚未提交的纹理上传请求；staging owner 由调度器持有。
        struct PendingTexture {
            TextureViewHandle destination{};
            BufferHandle staging{};
            std::vector<RHIBufferTextureCopy> copies;
        };

        RHIDevice& m_Device;
        std::vector<PendingBuffer> m_Pending;
        std::vector<PendingTexture> m_PendingTextures;

        // 仅加入本帧图的请求前缀属于此次提交。
        size_t m_RecordedCount = 0;
        size_t m_RecordedTextureCount = 0;
        bool m_Recorded = false;
        RHICompletionPoint m_LastSubmission{};
    };

}
