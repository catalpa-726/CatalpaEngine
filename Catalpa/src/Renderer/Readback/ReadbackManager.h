#pragma once

#include "Renderer/Readback/Picking.h"
#include "RHI/RHICompletionPoint.h"
#include "RHI/RHIError.h"
#include "RHI/RHIHandle.h"

namespace Catalpa {
    class RHIDevice;
    class RenderGraph;

    // 点击只排队1像素读回；每个ticket拥有自己的Readback buffer和显示帧身份表。
    // 保活lease来自Renderer目标owner，Manager不再创立第二个纹理owner。
    class CATALPA_API ReadbackManager {
    public:
        explicit ReadbackManager(RHIDevice& device) : m_Device(device) {}
        ~ReadbackManager();

        [[nodiscard]] RHIStatus Request(const PickingRequest&, TextureViewHandle,
            std::shared_ptr<void> keepAlive, std::span<const RenderInstanceId> identities, uint64_t& ticket);
        [[nodiscard]] RHIStatus AddPasses(RenderGraph&);
        void OnSubmitted(RHICompletionPoint) noexcept;
        void OnCancelled() noexcept;
        [[nodiscard]] bool Poll(uint64_t ticket, PickingResult& output);
        void Cancel(uint64_t ticket) noexcept;
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
