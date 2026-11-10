#include "TestRunner.h"
#include "Application/WorkspaceEditing.h"
#include "Commands/SceneCommands.h"
#include "Core/Application.h"
#include "imgui.h"
#include <chrono>
#include <set>

namespace Catalpa::Tests {
    namespace {
        constexpr uint64_t SceneSurface = 0x43415453434e4501ULL;
        constexpr unsigned Columns = 13, Rows = 9;

        class GroveGpuTest final : public Layer {
          public:
            GroveGpuTest() : Layer("GroveGpuTest") {}
            void OnAttach() override {
                auto& app = Application::Get();
                auto& scenes = app.GetScenes();
                auto status = scenes.CreateScene("Unpublished return target", m_Previous);
                if (status.IsSuccess()) status = scenes.SetActive(m_Previous);
                if (!status.IsSuccess()) { Finish(false, status.message); return; }
                m_Workspace = std::make_unique<Editor::WorkspaceEditing>(app);
                m_Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
            }
            void OnDetach() override {
                m_Workspace.reset();
            }
            void OnEvent(Event& event) override {
                if (!m_Finished && event.GetEventType() == EventType::WindowClose)
                    Finish(false, "Window closed before Grove acceptance completed");
            }
            void OnUpdate(Deltatime) override {
                if (!m_Finished && std::chrono::steady_clock::now() > m_Deadline)
                    Finish(false, "Timed out waiting for the Grove author scene, published surface or GPU readbacks");
            }
            void OnImGuiRender() override {
                if (m_Finished || !m_Workspace) return;
                const auto dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
                m_Workspace->Draw([](const char*, const char* fallback, const char* id) {
                    return id ? std::string(fallback) + "###" + id : std::string(fallback);
                }, nullptr, dock);

                auto* doc = dynamic_cast<Editor::SceneDocument*>(m_Workspace->CurrentDocument());
                if (!doc) return;
                if (!m_OriginalId.HasValue()) {
                    std::string error;
                    if (!Editor::CreateAuthorEntity(*doc, "Unsaved Grove return marker", {}, m_Marker, error)) {
                        Finish(false, error);
                        return;
                    }
                    m_OriginalId = doc->Id();
                    m_OriginalPosition = doc->History().Position();
                    m_Workspace->OpenGroveShowcase();
                    if (!m_Workspace->CanReturnFromGrove())
                        Finish(false, "The Grove menu action could not open the deployed scene/catalog");
                    return;
                }
                if (m_Returning) {
                    const auto* original = doc->Resolve();
                    const bool restored = !m_Workspace->CanReturnFromGrove() && doc->Handle() == m_Previous &&
                        doc->Id() == m_OriginalId && doc->History().Dirty() &&
                        doc->History().Position() == m_OriginalPosition && original &&
                        original->Resolve(m_Marker).HasValue();
                    Finish(restored, restored ? "GPU picked tree, paving and architecture; selected outline frame preserved picking identity; previous unsaved document, entity and undo position restored"
                                              : "Returning from Grove changed the previous unsaved document or its undo history");
                    return;
                }
                if (!m_Workspace->CanReturnFromGrove()) {
                    Finish(false, "Grove author loading was cancelled before its presentation");
                    return;
                }
                if (doc->Handle() == m_Previous || !doc->Resolve()) return;
                auto& app = Application::Get();
                RenderSurfaceSnapshot frame;
                if (!app.GetEditorPreview(SceneSurface, frame) || !frame.textureToken ||
                    frame.sceneIdentity != doc->Resolve()->GetWorld().GetIdentity()) return;
                SampleViewport(app, *doc, frame);
            }

