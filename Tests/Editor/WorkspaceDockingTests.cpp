#include "Application/EditorWorkspace.h"
#include "Assets/AssetTestFixture.h"
#include "Core/Application.h"
#include "Log/LogSubscription.h"
#include "TestRunner.h"
#include "imgui_internal.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <chrono>

namespace Catalpa::Tests {
    namespace {
        constexpr const char* Panels[]{"###Catalpa.Scene", "###Catalpa.Assets", "###Catalpa.Content"};

        class WorkspaceDockingTest final : public Layer {
          public:
            WorkspaceDockingTest() : Layer("WorkspaceDockingTest") {}

            void OnAttach() override {
                std::string error;
                if (!m_Fixture.Create(error)) { Finish(false, error); return; }
                const auto file = m_Fixture.root / "Workspace.json";
                m_Fixture.files = {file, m_Fixture.root / "Workspace.json.bak"};
                m_Editor = std::make_unique<Editor::EditorWorkspace>(std::filesystem::path{}, file);
                m_Editor->OnAttach();
                const auto main = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
                // 给主窗右侧留下可见桌面，验证标签能越过客户区边界。
                SetWindowPos(main, nullptr, 60, 60, 960, 720, SWP_NOACTIVATE | SWP_NOZORDER);
                ImGuiContextHook hook;
                hook.Type = ImGuiContextHookType_NewFramePre;
                hook.UserData = this;
                hook.Callback = [](ImGuiContext*, ImGuiContextHook* h) {
                    static_cast<WorkspaceDockingTest*>(h->UserData)->DispatchPointer();
                };
                m_Hook = ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
                m_Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(40);
            }

            void OnDetach() override {
                if (m_Hook) ImGui::RemoveContextHook(ImGui::GetCurrentContext(), m_Hook);
                if (m_Editor) m_Editor->OnDetach();
            }
            void OnUpdate(Deltatime dt) override { if (m_Editor) m_Editor->OnUpdate(dt); }
            void OnEvent(Event& e) override {
                if (e.GetEventType() == EventType::WindowClose && !m_Finished)
                    Finish(false, "Test window closed before the last docking gesture");
                else if (m_Editor) m_Editor->OnEvent(e);
            }

