#pragma once

#include "catpch.h"

#include "Camera/CameraLens.h"
#include "Camera/CameraMatrices.h"
#include "Core/Math/TransformMath.h"

namespace Catalpa {

    // 渲染侧稳定实例身份，仅为值；不引入 ECS/World 的类型或存储布局。
    struct RenderInstanceId {
        uint64_t scene = 0;
        uint32_t index = 0;
        uint32_t generation = 0;
        [[nodiscard]] constexpr bool HasValue() const noexcept { return scene != 0 && generation != 0 && index != std::numeric_limits<uint32_t>::max(); }
        constexpr auto operator<=>(const RenderInstanceId&) const = default;
    };

    // 每个逻辑视图独占相机和像素尺寸；目标代次只标记重建，不持有GPU资源。
    struct RenderView {
        uint64_t id = 0;
        RenderInstanceId camera;
        uint64_t historyRevision = 0;
        CameraLens lens;
        CameraViewport viewport;
        CameraMatrices matrices;
        uint64_t targetGeneration = 1;
        // 独立视图由宿主提供刚性姿态，没有对应的 World 实体。
        bool independentCamera = false;
    };

    // 检查值快照的身份、镜头、刚性姿态、最终float矩阵组合和逆，不查询World。
    [[nodiscard]] CATALPA_API Math::MathStatus ValidateRenderView(const RenderView& view) noexcept;

    // 上一次成功提交的实际VP；首次、切镜头/尺寸/目标重建时明确无历史。
    struct RenderViewHistoryState {
        bool valid = false;
        uint64_t previousSequence = 0;
        Math::Mat4 previousViewProjection{ 1.0f };
    };

    // 准备阶段自有相机值与派生历史，不向输入RenderFramePacket回写历史。
    struct PreparedRenderView {
        RenderView view;
        RenderViewHistoryState history;
    };

    // 主线程提交事务：Stage只准备；仅真实队列接受后OnSubmitted推进历史。
    // acquire/录制/提交失败均OnCancelled，不把未绘制的CPU帧当成前帧。
    class CATALPA_API RenderViewHistory {
    public:
        [[nodiscard]] Math::MathStatus Stage(uint64_t sequence, uint64_t sceneIdentity, std::span<const RenderView> views);
        [[nodiscard]] const std::vector<PreparedRenderView>& GetPending() const noexcept { return m_Pending; }
        void OnSubmitted() noexcept;
        void OnCancelled() noexcept;
        void Clear() noexcept;

    private:
        std::vector<PreparedRenderView> m_Committed;
        std::vector<PreparedRenderView> m_Pending;
        uint64_t m_CommittedSequence = 0;
        uint64_t m_CommittedScene = 0;
        uint64_t m_PendingSequence = 0;
        uint64_t m_PendingScene = 0;
        bool m_HasPending = false;
    };

}
