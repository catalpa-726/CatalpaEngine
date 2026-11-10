#include "TestRunner.h"
#include "Application/EditorWorkspace.h"
#include "Core/Application.h"
#include "Core/PlatformServices.h"
#include "Events/ApplicationEvent.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "imgui_internal.h"
#include <chrono>

namespace Catalpa::Tests {
    namespace {
        // 独立进程共享专用配置。第一次用真实控件切换语言和字号，第二次检查重新启动后的绘制与配置。
        class WorkspaceLanguageTest final : public Layer {
          public:
            explicit WorkspaceLanguageTest(const std::filesystem::path& directory)
                : Layer("WorkspaceLanguageTest"), m_Path(directory / "CatalpaLanguageAcceptance.json") {}

            void OnAttach() override {
                std::string error;
                Editor::WorkspaceSettings settings;
                auto result = Editor::LoadWorkspaceSettings(m_Path, settings, error);
                if (result == Editor::WorkspaceLoadResult::Failed) { Fail(error); return; }
                if (result == Editor::WorkspaceLoadResult::Missing) {
                    settings.showSettings = true;
                    settings.editorFontSize = 18;
                    if (!Editor::SaveWorkspaceSettings(m_Path, settings, error)) { Fail(error); return; }
                } else if (settings.language == "zh-CN" && settings.editorFontSize == 24) {
                    m_Reopening = true;
                } else if (settings.language != "en-US" || settings.editorFontSize != 18 || !settings.showSettings) {
                    Fail("Unexpected language acceptance fixture; use a new directory");
                    return;
                }
                auto& scenes = Application::Get().GetScenes();
                SceneHandle initial;
                auto status = scenes.CreateScene("Language acceptance", initial);
                if (status.IsSuccess()) status = scenes.SetActive(initial);
                if (!status.IsSuccess()) { Fail(status.message); return; }
                m_Workspace = std::make_unique<Editor::EditorWorkspace>(std::filesystem::path{}, m_Path);
                m_Workspace->OnAttach();
                m_Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            }
            void OnDetach() override {
                if (m_MouseHook) ImGui::RemoveContextHook(ImGui::GetCurrentContext(), m_MouseHook);
                if (m_Workspace) m_Workspace->OnDetach();
            }
            void OnUpdate(Deltatime dt) override {
                if (!m_Workspace || m_Failed) return;
                if (std::chrono::steady_clock::now() > m_Deadline) {
                    Fail("Timed out at Settings action " + std::to_string(m_Step));
                    return;
                }
                m_Workspace->OnUpdate(dt);
            }
            void OnEvent(Event& e) override {
                if (!m_Workspace) return;
                if (e.GetEventType() == EventType::WindowClose && !m_Done) {
                    Fail("Workspace was closed before verifying its rendered language");
                    return;
                }
                m_Workspace->OnEvent(e);
            }
            void OnImGuiRender() override {
                if (!m_Workspace || m_Failed) return;
                ImGui::LogToBuffer(0);
                m_Workspace->OnImGuiRender();
                const auto& buffer = ImGui::GetCurrentContext()->LogBuffer;
                const std::string drawn(buffer.c_str(), static_cast<size_t>(buffer.size()));
                const bool chinese = drawn.find("界面字号") != drawn.npos && drawn.find("恢复为 24 px") != drawn.npos;
                ImGui::LogFinish();
                if (m_Done) return;

                if (m_Reopening && m_VerifiedFrames >= 12) {
                    if (!CheckInspector(drawn)) return;
                } else if (!CheckLanguageControls(chinese)) return;

                Editor::WorkspaceSettings saved;
                std::string error;
                if (Editor::LoadWorkspaceSettings(m_Path, saved, error) != Editor::WorkspaceLoadResult::Loaded ||
                    saved.language != "zh-CN" || saved.editorFontSize != 24) {
                    Fail(error.empty() ? "Saved language or font size differs from the rendered Settings panel" : error);
                    return;
                }
                CAT_INFO("[Editor language PASS] {0}; actual Chinese Settings labels rendered, 24 px persisted",
                         m_Reopening ? "Reopened Grove; Inspector followed entity, asset and main window" : "English 18 px changed with real Settings widgets");
                m_Done = true;
                WindowCloseEvent close;
                m_Workspace->OnEvent(close);
            }
          private:
            bool CheckLanguageControls(bool chinese) {
                auto* settings = ImGui::FindWindowByName("###Catalpa.Settings");
                if (!settings || !settings->Active) return false;
                if (settings->SkipItems) { ImGui::FocusWindow(settings); return false; }
                // 首帧构建 DockSpace 后标签还会迁移一次，等窗口真正稳定再激活控件。
                if (++m_ShownFrames < 5) return false;
                if (!m_Reopening && m_Step < 3) {
                    DriveSettings(*settings, chinese);
                    return false;
                }
                if (!chinese || ImGui::GetStyle().FontScaleMain != 1.f) return false;
                if (m_Reopening && !PresentChineseGrove()) return false;
                return ++m_VerifiedFrames >= 12 && !m_Reopening;
            }