            void OnImGuiRender() override {
                if (!m_Editor || m_Finished) return;
                m_Editor->OnImGuiRender();
                uint64_t dropped{};
                for (const auto& record : m_Log.Drain(256, dropped)) {
                    if (record.message.find("VUID-") != std::string::npos || record.message.find("SYNC-HAZARD") != std::string::npos) {
                        Finish(false, record.message);
                        return;
                    }
                }
                if (dropped || std::chrono::steady_clock::now() > m_Deadline) {
                    Finish(false, dropped ? "Validation log overflow" : "Timed out");
                    return;
                }
                auto* panel = ImGui::FindWindowByName(Panels[m_Index]);
                auto* target = ImGui::FindWindowByName("###Catalpa.Inspector");
                if (!panel || !target || !panel->Active || !target->Active || !target->DockNode) return;
                auto* main = ImGui::GetMainViewport();
                const auto* scene = ImGui::FindWindowByName("###Catalpa.Viewport");
                if (!scene || !scene->Active || !scene->DockNode || !scene->DockNode->IsCentralNode() || scene->Viewport != main) {
                    Finish(false, "Scene viewport left the central node of the main window");
                    return;
                }
                PlatformWindowPresentation present;
                switch (m_Stage) {
                case Stage::Start:
                    if (!panel->DockNode || !panel->DockNode->TabBar || ++m_Frames < 6) return;
                    m_Frames = 0;
                    m_SourceTabs = panel->DockNode->Windows.Size;
                    m_TargetDock = target->DockId;
                    BeginDrag(*panel, panel->DC.DockTabItemRect.GetCenter(),
                              {main->Pos.x + main->Size.x + 100, main->Pos.y + 150});
                    m_Stage = Stage::DragOut;
                    break;
                case Stage::DragOut:
                    if (AdvanceDrag()) m_Stage = Stage::Floating;
                    break;
                case Stage::Floating: {
                    const auto* view = panel->Viewport;
                    if (!view || view == main || !view->PlatformHandleRaw || panel->DockNode ||
                        !Application::Get().GetPlatformWindowPresentation(view->ID, present) || present.sequence < 3) return;
                    RECT rect;
                    const auto hwnd = static_cast<HWND>(view->PlatformHandleRaw);
                    if (!IsWindowVisible(hwnd) || !GetWindowRect(hwnd, &rect) || rect.right <= main->Pos.x + main->Size.x) return;
                    m_Detached = view->ID;
                    CAT_INFO("[dock gesture] {}: dragged from {} tab(s) to HWND {} outside host; present {}",
                             Panels[m_Index], m_SourceTabs, reinterpret_cast<uintptr_t>(hwnd), present.sequence);
                    auto title = panel->TitleBarRect().GetCenter();
                    // 放到目标标签栏空白区，触发常规标签合并预览与释放。
                    const auto bar = target->DockNode->TabBar;
                    if (!bar) return;
                    BeginDrag(*panel, title, bar->BarRect.GetCenter());
                    m_Stage = Stage::DragBack;
                    break;
                }
                case Stage::DragBack:
                    if (AdvanceDrag()) m_Stage = Stage::Docked;
                    break;
                case Stage::Docked:
                    if (!panel->DockNode || panel->DockId != m_TargetDock || panel->Viewport != main ||
                        ImGui::FindViewportByID(m_Detached) || Application::Get().GetPlatformWindowPresentation(m_Detached, present)) return;
                    CAT_INFO("[dock gesture] {}: docked into Inspector node {}, detached swapchain retired", Panels[m_Index], m_TargetDock);
                    if (++m_Index == std::size(Panels)) { Finish(true, "All panel gestures passed"); return; }
                    m_Frames = 0;
                    m_Stage = Stage::Start;
                    break;
                }
            }
          private:
            enum class Stage { Start, DragOut, Floating, DragBack, Docked };
            void BeginDrag(const ImGuiWindow& panel, ImVec2 start, ImVec2 end) {
                m_Receiver = static_cast<HWND>(panel.Viewport->PlatformHandleRaw);
                m_Start = m_Position = start;
                m_End = end;
                m_Message = WM_MOUSEMOVE;
                m_Step = 0;
                m_Pointer = true;
            }
            bool AdvanceDrag() {
                ++m_Step;
                if (m_Step == 1) m_Message = WM_LBUTTONDOWN;
                if (m_Step >= 3 && m_Step <= 19) {
                    const float t = float(m_Step - 2) / 17.f;
                    m_Position = ImLerp(m_Start, m_End, t);
                }
                if (m_Step == 25) m_Message = WM_LBUTTONUP;
                return m_Step > 26;
            }
            void DispatchPointer() {
                if (!m_Pointer || m_Finished) return;
                // GLFW 去重相同客户坐标。持续 1 像素的微动使悬停位置跟随测试鼠标，
                // 避免非输入桌面的系统光标轮询在按下/松开帧恢复到 (0, 0)。
                POINT screen{LONG(m_Position.x) + (ImGui::GetFrameCount() & 1), LONG(m_Position.y)};
                const HWND hovered = WindowFromPoint(screen);
                ImGuiID hoverId = 0;
                for (const auto* window : ImGui::GetPlatformIO().Viewports)
                    if (window->PlatformHandleRaw == hovered)
                        hoverId = window->ID;
                // 独立桌面没有系统光标；按指定屏幕点查询 Win32 命中窗口以提供悬停视口。
                // 位置与按钮仍由 HWND 经 GLFW 的实际 WndProc 和 ImGui 后端处理。
                ImGui::GetIO().AddMouseViewportEvent(hoverId);
                const HWND receiver = IsWindow(m_Receiver) ? m_Receiver : hovered;
                if (!receiver || !ScreenToClient(receiver, &screen)) return;
                const LPARAM position = MAKELPARAM(screen.x, screen.y);
                SendMessageW(receiver, WM_MOUSEMOVE, m_Pressed ? MK_LBUTTON : 0, position);
                if (m_Message != WM_MOUSEMOVE) {
                    m_Pressed = m_Message == WM_LBUTTONDOWN;
                    SendMessageW(receiver, m_Message, m_Pressed ? MK_LBUTTON : 0, position);
                }
                m_Message = WM_MOUSEMOVE;
            }
            void Finish(bool ok, const std::string& reason) {
                m_Finished = true;
                if (ok) CAT_INFO("[dock gesture PASS] {}", reason);
                else CAT_ERROR("[dock gesture FAIL] {} panel={} stage={} frame={} move={} pos=({}, {})", reason,
                    Panels[m_Index], int(m_Stage), m_Step, ImGui::GetCurrentContext()->MovingWindow != nullptr,
                    ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);
                Application::Get().RequestClose(ok ? 0 : 1);
            }

            AssetTest::Fixture m_Fixture;
            LogSubscription m_Log;
            std::unique_ptr<Editor::EditorWorkspace> m_Editor;
            std::chrono::steady_clock::time_point m_Deadline;
            HWND m_Receiver{};
            ImGuiID m_Hook{}, m_TargetDock{}, m_Detached{};
            ImVec2 m_Start, m_End, m_Position;
            UINT m_Message = WM_MOUSEMOVE;
            Stage m_Stage = Stage::Start;
            size_t m_Index = 0;
            int m_Frames = 0, m_Step = 0, m_SourceTabs = 0;
            bool m_Pointer = false, m_Pressed = false, m_Finished = false;
        };
    }

    std::unique_ptr<Layer> CreateWorkspaceDockingTest() {
        return std::make_unique<WorkspaceDockingTest>();
    }
}
