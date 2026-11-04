#include "catpch.h"
#include "UploadScheduler.h"
#include "Log/Log.h"
#include "RHI/RHIDevice.h"
#include "Renderer/RenderGraph/RenderGraph.h"

namespace Catalpa {

    UploadScheduler::~UploadScheduler() { Shutdown(); }

    RHIStatus UploadScheduler::CreateBuffer(std::span<const std::byte> data, RHIBufferUsage usage, BufferHandle& buffer)
    {
        constexpr const char* operation = "UploadScheduler::CreateBuffer";
        buffer = {};
        if (data.empty() || (usage != RHIBufferUsage::Vertex && usage != RHIBufferUsage::Index &&
            usage != (RHIBufferUsage::Vertex | RHIBufferUsage::Index)))
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        BufferHandle destination{}, staging{};
        const auto discard = [&] {
            m_Device.ReleaseBuffer(std::exchange(staging, BufferHandle{}));
            m_Device.ReleaseBuffer(std::exchange(destination, BufferHandle{}));
        };
        try {
            if (m_Pending.size() == m_Pending.max_size())
                return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
            // 所有容器准备先于原生创建；候选句柄跨try保留，以收束后续创建/写入的分配异常。
            m_Pending.reserve(m_Pending.size() + 1);
            const uint64_t size = static_cast<uint64_t>(data.size());
            const RHIBufferDesc destinationDesc{
                .size = size, .usage = usage | RHIBufferUsage::TransferDestination, .memory = RHIMemoryDomain::Device
            };
            const RHIBufferDesc stagingDesc{
                .size = size, .usage = RHIBufferUsage::TransferSource, .memory = RHIMemoryDomain::Upload
            };
            RHIStatus status = m_Device.CreateBuffer(destinationDesc, destination);
            if (status.IsSuccess()) status = m_Device.CreateBuffer(stagingDesc, staging);
            if (status.IsSuccess()) status = m_Device.WriteBuffer(staging, 0, data);
            if (!status.IsSuccess()) { discard(); return status; }

            m_Pending.push_back({ .destination = destination, .staging = staging, .size = size, .usage = usage });
            // Pending持有staging owner，调用方接收destination owner；发布后不再让catch看见这些候选。
            buffer = std::exchange(destination, BufferHandle{});
            staging = {};
            return {};
        } catch (const std::bad_alloc&) {
            discard();
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
        }
    }

    RHIStatus UploadScheduler::CreateTexture(std::span<const std::byte> data, const RHITextureDesc& desc, TextureHandle& texture, TextureViewHandle& view)
    {
        constexpr const char* operation = "UploadScheduler::CreateTexture";
        texture = {};
        view = {};
        uint64_t bytes = 0;
        if (!TryGetTextureUploadByteSize(desc.extent, desc.format, bytes))
            return { .code = RHIErrorCode::Unsupported, .operation = operation };
        if (static_cast<uint64_t>(data.size()) != bytes)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        TextureHandle destination{};
        TextureViewHandle destinationView{};
        BufferHandle staging{};
        const auto discard = [&] {
            m_Device.ReleaseBuffer(std::exchange(staging, BufferHandle{}));
            m_Device.ReleaseTextureView(std::exchange(destinationView, TextureViewHandle{}));
            m_Device.ReleaseTexture(std::exchange(destination, TextureHandle{}));
        };
        try {
            if (m_PendingTextures.size() == m_PendingTextures.max_size())
                return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
            m_PendingTextures.reserve(m_PendingTextures.size() + 1);
            RHITextureDesc destinationDesc = desc;
            destinationDesc.usage = destinationDesc.usage | RHITextureUsage::TransferDestination;
            const RHIBufferDesc stagingDesc{
                .size = bytes, .usage = RHIBufferUsage::TransferSource, .memory = RHIMemoryDomain::Upload
            };
            RHIStatus status = m_Device.CreateTexture(destinationDesc, destination);
            if (status.IsSuccess()) status = m_Device.CreateTextureView(destination, RHITextureViewDesc{}, destinationView);
            if (status.IsSuccess()) status = m_Device.CreateBuffer(stagingDesc, staging);
            if (status.IsSuccess()) status = m_Device.WriteBuffer(staging, 0, data);
            if (!status.IsSuccess()) { discard(); return status; }

            m_PendingTextures.push_back({ .destination = destinationView, .staging = staging });
            texture = std::exchange(destination, TextureHandle{});
            view = std::exchange(destinationView, TextureViewHandle{});
            staging = {};
            return {};
        } catch (const std::bad_alloc&) {
            // View先于图像归还，避免候选图像仍有View引用时提前销毁；Pending协议保持原样。
            discard();
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
        }
    }

