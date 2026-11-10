#include "Graphs/GraphCanvas.h"
#include "Panels/EditorText.h"
#include "imgui_node_editor.h"
#include "imgui.h"
#include "Core/PlatformServices.h"

namespace Catalpa::Editor {
    namespace ed = ax::NodeEditor;
    std::string GraphTerm(std::string_view symbol) {
        std::string key("graph.term.");
        for (const char c : symbol)
            key += c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
        const std::string fallback(symbol);
        return UiText(key.c_str(), fallback.c_str());
    }
    StableId CanvasIdentity(StableId owner, std::string_view role) {
        const auto text=owner.ToString()+"/"+std::string(role);
        std::array<std::byte,32> hash;
        std::string error;
        if(!ComputeSha256(std::as_bytes(std::span(text.data(),text.size())),hash,error)) throw std::runtime_error(error);
        StableId id;
        for(size_t i=0;i<id.bytes.size();++i) id.bytes[i]=std::to_integer<uint8_t>(hash[i]);
        return id;
    }
    GraphCanvas::~GraphCanvas() { Reset(); }
    void GraphCanvas::Reset() {
        if (m_Context) ed::DestroyEditor(static_cast<ed::EditorContext*>(m_Context));
        m_Context = nullptr; m_Document = {}; m_Local.clear(); m_Stable.clear(); m_Next = 1;
        m_Focus = {};
        m_CancelDrag = false;
    }
    uintptr_t GraphCanvas::Local(StableId id) {
        if (auto found = m_Local.find(id); found != m_Local.end()) return found->second;
        if (m_Next == std::numeric_limits<uintptr_t>::max()) throw std::overflow_error("Canvas identity space exhausted");
        const auto local = m_Next++;
        m_Local.emplace(id, local); m_Stable.emplace(local, id);
        return local;
    }
    StableId GraphCanvas::Stable(uintptr_t id) const {
        const auto found = m_Stable.find(id);
        return found == m_Stable.end() ? StableId{} : found->second;
    }
    CanvasChanges GraphCanvas::Draw(StableId document, std::span<const CanvasNode> nodes,
        std::span<const CanvasLink> links, const ConnectionCheck& check, std::string& error) {
        CanvasChanges changes;
        // 长会话删除过的 UI ID 不参与作者身份；超过有界工作集后重建画布上下文。
        if (m_Document != document || m_Local.size() > 32768) {
            Reset(); m_Document = document;
            ed::Config config; config.SettingsFile = nullptr;
            m_Context = ed::CreateEditor(&config);
        }
        const auto previous = ed::GetCurrentEditor();
        ed::SetCurrentEditor(static_cast<ed::EditorContext*>(m_Context));
        struct Restore { ed::EditorContext* previous; ~Restore() { ed::SetCurrentEditor(previous); } } restore{previous};
        std::map<StableId, CanvasPin> pins;
        for (const auto& node : nodes) {
            const bool fresh = !m_Local.contains(node.id);
            const ed::NodeId local(Local(node.id));
            if (fresh || (!ImGui::IsMouseDown(0) && !ImGui::IsMouseReleased(0))) ed::SetNodePosition(local, {node.x, node.y});
            for (const auto& pin : node.pins) pins.emplace(pin.id, pin);
        }
        const auto size = ImVec2(std::max(ImGui::GetContentRegionAvail().x, 120.f), 360.f);
        ed::Begin("GraphCanvas", size);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && (ImGui::IsMouseDown(0) || ImGui::IsMouseReleased(0)))
            m_CancelDrag = true;
        for (const auto& node : nodes) {
            ed::BeginNode(ed::NodeId(Local(node.id)));
            ImGui::TextUnformatted(node.title.c_str());
            for (const auto& pin : node.pins) {
                ed::BeginPin(ed::PinId(Local(pin.id)), pin.input ? ed::PinKind::Input : ed::PinKind::Output);
                ImGui::Text("%s %s", pin.input ? ">" : "<", pin.label.c_str());
                ed::EndPin();
            }
            ed::EndNode();
        }
        for (const auto& link : links) ed::Link(ed::LinkId(Local(link.id)), ed::PinId(Local(link.from)), ed::PinId(Local(link.to)));
        if (ed::BeginCreate()) {
            ed::PinId first, second;
            if (ed::QueryNewLink(&first, &second) && first && second) {
                const auto a = pins.find(Stable(first.Get())), b = pins.find(Stable(second.Get()));
                if (a != pins.end() && b != pins.end()) {
                    auto from = a->second, to = b->second;
                    if (from.input) std::swap(from, to);
                    std::string diagnostic;
                    if (!m_CancelDrag && !from.input && to.input && (!check || check(from, to, diagnostic))) {
                        if (ed::AcceptNewItem()) changes.connection = std::make_pair(from, to);
                    } else {
                        ed::RejectNewItem();
                        if (!diagnostic.empty()) error = std::move(diagnostic);
                    }
                }
            }
            ed::EndCreate();
        }
        if (ed::BeginDelete()) {
            ed::LinkId link;
            while (ed::QueryDeletedLink(&link)) if (ed::AcceptDeletedItem()) changes.removeLinks.push_back(Stable(link.Get()));
            ed::NodeId node;
            while (ed::QueryDeletedNode(&node)) if (ed::AcceptDeletedItem()) changes.removeNodes.push_back(Stable(node.Get()));
            ed::EndDelete();
        }
        ed::NodeId selected;
        if (ed::GetSelectedNodes(&selected, 1) > 0) changes.selected = Stable(selected.Get());
        if (ImGui::IsMouseReleased(0)) {
            for (const auto& node : nodes) {
                const ed::NodeId local(Local(node.id));
                if (m_CancelDrag) {
                    // Escape 后释放只恢复文档中的位置，不把已取消的拖动记入撤销栈。
                    ed::SetNodePosition(local, {node.x, node.y});
                    continue;
                }
                const auto position = ed::GetNodePosition(local);
                if (std::abs(position.x - node.x) > 0.01f || std::abs(position.y - node.y) > 0.01f)
                    changes.positions.emplace(node.id, std::pair{position.x, position.y});
            }
        }
        ed::End();
        if (m_Focus.HasValue()) {
            if (std::any_of(nodes.begin(), nodes.end(), [&](const auto& node) { return node.id == m_Focus; })) {
                ed::SelectNode(ed::NodeId(Local(m_Focus)));
                ed::NavigateToSelection();
                changes.selected = m_Focus;
            }
            m_Focus = {};
        }
        if (!ImGui::IsMouseDown(0)) m_CancelDrag = false;
        return changes;
    }
}
