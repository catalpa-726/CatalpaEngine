#include "TestRunner.h"
#include "Assets/AssetTestFixture.h"
#include "Core/Application.h"
#include "Log/LogSubscription.h"
#include "Panels/BrowserThumbnails.h"
#include "imgui.h"
#include <chrono>

namespace Catalpa::Tests {
    namespace {
        class PlatformWindowGpuTest final : public Layer {
          public:
            PlatformWindowGpuTest() : Layer("PlatformWindowGpuTest") {}

            void OnAttach() override {
                Application::Get().SetEditorViewportActive(true);
                m_Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                std::string error;
                if (!AssetTest::CreateThumbnailSources(m_Fixture, error))
                    Finish(false, error);
            }

            void OnDetach() override {
                m_Thumbnails.Reset();
                Application::Get().SetEditorViewportActive(false);
            }

            void OnUpdate(Deltatime) override {
                m_Thumbnails.BeginFrame();
            }

            void OnEvent(Event& event) override {
                if (!m_Finished && event.GetEventType() == EventType::WindowClose)
                    Finish(false, "Closed before native window retirement");
            }

            void OnImGuiRender() override {
                if (m_Finished) return;
                uint64_t dropped = 0;
                for (const auto& record : m_Logs.Drain(256, dropped)) {
                    if (record.message.find("VUID-") != std::string::npos ||
                        record.message.find("SYNC-HAZARD") != std::string::npos) {
                        Finish(false, record.message);
                        return;
                    }
                }
                if (dropped || std::chrono::steady_clock::now() > m_Deadline) {
                    Finish(false, dropped ? "Validation log overflow" : "Timed out awaiting presentation or retirement");
                    return;
                }
                if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
                    Finish(false, "Platform viewports are disabled");
                    return;
                }

                const auto* main = ImGui::GetMainViewport();
                const auto dock = ImGui::DockSpaceOverViewport(0, main);
                PlatformWindowPresentation snapshot;
                const bool live = m_View && Application::Get().GetPlatformWindowPresentation(m_View, snapshot);
                if (m_Stage == Stage::Close) {
                    if (!live && !ImGui::FindViewportByID(m_View))
                        Finish(true, "Native window presented, resized, docked into the main swapchain with the material tile, detached again and closed");
                    return;
                }

                if (m_Stage == Stage::Redock) {
                    ImGui::SetNextWindowDockID(dock);
                } else {
                    ImGuiWindowClass detached{};
                    detached.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge | ImGuiViewportFlags_NoFocusOnAppearing |
                                                         ImGuiViewportFlags_NoFocusOnClick | ImGuiViewportFlags_NoTaskBarIcon;
                    ImGui::SetNextWindowClass(&detached);
                    ImGui::SetNextWindowDockID(0);
                    ImGui::SetNextWindowPos({main->Pos.x + 70, main->Pos.y + 70});
                    ImGui::SetNextWindowSize(m_First.sequence ? ImVec2{340, 180} : ImVec2{220, 120});
                }
                ImGui::Begin("GPU material panel", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
                const auto* view = ImGui::GetWindowViewport();
                const bool docked = ImGui::IsWindowDocked() && ImGui::GetWindowDockID() == dock && view == main;
                ImGui::TextUnformatted("Material thumbnail");
                const auto pos = ImGui::GetCursorScreenPos();
                std::string error;
                const bool tile = m_Thumbnails.Draw(m_Fixture.root, m_Fixture.files[1],
                    pos, {pos.x + 40, pos.y + 40}, error);
                ImGui::Dummy({40, 40});
                RenderSurfaceSnapshot atlas;
                bool drawsTile = false;
                if (tile && Application::Get().GetEditorPreview(Atlas + 1, atlas)) {
                    for (const auto& cmd : ImGui::GetWindowDrawList()->CmdBuffer)
                        if (cmd.ElemCount && cmd.GetTexID() == atlas.textureToken) drawsTile = true;
                }
                ImGui::End();
                if (!error.empty()) {
                    Finish(false, error);
                    return;
                }
                if (m_Stage == Stage::Redock) {
                    PlatformWindowPresentation primary;
                    if (docked && drawsTile && !live && !ImGui::FindViewportByID(m_View) &&
                        Application::Get().GetPlatformWindowPresentation(0, primary)) {
                        if (!m_DockedAt) m_DockedAt = primary.sequence;
                        else if (primary.sequence >= m_DockedAt + 2) m_Stage = Stage::DetachAgain;
                    }
                    return;
                }
                if (view->ID == main->ID || !view->PlatformHandleRaw) return;
                m_View = view->ID;
                if (!live || !drawsTile || snapshot.sequence < 3) return;
                if (m_Stage == Stage::DetachAgain) {
                    m_Stage = Stage::Close;
                    return;
                }
                if (!m_First.sequence) {
                    m_First = snapshot;
                    return;
                }
                // 比较真实像素而非逻辑点，不假定 Windows 显示缩放为 100%。
                if (snapshot.extent.width > m_First.extent.width && snapshot.extent.height > m_First.extent.height &&
                    snapshot.sequence >= m_First.sequence + 3) {
                    m_Last = snapshot;
                    m_Stage = Stage::Redock;
                }
            }

          private:
            void Finish(bool success, const std::string& reason) {
                m_Finished = true;
                if (success) {
                    CAT_INFO("[GPU native window] {}. id={} first={}x{} at present {}, resized={}x{} at present {}",
                        reason, m_View, m_First.extent.width, m_First.extent.height, m_First.sequence,
                        m_Last.extent.width, m_Last.extent.height, m_Last.sequence);
                } else {
                    CAT_ERROR("[GPU native window] {}. id={}, first present={}, docked at={}, stage={}",
                              reason, m_View, m_First.sequence, m_DockedAt, int(m_Stage));
                }
                Application::Get().RequestClose(success ? 0 : 1);
            }

            std::chrono::steady_clock::time_point m_Deadline;
            LogSubscription m_Logs;
            AssetTest::Fixture m_Fixture;
            static constexpr uint64_t Atlas = 0x50574e445448554dull;
            Editor::BrowserThumbnails m_Thumbnails{Atlas};
            PlatformWindowPresentation m_First, m_Last;
            ImGuiID m_View = 0;
            uint64_t m_DockedAt = 0;
            enum class Stage { Detached, Redock, DetachAgain, Close };
            Stage m_Stage = Stage::Detached;
            bool m_Finished = false;
        };
    }

    std::unique_ptr<Layer> CreatePlatformWindowGpuTest() {
        return std::make_unique<PlatformWindowGpuTest>();
    }
}