            bool CheckInspector(std::string_view drawn) {
                auto* inspector = ImGui::FindWindowByName("###Catalpa.Inspector");
                if (!inspector || inspector->SkipItems) {
                    if (inspector) ImGui::FocusWindow(inspector);
                    return false;
                }
                const bool entity = drawn.find("旋转 XYZ（度）") != drawn.npos;
                const bool file = drawn.find("打开作者项目后，通过内容浏览器导入支持的源文件。") != drawn.npos;
                const bool window = drawn.find("帧缓冲尺寸") != drawn.npos;
                if (m_Step == 0) {
                    auto* viewport = ImGui::FindWindowByName("###Catalpa.Viewport");
                    if (!viewport || viewport->SkipItems) return false;
                    // 庭院视图中央下方是铺地。在 ImGui 消费下一帧输入前送入点击，走日常异步 GPU 拾取路径。
                    m_Click = viewport->Rect().GetCenter();
                    m_Click.y += viewport->Size.y * .25f;
                    ImGuiContextHook hook;
                    hook.Type = ImGuiContextHookType_NewFramePre;
                    hook.UserData = this;
                    hook.Callback = [](ImGuiContext* ctx, ImGuiContextHook* h) {
                        auto& test = *static_cast<WorkspaceLanguageTest*>(h->UserData);
                        ctx->IO.AddMousePosEvent(test.m_Click.x, test.m_Click.y);
                        ctx->IO.AddMouseViewportEvent(ImGui::GetMainViewport()->ID);
                        ctx->IO.AddMouseButtonEvent(ImGuiMouseButton_Left, !test.m_MouseDown);
                        if (test.m_MouseDown) {
                            ImGui::RemoveContextHook(ctx, h->HookId);
                            test.m_MouseHook = 0;
                        }
                        test.m_MouseDown = true;
                    };
                    m_MouseHook = ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
                    ++m_Step;
                } else if (m_Step == 1 && entity && !file && !window) {
                    auto* assets = ImGui::FindWindowByName("###Catalpa.Assets");
                    if (!assets || assets->SkipItems) return false;
                    std::filesystem::path executable;
                    std::string error;
                    if (!GetExecutableDirectory(executable, error)) { Fail(error); return false; }
                    std::error_code pathError;
                    const auto folder = std::filesystem::canonical(executable / "Resources" / "Grove", pathError).generic_u8string();
                    if (pathError) { Fail(pathError.message()); return false; }
                    for (auto* child : assets->DC.ChildWindows) {
                        if (child->ChildId != assets->GetID("Files")) continue;
                        ImGui::ActivateItemByID(ImHashStr("##File", 0, child->GetID(reinterpret_cast<const char*>(folder.c_str()))));
                        ++m_Step;
                        break;
                    }
                } else if (m_Step == 2 && file && !entity && !window) {
                    auto* hierarchy = ImGui::FindWindowByName("###Catalpa.Scene");
                    if (!hierarchy || hierarchy->SkipItems) return false;
                    ImGui::ActivateItemByID(hierarchy->GetID("###MainWindow"));
                    ++m_Step;
                }
                return m_Step == 3 && window && !entity && !file;
            }
            // 重开后从主菜单运行展示场景；中文层级、场景视口和属性框必须仍在同一实际工作区。
            bool PresentChineseGrove() {
                if (!m_GroveRequested) {
                    auto* bar = ImGui::FindWindowByName("##MainMenuBar");
                    if (!bar) return false;
                    if (auto* menu = ImGui::FindWindowByName("###Menu_00"); menu && menu->Active && !menu->SkipItems) {
                        ImGui::ActivateItemByID(menu->GetID("###OpenGrove"));
                        m_GroveRequested = true;
                    } else if (m_ShownFrames % 10 == 5) {
                        ImGui::ActivateItemByID(ImHashStr("###File", 0, bar->GetID("##MenuBar")));
                    }
                    return false;
                }
                const auto& app = Application::Get();
                const auto* scene = Application::Get().GetScenes().GetActive();
                RenderSurfaceSnapshot frame;
                return scene && scene->GetName() == "Grove" && app.GetEditorPreview(0x43415453434e4501ULL, frame) &&
                       frame.textureToken && frame.sceneIdentity == scene->GetWorld().GetIdentity();
            }
            void DriveSettings(ImGuiWindow& settings, bool chinese) {
                switch (m_Step) {
                case 0:
                    if (ImGui::GetStyle().FontScaleMain != .75f) { Fail("Expected English fixture at 18 px"); return; }
                    ImGui::ActivateItemByID(settings.GetID("###WorkspaceLanguage"));
                    ++m_Step;
                    break;
                case 1:
                    if (auto* popup = ImGui::FindWindowByName("##Combo_00"); popup && popup->Active && !popup->SkipItems) {
                        // Combo 在弹窗内先 PushID(选项索引)，再提交带完整名称的 Selectable。
                        const ImGuiID row = popup->GetID(1);
                        ImGui::ActivateItemByID(ImHashStr("简体中文 (zh-CN)", 0, row));
                        ++m_Step;
                    } else if (m_ShownFrames % 12 == 0) {
                        ImGui::ActivateItemByID(settings.GetID("###WorkspaceLanguage"));
                    }
                    break;
                case 2:
                    if (chinese) {
                        ImGui::ActivateItemByID(settings.GetID("###ResetWorkspaceFontSize"));
                        ++m_Step;
                    }
                    break;
                }
            }
            void Fail(const std::string& reason) {
                CAT_ERROR("[Editor language FAIL] {0}", reason);
                m_Failed = true;
                Application::Get().RequestClose(1);
            }
            std::filesystem::path m_Path;
            std::unique_ptr<Editor::EditorWorkspace> m_Workspace;
            std::chrono::steady_clock::time_point m_Deadline{};
            int m_Step = 0, m_ShownFrames = 0, m_VerifiedFrames = 0;
            ImGuiID m_MouseHook = 0;
            ImVec2 m_Click;
            bool m_MouseDown = false;
            bool m_Reopening = false, m_GroveRequested = false, m_Done = false, m_Failed = false;
        };
    }
    std::unique_ptr<Layer> CreateWorkspaceLanguageTest(const std::filesystem::path& directory) {
        return std::make_unique<WorkspaceLanguageTest>(directory);
    }
}
