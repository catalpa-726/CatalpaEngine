#include "catpch.h"
#include "UploadScheduler.h"
#include "Log/Log.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHICommandList.h"

namespace Catalpa {

    UploadScheduler::~UploadScheduler() { Shutdown(); }

    RHIStatus UploadScheduler::CreateBuffer(std::span<const std::byte> data, RHIBufferUsage usage, BufferHandle& buffer)
    {
        constexpr const char* operation = "UploadScheduler::CreateBuffer";
        buffer = {};

        if (data.empty() || (usage != RHIBufferUsage::Vertex && usage != RHIBufferUsage::Index &&
            usage != (RHIBufferUsage::Vertex | RHIBufferUsage::Index)))
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        if (m_Pending.size() == m_Pending.max_size())
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };

        m_Pending.reserve(m_Pending.size() + 1);
        const uint64_t size = static_cast<uint64_t>(data.size());

        const RHIBufferDesc destinationDesc{
            .size = size,
            .usage = usage | RHIBufferUsage::TransferDestination,
            .memory = RHIMemoryDomain::Device
        };

        const RHIBufferDesc stagingDesc{
            .size = size,
            .usage = RHIBufferUsage::TransferSource,
            .memory = RHIMemoryDomain::Upload
        };

        BufferHandle destination{}, staging{};
        RHIStatus status = m_Device.CreateBuffer(destinationDesc, destination);
        if (!status.IsSuccess()) return status;

        status = m_Device.CreateBuffer(stagingDesc, staging);
        if (status.IsSuccess()) status = m_Device.WriteBuffer(staging, 0, data);

        if (!status.IsSuccess()) {
            m_Device.ReleaseBuffer(staging);
            m_Device.ReleaseBuffer(destination);
            return status;
        }

        m_Pending.push_back({ .destination = destination, .staging = staging, .size = size, .usage = usage });
        buffer = destination;
        return {};
    }

    RHIStatus UploadScheduler::RecordPending(RHICommandList& commands)
    {
        if (m_Recorded || commands.GetState() != RHICommandListState::Recording)
            return { .code = RHIErrorCode::InvalidState, .operation = "UploadScheduler::RecordPending" };

        m_Recorded = true;
        m_RecordedCount = m_Pending.size();

        if (m_RecordedCount == 0) return {};

        RHICommandLabelScope uploadLabel(commands, "Static Buffer Uploads", { 0.2f, 0.8f, 0.3f, 1.0f });

        for (size_t i = 0; i < m_RecordedCount; ++i) {
            const auto& pending = m_Pending[i];
            const RHIBufferCopy copy{ .size = pending.size };

            if (const RHIStatus status = commands.CopyBuffer(pending.staging, pending.destination, copy); !status.IsSuccess()) return status;

            const bool vertex = HasBufferUsage(pending.usage, RHIBufferUsage::Vertex);
            const bool index = HasBufferUsage(pending.usage, RHIBufferUsage::Index);
            const RHIResourceAccess access = vertex && index ? RHIResourceAccess::VertexIndexRead :
                vertex ? RHIResourceAccess::VertexRead : RHIResourceAccess::IndexRead;

            const RHIBufferBarrier barrier{
                .buffer = pending.destination,
                .size = pending.size,
                .before = {.stage = RHIPipelineStage::Transfer, .access = RHIResourceAccess::TransferWrite },
                .after = {.stage = RHIPipelineStage::VertexInput, .access = access }
            };

            if (const RHIStatus status = commands.PipelineBarrier(std::span<const RHIBufferBarrier>{ &barrier, 1 }); !status.IsSuccess()) return status;
        }

        return {};
    }

    void UploadScheduler::OnSubmitted(const RHICompletionPoint& completion) noexcept
    {
        CAT_CORE_ASSERT(m_Recorded && completion.HasValue() && completion.device == m_Device.GetDeviceId(),
            "Upload submission requires the completion point of its accepted drawing submission!");

        if (m_RecordedCount != 0) {
            m_LastSubmission = completion;

            // 这里只释放所有者引用；命令引用保留至安全 reset。
            for (size_t i = 0; i < m_RecordedCount; ++i) m_Device.ReleaseBuffer(m_Pending[i].staging);

            m_Pending.erase(m_Pending.begin(), m_Pending.begin() + static_cast<std::ptrdiff_t>(m_RecordedCount));
        }

        m_RecordedCount = 0;
        m_Recorded = false;
    }

    void UploadScheduler::OnCancelled() noexcept
    {
        m_RecordedCount = 0;
        m_Recorded = false;
    }

    void UploadScheduler::Shutdown() noexcept
    {
        for (const auto& pending : m_Pending) m_Device.ReleaseBuffer(pending.staging);

        m_Pending.clear();
        m_RecordedCount = 0;
        m_Recorded = false;
        m_LastSubmission = {};
    }

}