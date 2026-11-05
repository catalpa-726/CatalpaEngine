#include "catpch.h"
#include "Renderer/Readback/ReadbackManager.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "RHI/RHIDevice.h"

namespace Catalpa {

    ReadbackManager::~ReadbackManager()
    {
        for (auto& [ticket, pending] : m_Pending) m_Device.ReleaseBuffer(pending.destination);
    }

    RHIStatus ReadbackManager::Request(const PickingRequest& request, TextureViewHandle source,
        std::shared_ptr<void> keepAlive, std::span<const RenderInstanceId> identities, uint64_t& ticket)
    {
        constexpr const char* operation = "ReadbackManager::Request";
        if (!source.HasValue() || !keepAlive || request.requestId == 0 || request.surfaceId == 0
            || m_NextTicket == UINT64_MAX || m_Pending.size() >= 32)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };

        try {
            Pending pending;
            pending.request = request;
            pending.source = source;
            pending.keepAlive = std::move(keepAlive);
            pending.identities.assign(identities.begin(), identities.end());
            m_Pending.emplace(m_NextTicket, std::move(pending));
            ticket = m_NextTicket++;
            return {};
        } catch (const std::bad_alloc&) {
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
        }
    }

    RHIStatus ReadbackManager::AddPasses(RenderGraph& graph)
    {
        for (auto& [ticket, pending] : m_Pending) {
            if (pending.completion.HasValue() || pending.recorded) continue;
            if (!pending.destination.HasValue()) {
                const RHIBufferDesc desc{ .size = sizeof(uint32_t), .usage = RHIBufferUsage::TransferDestination,
                    .memory = RHIMemoryDomain::Readback };
                if (const auto status = m_Device.CreateBuffer(desc, pending.destination); !status.IsSuccess()) return status;
            }
            RGTexture source;
            RGBuffer destination;
            if (const auto status = graph.ImportTexture(pending.source, source); !status.IsSuccess()) return status;
            if (const auto status = graph.ImportBuffer(pending.destination, destination); !status.IsSuccess()) return status;
            const RHITextureBufferCopy copy{ .textureOffset = { static_cast<int32_t>(pending.request.x),
                static_cast<int32_t>(pending.request.y) }, .extent = { 1, 1 } };
            if (const auto status = graph.AddTextureReadbackPass("Picking pixel readback", source, destination, copy); !status.IsSuccess()) return status;
            pending.recorded = true;
        }
        return {};
    }

    void ReadbackManager::OnSubmitted(RHICompletionPoint completion) noexcept
    {
        for (auto& [ticket, pending] : m_Pending)
            if (pending.recorded) {
                pending.completion = completion;
                pending.recorded = false;
            }
    }

    void ReadbackManager::OnCancelled() noexcept
    {
        for (auto& [ticket, pending] : m_Pending) pending.recorded = false;
    }

    bool ReadbackManager::Poll(uint64_t ticket, PickingResult& output)
    {
        const auto found = m_Pending.find(ticket);
        if (found == m_Pending.end()) return false;
        auto& pending = found->second;
        PickingResult result{ .ticket = ticket, .request = pending.request };
        if (!pending.completion.HasValue()) return false;
        bool complete = false;
        const auto completionStatus = m_Device.GetGraphicsQueue().IsComplete(pending.completion, complete);
        if (completionStatus.IsSuccess() && !complete) return false;
        uint32_t identityIndex = 0;
        if (completionStatus.IsSuccess()
            && m_Device.ReadBuffer(pending.destination, 0, std::as_writable_bytes(std::span{ &identityIndex, 1 }),
                pending.completion).IsSuccess()) {
            result.status = PickingResultStatus::Ready;
            if (identityIndex != 0) {
                if (identityIndex > pending.identities.size()) result.status = PickingResultStatus::Failed;
                else result.hit = pending.identities[identityIndex - 1];
            }
        }
        // 释放逻辑owner即可，已接受的GPU命令仍由后端保活，取消不进行WaitIdle。
        m_Device.ReleaseBuffer(pending.destination);
        m_Pending.erase(found);
        output = result;
        return true;
    }

    void ReadbackManager::Cancel(uint64_t ticket) noexcept
    {
        const auto found = m_Pending.find(ticket);
        if (found == m_Pending.end()) return;
        // 主线程在图录制之外取消；调用者可直接丢弃ticket，不必为释放配额再Poll。
        m_Device.ReleaseBuffer(found->second.destination);
        m_Pending.erase(found);
    }

    void ReadbackManager::CancelSurface(uint64_t surfaceId) noexcept
    {
        for (auto it = m_Pending.begin(); it != m_Pending.end();)
            if (it->second.request.surfaceId == surfaceId) {
                m_Device.ReleaseBuffer(it->second.destination);
                it = m_Pending.erase(it);
            } else ++it;
    }
}