          private:
            // 同一作者相机下逐行采样实际 ID 附件，异步票据必须在它所属的已发布画面上消费。
            void SampleViewport(Application& app, Editor::SceneDocument& doc, const RenderSurfaceSnapshot& frame) {
                // 轮廓使用整数 ID 的最高位。要求在已提交的选中帧再次命中同一个实体。
                if (m_Selected.HasValue() && frame.view.editorDisplay.selected == m_Selected && !m_CheckedSelection) {
                    if (!m_SelectedTicket) {
                        PickingRequest request{Columns * Rows + 1, frame.surfaceId, frame.sceneIdentity,
                            frame.frameSequence, frame.targetGeneration, m_SelectedX, m_SelectedY};
                        std::string error;
                        if (!app.RequestEditorPick(request, m_SelectedTicket, error)) { Finish(false, error); return; }
                    } else {
                        PickingResult result;
                        if (app.PollEditorPick(m_SelectedTicket, result)) {
                            if (result.status != PickingResultStatus::Ready || result.hit != m_Selected) {
                                Finish(false, "Outline frame changed the selected GPU picking identity");
                                return;
                            }
                            m_CheckedSelection = true;
                        }
                    }
                }
                if (m_Row == Rows) {
                    if (m_CheckedSelection) {
                        m_Workspace->ReturnFromGrove();
                        m_Returning = true;
                    }
                    return;
                }
                if (m_NewRow) {
                    for (unsigned col = 0; col < Columns; ++col) {
                        const uint32_t x = (2 * col + 1) * frame.extent.width / (2 * Columns);
                        const uint32_t y = (2 * m_Row + 1) * frame.extent.height / (2 * Rows);
                        const PickingRequest request{1u + m_Row * Columns + col, frame.surfaceId,
                            frame.sceneIdentity, frame.frameSequence, frame.targetGeneration, x, y};
                        std::string error;
                        if (!app.RequestEditorPick(request, m_Tickets[col], error)) { Finish(false, error); return; }
                    }
                    m_NewRow = false;
                    return;
                }
                for (auto& ticket : m_Tickets) {
                    if (!ticket) continue;
                    PickingResult result;
                    if (!app.PollEditorPick(ticket, result)) continue;
                    if (result.status != PickingResultStatus::Ready || result.ticket != ticket) {
                        Finish(false, "Grove picking attachment did not produce its requested pixel");
                        return;
                    }
                    ticket = 0;
                    if (result.hit.HasValue()) {
                        IdentityComponent identity;
                        const EntityId entity{result.hit.scene, result.hit.index, result.hit.generation};
                        if (!doc.Resolve()->GetIdentity(entity, identity).IsSuccess()) { Finish(false, "Unresolvable Grove GPU identity"); return; }
                        m_Hits.insert(identity.name);
                        if (!m_Selected.HasValue()) {
                            m_Selected = result.hit;
                            m_SelectedX = result.request.x;
                            m_SelectedY = result.request.y;
                            std::string error;
                            if (!m_Workspace->PickViewport(doc, frame, m_SelectedX, m_SelectedY, error)) {
                                Finish(false, error);
                                return;
                            }
                        }
                    }
                }
                if (std::any_of(m_Tickets.begin(), m_Tickets.end(), [](auto ticket) { return ticket != 0; })) return;
                m_NewRow = true;
                if (++m_Row < Rows) return;
                for (const auto& name : m_Hits) CAT_INFO("[Grove GPU hit] {0}", name);
                const bool leaves = m_Hits.contains("Leaf surface") || m_Hits.contains("Young leaves") || m_Hits.contains("Deep foliage");
                const bool paving = m_Hits.contains("Limestone ivory") || m_Hits.contains("Limestone sand") || m_Hits.contains("Limestone warm grey");
                const bool building = m_Hits.contains("Warm lime plaster") || m_Hits.contains("Bronze window frames") || m_Hits.contains("Blue green glazing");
                if (!leaves || !paving || !building) { Finish(false, "Visible foliage, courtyard paving and architecture are required"); return; }
            }
            void Finish(bool passed, const std::string& detail) {
                m_Finished = true;
                if (passed) CAT_INFO("[Grove GPU PASS] {0}", detail);
                else CAT_ERROR("[Grove GPU FAIL] {0}", detail);
                Application::Get().RequestClose(passed ? 0 : 1);
            }
            std::unique_ptr<Editor::WorkspaceEditing> m_Workspace;
            std::chrono::steady_clock::time_point m_Deadline{};
            SceneHandle m_Previous;
            StableId m_OriginalId;
            PersistentEntityId m_Marker;
            size_t m_OriginalPosition = 0;
            std::array<uint64_t, Columns> m_Tickets{};
            RenderInstanceId m_Selected;
            uint32_t m_SelectedX = 0, m_SelectedY = 0;
            uint64_t m_SelectedTicket = 0;
            bool m_CheckedSelection = false;
            std::set<std::string> m_Hits;
            unsigned m_Row = 0;
            bool m_NewRow = true, m_Returning = false, m_Finished = false;
        };
    }

    std::unique_ptr<Layer> CreateGroveGpuTest() { return std::make_unique<GroveGpuTest>(); }
}
