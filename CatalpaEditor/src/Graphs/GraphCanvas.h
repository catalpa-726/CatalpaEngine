#pragma once
#include "Core/Id/StableId.h"

namespace Catalpa::Editor {
    StableId CanvasIdentity(StableId owner, std::string_view role);
    struct CanvasPin { StableId id, node; std::string label; bool input = false; };
    struct CanvasNode { StableId id; std::string title; std::vector<CanvasPin> pins; float x = 0, y = 0; };
    struct CanvasLink { StableId id, from, to; };
    struct CanvasChanges {
        std::optional<std::pair<CanvasPin, CanvasPin>> connection;
        std::vector<StableId> removeNodes, removeLinks;
        std::map<StableId, std::pair<float, float>> positions;
        StableId selected;
    };
    class GraphCanvas {
      public:
        GraphCanvas() = default;
        ~GraphCanvas();
        GraphCanvas(const GraphCanvas&) = delete;
        GraphCanvas& operator=(const GraphCanvas&) = delete;
        using ConnectionCheck = std::function<bool(const CanvasPin&, const CanvasPin&, std::string&)>;
        CanvasChanges Draw(StableId document, std::span<const CanvasNode>, std::span<const CanvasLink>,
                           const ConnectionCheck&, std::string& error);
        void Reset();
        // 下次绘制时定位稳定节点，仅改变画布选择/相机，不产生作者命令。
        void Focus(StableId node) noexcept { m_Focus = node; }
      private:
        uintptr_t Local(StableId);
        StableId Stable(uintptr_t) const;
        void* m_Context = nullptr;
        StableId m_Document;
        StableId m_Focus;
        bool m_CancelDrag = false;
        std::map<StableId, uintptr_t> m_Local;
        std::map<uintptr_t, StableId> m_Stable;
        uintptr_t m_Next = 1;
    };
}
