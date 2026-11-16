#include "Application/EditorWorkspace.h"
#include "Application/WorkspaceSettings.h"
#include "Assets/AssetTestFixture.h"
#include "Core/Application.h"
#include "TestRunner.h"
#include "imgui_internal.h"

#include <objbase.h>
#include <shobjidl.h>
#include <wrl.h>
#include <chrono>
#include <cmath>

namespace Catalpa::Tests {
	namespace {
		/* 保留旧布局中会影响输入的独立后备视口及中央标签的选中顺序。 */
		constexpr const char* LegacyLayout = R"ini([Window][Debug##Default]
ViewportPos=60,94
ViewportId=0x16723995
Size=400,400
Collapsed=0

[Window][Catalpa.Viewport]
DockId=0x00000008,0

[Window][Catalpa.Welcome]
DockId=0x00000008,1

[Window][Catalpa.Scene]
DockId=0x00000001,0

[Window][Catalpa.Inspector]
DockId=0x00000004,0

[Window][Catalpa.Content]
DockId=0x00000006,0

[Window][Catalpa.Console]
DockId=0x00000006,1

[Docking][Data]
DockSpace ID=0xC27326DF Window=0x1BBC0F80 Pos=0,68 Size=960,720 Split=X
  DockNode ID=0x00000001 Parent=0xC27326DF SizeRef=182,720
  DockNode ID=0x00000002 Parent=0xC27326DF SizeRef=778,720 Split=X
    DockNode ID=0x00000003 Parent=0x00000002 SizeRef=576,720 Split=Y
      DockNode ID=0x00000008 Parent=0x00000003 SizeRef=576,540 CentralNode=1 Selected=0xAE8BBAB8
      DockNode ID=0x00000006 Parent=0x00000003 SizeRef=576,180
    DockNode ID=0x00000004 Parent=0x00000002 SizeRef=202,720

)ini";

