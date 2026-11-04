#pragma once

#include "Core/Base.h"
#include "Core/Math/TransformMath.h"
#include "Renderer/RenderFramePacket.h"

namespace Catalpa {

    // 渲染方拥有的值快照，不是可独立编辑的第二个 World 或 GPU 资源所有者。
    class CATALPA_API RenderScene {
    public:
        // sequence 全局严格递增，即使切换来源场景也不接受旧帧覆盖新帧。
        [[nodiscard]] Math::MathStatus Apply(RenderFramePacket packet);
        [[nodiscard]] const RenderFramePacket& GetFrame() const noexcept { return m_Frame; }
        // 停止消费者后清快照；生命周期结束时才允许重置序号。
        void Clear() noexcept;

    private:
        RenderFramePacket m_Frame;
    };

}