    RHIStatus UploadScheduler::AddPasses(RenderGraph& graph)
    {
        if (m_Recorded)
            return { .code = RHIErrorCode::InvalidState, .operation = "UploadScheduler::AddPasses" };

        m_Recorded = true;
        m_RecordedCount = m_Pending.size();
        m_RecordedTextureCount = m_PendingTextures.size();

        for (size_t i = 0; i < m_RecordedCount; ++i) {
            const PendingBuffer pending = m_Pending[i];
            RGBuffer source{}, destination{};

            if (const RHIStatus status = graph.ImportBuffer(pending.staging, source); !status.IsSuccess()) return status;
            if (const RHIStatus status = graph.ImportBuffer(pending.destination, destination); !status.IsSuccess()) return status;

            const RHIBufferCopy copy{ .size = pending.size };

            if (const RHIStatus status = graph.AddCopyPass("Static Buffer Upload", source, destination, copy); !status.IsSuccess())
                return status;
        }

        for (size_t i = 0; i < m_RecordedTextureCount; ++i) {
            const PendingTexture pending = m_PendingTextures[i];
            RGBuffer source{};
            RGTexture destination{};

            if (const RHIStatus status = graph.ImportBuffer(pending.staging, source); !status.IsSuccess()) return status;
            if (const RHIStatus status = graph.ImportTexture(pending.destination, destination); !status.IsSuccess()) return status;

            if (const RHIStatus status = graph.AddTextureUploadPass("Static Texture Upload", source, destination, RHIBufferTextureCopy{}); !status.IsSuccess())
                return status;
        }

        return {};
    }

    void UploadScheduler::OnSubmitted(const RHICompletionPoint& completion) noexcept
    {
        CAT_CORE_ASSERT(m_Recorded && completion.HasValue() && completion.device == m_Device.GetDeviceId(),
            "Upload submission requires the completion point of its accepted drawing submission!");

        if (m_RecordedCount != 0 || m_RecordedTextureCount != 0) m_LastSubmission = completion;

        // 只释放所有者引用；已提交命令继续保留 staging 至安全 reset。
        for (size_t i = 0; i < m_RecordedCount; ++i) m_Device.ReleaseBuffer(m_Pending[i].staging);
        for (size_t i = 0; i < m_RecordedTextureCount; ++i) m_Device.ReleaseBuffer(m_PendingTextures[i].staging);

        m_Pending.erase(m_Pending.begin(), m_Pending.begin() + static_cast<std::ptrdiff_t>(m_RecordedCount));
        m_PendingTextures.erase(m_PendingTextures.begin(),
            m_PendingTextures.begin() + static_cast<std::ptrdiff_t>(m_RecordedTextureCount));

        m_RecordedCount = 0;
        m_RecordedTextureCount = 0;
        m_Recorded = false;
    }

    void UploadScheduler::OnCancelled() noexcept
    {
        m_RecordedCount = 0;
        m_RecordedTextureCount = 0;
        m_Recorded = false;
    }

    void UploadScheduler::Shutdown() noexcept
    {
        for (const auto& pending : m_Pending) m_Device.ReleaseBuffer(pending.staging);
        for (const auto& pending : m_PendingTextures) m_Device.ReleaseBuffer(pending.staging);

        m_Pending.clear();
        m_PendingTextures.clear();
        m_RecordedCount = 0;
        m_RecordedTextureCount = 0;
        m_Recorded = false;
        m_LastSubmission = {};
    }

}