		/** @brief 观察本次 COM 目录对话框及其窗口所有者，在窗口实际可见后自行取消。 */
		class FolderDialogProbe final
		    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
		          IFileDialogEvents> {
		public:
			/**
			 * @brief 以主窗口为所有者显示测试专有的 Shell 目录选择器。
			 * @param[in] owner 引擎主窗口句柄。
			 * @return 观察到可见且归属正确的对话框，并成功取消后恢复主窗口输入时为 true。
			 */
			bool Run(HWND owner) {
				if (!IsWindow(owner) || !IsWindowEnabled(owner))
					return false;
				Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
				HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog));
				FILEOPENDIALOGOPTIONS options{};
				if (SUCCEEDED(hr))
					hr = dialog->GetOptions(&options);
				if (SUCCEEDED(hr))
					hr = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
				DWORD cookie{};
				if (SUCCEEDED(hr))
					hr = dialog->Advise(this, &cookie);
				if (FAILED(hr)) {
					CAT_ERROR("[Shell folder dialog] setup HRESULT=0x{:08X}", static_cast<unsigned>(hr));
					return false;
				}
				m_Dialog = dialog.Get();
				m_Owner = owner;
				m_Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
				s_Active = this;
				m_Timer = SetTimer(nullptr, 0, 30, OnTimer);
				const HRESULT shown = m_Timer ? dialog->Show(owner) : E_FAIL;
				if (m_Timer)
					KillTimer(nullptr, m_Timer);
				s_Active = nullptr;
				dialog->Unadvise(cookie);
				m_Dialog = nullptr;
				const bool enabled = IsWindowEnabled(owner) != FALSE;
				const bool passed = !m_TimedOut && m_SawFolder && m_Visible && m_CorrectOwner && enabled &&
				    SUCCEEDED(m_CloseResult) && shown == HRESULT_FROM_WIN32(ERROR_CANCELLED);
				CAT_INFO(
				    "[Shell folder dialog] seen={} visible={} owned={} timeout={} ownerEnabled={} Close=0x{:08X} Show=0x{:08X}",
				    m_SawFolder, m_Visible, m_CorrectOwner, m_TimedOut, enabled, static_cast<unsigned>(m_CloseResult),
				    static_cast<unsigned>(shown));
				return passed;
			}

			/** @brief 只观察本实例的文件夹导航事件；早于窗口显示的回调留到其模态消息循环复查。 */
			HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog*) override {
				if (m_Dialog) {
					m_SawFolder = true;
					ObserveWindow();
				}
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog*) override { return S_FALSE; }
			HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog*, IShellItem*) override { return S_OK; }
			HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog*) override { return S_OK; }
			HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog*) override { return S_OK; }
			HRESULT STDMETHODCALLTYPE OnShareViolation(
			    IFileDialog*, IShellItem*, FDE_SHAREVIOLATION_RESPONSE* response) override {
				*response = FDESVR_DEFAULT;
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE OnOverwrite(
			    IFileDialog*, IShellItem*, FDE_OVERWRITE_RESPONSE* response) override {
				*response = FDEOR_DEFAULT;
				return S_OK;
			}

		private:
			/** @brief 通过当前 COM 实例查询其窗口，确认可见状态与 Win32 所有者。 */
			void ObserveWindow() {
				Microsoft::WRL::ComPtr<IOleWindow> window;
				HWND handle{};
				if (SUCCEEDED(m_Dialog->QueryInterface(IID_PPV_ARGS(&window))) &&
				    SUCCEEDED(window->GetWindow(&handle)) && handle && IsWindowVisible(handle)) {
					m_Visible = true;
					m_CorrectOwner = GetWindow(handle, GW_OWNER) == m_Owner;
				}
			}

			/** @brief 在调用 Show 的同一线程中取消选择器；超时即使关闭成功也记为失败。 */
			static void CALLBACK OnTimer(HWND, UINT, UINT_PTR timer, DWORD) {
				auto* probe = s_Active;
				if (!probe || probe->m_Timer != timer || probe->m_Closing)
					return;
				if (probe->m_SawFolder)
					probe->ObserveWindow();
				if (std::chrono::steady_clock::now() >= probe->m_Deadline)
					probe->m_TimedOut = true;
				if (probe->m_Visible || probe->m_TimedOut) {
					probe->m_Closing = true;
					probe->m_CloseResult = probe->m_Dialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
					probe->m_Closing = SUCCEEDED(probe->m_CloseResult);
				}
			}

			inline static thread_local FolderDialogProbe* s_Active{};
			IFileDialog* m_Dialog{};
			HWND m_Owner{};
			UINT_PTR m_Timer{};
			std::chrono::steady_clock::time_point m_Deadline{};
			HRESULT m_CloseResult = E_PENDING;
			bool m_SawFolder = false, m_Visible = false, m_CorrectOwner = false, m_TimedOut = false, m_Closing = false;
		};

		/** @brief 用鼠标逐帧检查正式工作区的两个新建项目入口和模态关闭行为。 */
		class WorkspaceModalTest final : public Layer {
		public:
			WorkspaceModalTest() : Layer("WorkspaceModalTest") {}

			/** @brief 创建含旧停靠数据的隔离工作区，并注册 ImGui 帧前指针事件。 */
			void OnAttach() override {
				/* 原生目录对话框随编辑器在 UI 线程创建，必须沿用窗口消息循环所在的 STA。 */
				APTTYPE apartment = APTTYPE_CURRENT;
				APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
				const HRESULT hr = CoGetApartmentType(&apartment, &qualifier);
				if (FAILED(hr) || (apartment != APTTYPE_STA && apartment != APTTYPE_MAINSTA)) {
					CAT_ERROR("[Editor modals] UI apartment APTTYPE={} qualifier={} HRESULT=0x{:08X}", int(apartment),
					    int(qualifier), static_cast<unsigned>(hr));
					Finish(false, "Editor UI thread must already own a single-threaded COM apartment");
					return;
				}
				std::string error;
				if (!m_Fixture.Create(error)) {
					Finish(false, error);
					return;
				}
				const auto settingsPath = m_Fixture.root / "Workspace.json";
				m_Fixture.files = {settingsPath, m_Fixture.root / "Workspace.json.bak"};
				Editor::WorkspaceSettings settings;
				settings.language = "zh-CN";
				settings.layout = LegacyLayout;
				if (!Editor::SaveWorkspaceSettings(settingsPath, settings, error)) {
					Finish(false, error);
					return;
				}
				m_Workspace = std::make_unique<Editor::EditorWorkspace>(std::filesystem::path{}, settingsPath);
				m_Workspace->OnAttach();
				ImGuiContextHook hook;
				hook.Type = ImGuiContextHookType_NewFramePre;
				hook.UserData = this;
				hook.Callback = [](ImGuiContext*, ImGuiContextHook* h) {
					static_cast<WorkspaceModalTest*>(h->UserData)->DispatchPointer();
				};
				m_Hook = ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
				m_Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(35);
			}

			/** @brief 注销指针事件并释放临时工作区。 */
			void OnDetach() override {
				if (m_Hook)
					ImGui::RemoveContextHook(ImGui::GetCurrentContext(), m_Hook);
				if (m_Workspace)
					m_Workspace->OnDetach();
			}

			/**
			 * @brief 更新工作区并监测交互超时。
			 * @param[in] dt 宿主本帧时间步。
			 */
			void OnUpdate(Deltatime dt) override {
				if (m_Finished || !m_Workspace)
					return;
				m_Workspace->OnUpdate(dt);
				if (std::chrono::steady_clock::now() > m_Deadline)
					Finish(false, "Timed out waiting for workspace control");
			}

			/**
			 * @brief 向工作区转交宿主事件。
			 * @param[in,out] event 当前事件。
			 */
			void OnEvent(Event& event) override {
				if (!m_Workspace || m_Finished)
					return;
				if (event.GetEventType() == EventType::WindowClose)
					Finish(false, "Workspace closed before completing the modal cycles");
				else
					m_Workspace->OnEvent(event);
			}

			/** @brief 点击标签、欢迎页和菜单，再逐次关闭弹窗并确认输入阻挡已解除。 */
			void OnImGuiRender() override {
				if (m_Finished || !m_Workspace)
					return;
				m_Workspace->OnImGuiRender();
				auto& context = *ImGui::GetCurrentContext();
				if (!(context.IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) ||
				    !context.IO.ConfigViewportsNoAutoMerge) {
					Finish(false, "Expected platform viewports with automatic merging disabled");
					return;
				}
				if (m_Stroke == Stroke::Press) {
					if (context.IO.MouseDown[0])
						m_Stroke = Stroke::Release;
					return;
				}
				if (m_Stroke == Stroke::Release) {
					if (!context.IO.MouseDown[0]) {
						m_Stroke = Stroke::Hover;
						m_Stage = m_AfterClick;
						m_Target = 0;
					}
					return;
				}

				auto* welcome = ImGui::FindWindowByName("###Catalpa.Welcome");
				auto* scene = ImGui::FindWindowByName("###Catalpa.Viewport");
				const float step = ImMax(4.f, ImGui::GetFrameHeight() * .4f);
				switch (m_Stage) {
				case Stage::SceneTab:
				case Stage::WelcomeTab: {
					if (!welcome || !scene || !welcome->Active || !scene->Active || !scene->DockNode ||
					    welcome->DockNode != scene->DockNode || ++m_Frames < 6)
						return;
					if (m_Stage == Stage::SceneTab && scene->DockTabIsVisible && !welcome->DockTabIsVisible) {
						m_Stage = Stage::WelcomeTab;
						return;
					}
					auto* tab = m_Stage == Stage::SceneTab ? scene : welcome;
					ClickTab(*tab, m_Stage == Stage::SceneTab ? Stage::WelcomeTab : Stage::Welcome);
					break;
				}
				case Stage::Welcome: {
					if (!welcome || !welcome->DockTabIsVisible || welcome->SkipItems)
						return;
					if (!context.OpenPopupStack.empty() || ImGui::FindBlockingModal(welcome)) {
						Finish(false, "A modal blocks the welcome page");
						return;
					}
					const auto rect = welcome->InnerClipRect;
					Probe(welcome->GetID("###Launcher.New"), {rect.GetCenter().x, rect.Min.y + 3}, {0, step},
					    int(rect.GetHeight() / step), Stage::Form);
					break;
				}
				case Stage::File: {
					auto* bar = ImGui::FindWindowByName("##MainMenuBar");
					if (!bar || !bar->Active || bar->SkipItems)
						return;
					const auto rect = bar->MenuBarRect();
					Probe(ImHashStr("###File", 0, bar->GetID("##MenuBar")), {rect.Min.x + 3, rect.GetCenter().y},
					    {step, 0}, int(rect.GetWidth() / step), Stage::MenuNew);
					break;
				}
				case Stage::MenuNew: {
					auto* menu = ImGui::FindWindowByName("###Menu_00");
					if (!menu || !menu->Active || menu->SkipItems)
						return;
					const auto rect = menu->InnerClipRect;
					Probe(menu->GetID("###NewProject"), {rect.GetCenter().x, rect.Min.y + 3}, {0, step},
					    int(rect.GetHeight() / step), Stage::Form);
					break;
				}
				case Stage::Form: {
					auto* form = ImGui::FindWindowByName("###Catalpa.NewProject");
					if (!form || !form->Active || ImGui::GetTopMostPopupModal() != form)
						return;
					if (context.OpenPopupStack.Size != 1 || form->Viewport != ImGui::GetMainViewport() ||
					    form->ViewportOwned) {
						Finish(false, "New project must be the single active modal in the main platform viewport");
						return;
					}
					if (form->SkipItems)
						return;
					if (!m_ShellVerified) {
						const auto probe = Microsoft::WRL::Make<FolderDialogProbe>();
						const auto owner = static_cast<HWND>(form->Viewport->PlatformHandleRaw);
						if (!probe || !probe->Run(owner)) {
							Finish(false, "Shell folder dialog did not show with the main HWND and cleanly cancel");
							return;
						}
						m_ShellVerified = true;
						return;
					}
					/* 无错误的新建表单以取消按钮收尾，使用实际排版所得行末位置。 */
					const auto end = form->DC.CursorPosPrevLine;
					Probe(form->GetID("###CancelCreateProject"), {end.x - 4, end.y + ImGui::GetFrameHeight() * .5f}, {},
					    10, Stage::Dismissal);
					break;
				}
				case Stage::Dismissal:
					if (!context.OpenPopupStack.empty())
						return;
					if (!welcome || ImGui::GetTopMostPopupModal() || ImGui::FindBlockingModal(welcome)) {
						Finish(false, "Cancelling project creation left the welcome page blocked");
						return;
					}
					if (++m_Closed == 3)
						Finish(true,
						    "Mouse clicked Welcome twice and File/New once; three cancellations cleared modal blocking");
					else
						m_Stage = m_Closed == 1 ? Stage::Welcome : Stage::File;
					break;
				}
			}

		private:
			enum class Stage { SceneTab, WelcomeTab, Welcome, File, MenuNew, Form, Dismissal };
			enum class Stroke { Hover, Press, Release };

			/** @brief 在 ImGui 处理本帧输入前发送鼠标位置、平台命中视口和左键状态。 */
			void DispatchPointer() {
				if (!m_HasPointer || m_Finished)
					return;
				auto& io = ImGui::GetIO();
				const auto* viewport = ImGui::FindHoveredViewportFromPlatformWindowStack(m_Position);
				io.AddMouseViewportEvent(viewport ? viewport->ID : 0);
				io.AddMousePosEvent(m_Position.x, m_Position.y);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, m_Stroke == Stroke::Press);
			}

			/** @brief 从停靠系统取得真实标签矩形，逐帧点击其中央。 */
			void ClickTab(const ImGuiWindow& tab, Stage after) {
				const auto rect = tab.DC.DockTabItemRect;
				if (rect.GetWidth() > 0 && rect.GetHeight() > 0)
					Probe(tab.TabId, rect.GetCenter(), {}, 10, after);
			}

			/**
			 * @brief 沿控件所在行或列移动鼠标，实际悬停到目标 ID 后发送按下与释放。
			 * @param[in] id 控件的稳定 ID，仅供观察命中状态。
			 * @param[in] origin 首个鼠标位置。
			 * @param[in] stride 相邻鼠标位置的位移。
			 * @param[in] count 可探测位置数。
			 * @param[in] after 释放后验证的交互阶段。
			 */
			void Probe(ImGuiID id, ImVec2 origin, ImVec2 stride, int count, Stage after) {
				if (m_Target != id) {
					m_Target = id;
					m_Sample = 0;
				} else {
					auto& context = *ImGui::GetCurrentContext();
					if (std::abs(m_Position.x - context.IO.MousePos.x) > 1.f ||
					    std::abs(m_Position.y - context.IO.MousePos.y) > 1.f)
						return;
					if (context.HoveredId == id) {
						m_Stroke = Stroke::Press;
						m_AfterClick = after;
						return;
					}
					if (++m_Sample >= count) {
						Finish(false, "Mouse could not hover the visible control");
						return;
					}
				}
				m_HasPointer = true;
				m_Position = {origin.x + stride.x * m_Sample, origin.y + stride.y * m_Sample};
			}

			/**
			 * @brief 将验收结果或最后的鼠标命中状态写入日志并请求退出。
			 * @param[in] ok 是否成功。
			 * @param[in] message 结果或失败原因。
			 */
			void Finish(bool ok, const std::string& message) {
				m_Finished = true;
				if (ok)
					CAT_INFO("[Editor modals PASS] {}", message);
				else {
					const auto& context = *ImGui::GetCurrentContext();
					CAT_ERROR(
					    "[Editor modals FAIL] {} (stage {}, completed {}, stroke {}, sample {}, target {:08X}, hovered {:08X}, active {:08X}, window {}, mouse {:.1f},{:.1f}, requested {:.1f},{:.1f}, viewport {:08X}, main flags {:08X}, popups {})",
					    message, int(m_Stage), m_Closed, int(m_Stroke), m_Sample, m_Target, context.HoveredId,
					    context.ActiveId, context.HoveredWindow ? context.HoveredWindow->Name : "(none)",
					    context.IO.MousePos.x, context.IO.MousePos.y, m_Position.x, m_Position.y,
					    context.IO.MouseHoveredViewport, unsigned(ImGui::GetMainViewport()->Flags),
					    context.OpenPopupStack.Size);
				}
				Application::Get().RequestClose(ok ? 0 : 1);
			}

			AssetTest::Fixture m_Fixture;
			std::unique_ptr<Editor::EditorWorkspace> m_Workspace;
			std::chrono::steady_clock::time_point m_Deadline{};
			Stage m_Stage = Stage::SceneTab, m_AfterClick = Stage::SceneTab;
			Stroke m_Stroke = Stroke::Hover;
			ImGuiID m_Hook{}, m_Target{};
			ImVec2 m_Position{};
			int m_Sample = 0, m_Frames = 0;
			unsigned m_Closed = 0;
			bool m_HasPointer = false, m_Finished = false, m_ShellVerified = false;
		};
	}

	std::unique_ptr<Layer> CreateWorkspaceModalTest() {
		return std::make_unique<WorkspaceModalTest>();
	}
}
