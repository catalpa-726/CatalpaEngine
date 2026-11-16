#include "AssetEditors/PhysicsNavigationInspector.h"
#include "AssetEditors/AudioUIInspector.h"
#include "Application/WorkspaceEditing.h"

#include "AssetPipeline/AssetImportRegistry.h"
#include "Assets/BuiltinAssetIds.h"
#include "Core/Application.h"
#include "Core/Window.h"
#include "Core/KeyCodes.h"
#include "UI/UIInputRouter.h"
#include "Commands/AssetCommands.h"
#include "Commands/SceneCommands.h"
#include "Commands/PrefabCommands.h"
#include "Panels/BuildPanel.h"
#include "Panels/EditorText.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include "Viewport/ToolbarButton.h"
#include "Viewport/ViewportPlacement.h"

namespace Catalpa::Editor {
	namespace {
		constexpr uint64_t SceneSurface = 0x43415453434e4501ULL;

		/** @brief 优先使用作者文件、导入子资源和内置材质的名称，未保存的草稿显示本地化类型。 */
		std::string DraftTitle(const AssetDocument& doc) {
			std::string name;
			if (!doc.AuthorPath().empty())
				name = Tools::AssetPathText(doc.AuthorPath().filename());
			else if (!doc.ProductName().empty())
				name = doc.ProductName();
			else if (doc.Asset() == BuiltinAssetIds::DefaultUnlitInstance)
				name = UiText("material.default_unlit", "Default unlit material");
			else if (doc.Asset() == BuiltinAssetIds::DefaultPbrInstance)
				name = UiText("material.default_pbr", "Default PBR material");
			else if (doc.Asset() == BuiltinAssetIds::ErrorInstance)
				name = UiText("material.error_instance", "Error material");
			else {
				const std::string type(doc.TypeKey());
				name = UiText(("asset.type." + type).c_str(), type.c_str());
				if (doc.Source())
					name += " (" + std::string(UiText("document.untitled", "Untitled")) + ")";
			}
			if (doc.History().Dirty())
				name += " *";
			return name;
		}

		/** @brief 将完整路径和版本信息保留在悬停提示内。 */
		void DraftTooltip(const AssetDocument& doc) {
			if (!ImGui::BeginItemTooltip())
				return;
			if (!doc.AuthorPath().empty())
				ImGui::TextWrapped("%s", Tools::AssetPathText(doc.AuthorPath()).c_str());
			ImGui::Text(
			    "%s / v%llu", doc.Asset().ToString().c_str(), static_cast<unsigned long long>(doc.SourceVersion()));
			ImGui::EndTooltip();
		}

		/** @brief 在场景工具栏显示移动速度；窄视口换行，弹窗可按对数拖动或直接输入米/秒。 */
		void DrawCameraSpeed(float& speed, const PanelText& text) {
			char value[32];
			std::snprintf(value, sizeof(value), " %.2f m/s", speed);
			const auto caption = text("viewport.camera_speed", "Camera speed", nullptr);
			const auto& style = ImGui::GetStyle();
			const float rightEdge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
			/* 按最大显示位数预留宽度，调速跨过整十或整百时不会改变视口高度。 */
			const float width = ImGui::CalcTextSize((caption + " 0000.00 m/s").c_str()).x + 2 * style.FramePadding.x;
			if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= rightEdge)
				ImGui::SameLine();
			if (ImGui::Button((caption + value + "###CameraSpeed").c_str()))
				ImGui::OpenPopup("CameraSpeedOptions");
			if (ImGui::BeginPopup("CameraSpeedOptions")) {
				const float sliderWidth =
				    std::min(18 * ImGui::GetFontSize(), ImGui::GetMainViewport()->WorkSize.x * .8f);
				ImGui::SetNextItemWidth(sliderWidth);
				float edited = speed;
				if (ImGui::SliderFloat("##CameraMoveSpeed", &edited, ViewportSettings::MinCameraSpeed,
				        ViewportSettings::MaxCameraSpeed, "%.2f m/s",
				        ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp |
				            ImGuiSliderFlags_NoRoundToFormat) &&
				    std::isfinite(edited))
					speed = edited;
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + sliderWidth);
				ImGui::TextUnformatted(text("viewport.camera_speed_help",
				    "Hold the middle mouse button and drag up or down, or scroll while holding it. Ctrl+click the slider to enter a speed in m/s.",
				    nullptr)
				        .c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndPopup();
			}
		}

		/**
		 * @brief 绘制视口投影和法线显示模式的切换控件。
		 * 宽视口直接显示两个模式，窄视口可在选项弹窗里使用相同控件。
		 *
		 * @param[in,out] camera 接收透视或正交投影切换的编辑相机。
		 * @param[in,out] normals 法线显示模式的开关，随控件选择更新。
		 * @param[out] error 切换相机投影失败时接收原因。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 */
		void DrawViewModes(EditorCameraController& camera, bool& normals, std::string& error, const PanelText& text) {
			const bool perspective = camera.Lens().projection == ProjectionType::Perspective;
			const auto perspectiveText = text("viewport.perspective", "Perspective", nullptr);
			const auto orthoText = text("viewport.ortho", "Orthographic", nullptr);
			const auto choice = [](const char* id, const std::string& a, const std::string& b, bool first) {
				const float width = std::max(ImGui::CalcTextSize(a.c_str()).x, ImGui::CalcTextSize(b.c_str()).x);
				ImGui::SetNextItemWidth(width + ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().FramePadding.x);
				int selection = -1;
				if (ImGui::BeginCombo(id, first ? a.c_str() : b.c_str())) {
					if (ImGui::Selectable(a.c_str(), first))
						selection = 0;
					if (ImGui::Selectable(b.c_str(), !first))
						selection = 1;
					ImGui::EndCombo();
				}
				return selection;
			};
			const int projection = choice("##projection", perspectiveText, orthoText, perspective);
			if (projection >= 0)
				camera.SetProjection(
				    projection == 0 ? ProjectionType::Perspective : ProjectionType::Orthographic, error);
			ImGui::SameLine();
			const int shading = choice("##shading", text("viewport.lit", "Lit", nullptr),
			    text("viewport.normals", "Normals", nullptr), !normals);
			if (shading >= 0)
				normals = shading == 1;
		}

		/**
		 * @brief 绘制导入设置。
		 *
		 * @param[in,out] doc 当前编辑的文档。
		 * @param[in] build 用于提交当前源文件重新导入请求的构建面板；无面板时无法提交。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 * @param[out] error 失败时接收原因说明。
		 */
		void DrawImportSettings(AssetDocument& doc, BuildPanel* build, const PanelText& text, std::string& error) {
			if ((!doc.Report().empty() || !doc.Diagnostics().empty()) &&
			    ImGui::CollapsingHeader(text("edit.cook_diagnostics", "Cook diagnostics", "CookDiagnostics").c_str())) {
				ImGui::TextWrapped("%s", Tools::AssetPathText(doc.Report()).c_str());
				for (const auto& diagnostic : doc.Diagnostics())
					ImGui::TextWrapped("%s: %s", diagnostic.element.c_str(), diagnostic.message.c_str());
			}
			if (!build || !doc.Recipe() ||
			    !ImGui::CollapsingHeader(
			        text("edit.import_settings", "Source import settings", "ImportSettings").c_str()))
				return;
			const auto& recipe = *doc.Recipe();
			const bool authorSource = bool(doc.Source());
			const auto& source = authorSource ? doc.AuthorPath() : recipe.source;
			ImGui::TextWrapped("%s", Tools::AssetPathText(source).c_str());
			const auto& settings = recipe.settings;
			double scale = settings.unitScale;
			bool normals = settings.generateNormals, tangents = settings.generateTangents;
			bool changed = ImGui::InputDouble(text("content.scale", "Additional unit scale", "DraftScale").c_str(),
			    &scale, 0, 0, "%.5g", ImGuiInputTextFlags_EnterReturnsTrue);
			changed |=
			    ImGui::Checkbox(text("edit.normals", "Generate missing normals", "DraftNormals").c_str(), &normals);
			changed |=
			    ImGui::Checkbox(text("edit.tangents", "Generate missing tangents", "DraftTangents").c_str(), &tangents);
			if (changed)
				ChangeImportSettings(doc, scale, normals, tangents, error);
			const bool needsSave = authorSource && (source.empty() || doc.History().Dirty());
			if (needsSave)
				ImGui::TextUnformatted(UiText(
				    "ui.save_the_author_document_before_rebuilding", "Save the author document before rebuilding."));
			ImGui::BeginDisabled(build->Busy() || needsSave);
			if (ImGui::Button(text("edit.reimport", "Rebuild from this draft", "Reimport").c_str())) {
				auto request = *doc.Recipe();
				/* 作者文件另存后使用新路径；模型导出的材质仍从原模型重新导入。 */
				if (authorSource)
					request.source = source;
				build->Start(request, error);
			}
			ImGui::EndDisabled();
		}

		/**
		 * @brief 绘制当前视图的方向控件，并识别被点击的有向坐标轴。
		 * 视轴取自实际显示图像的 view；透视和正交相机使用同一世界方向约定。
		 *
		 * @param[in] view 当前呈现图像对应的相机视图，用其视图矩阵确定各轴朝向。
		 * @param[in] origin 视口图像左上角在 ImGui 屏幕坐标中的位置。
		 * @param[in] size 视口图像的显示宽高，以 ImGui 逻辑像素计。
		 * @param[in] hovered 指针当前是否悬停于视口图像。
		 * @param[in] clicked 本帧是否触发点击，用于选取命中的方向轴。
		 * @param[out] overAxes 输出指针是否命中方向控件的交互区域。
		 * @param[in] text 根据翻译键生成当前语言的界面文案。
		 *
		 * @return 点击命中的轴端，±1、±2、±3 分别表示 X、Y、Z 的正负方向；未命中返回 0。
		 */
		int DrawOrientation(const RenderView& view, ImVec2 origin, ImVec2 size, bool hovered, bool clicked,
		    bool& overAxes, const PanelText& text) {
			const float unit = ImGui::GetFontSize();
			const float radius = std::min(unit * 1.5f, std::min(size.x, size.y) * .16f);
			if (radius < 10.f)
				return 0;
			const ImVec2 center{origin.x + size.x - radius * 1.7f, origin.y + radius * 1.7f};
			const auto mouse = ImGui::GetMousePos();
			const float mx = mouse.x - center.x, my = mouse.y - center.y;
			overAxes = hovered && mx * mx + my * my <= 2.3f * radius * radius;
			struct End {
				float x, y, z;
				int direction;
			};
			std::array<End, 6> ends;
			for (int axis = 0; axis < 3; ++axis) {
				const auto& column = view.matrices.view[axis];
				for (int side = 0; side < 2; ++side) {
					const float sign = side == 0 ? -1.f : 1.f;
					ends[axis * 2 + side] = {
					    column.x * sign, -column.y * sign, column.z * sign, (axis + 1) * (side == 0 ? -1 : 1)};
				}
			}
			std::stable_sort(ends.begin(), ends.end(), [](const End& a, const End& b) { return a.z < b.z; });
			auto* draw = ImGui::GetWindowDrawList();
			draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
			draw->AddCircleFilled(center, radius * 1.48f, IM_COL32(18, 21, 22, 165));
			const ImU32 colors[]{
			    IM_COL32(231, 93, 92, 255), IM_COL32(113, 204, 125, 255), IM_COL32(100, 155, 238, 255)};
			int hit = 0;
			for (const auto& e : ends) {
				const int axis = std::abs(e.direction) - 1;
				const ImVec2 endpoint{center.x + e.x * radius, center.y + e.y * radius};
				const float cap = radius * (e.direction > 0 ? .34f : .21f);
				draw->AddLine(center, endpoint, colors[axis], 1.7f);
				draw->AddCircleFilled(endpoint, cap, colors[axis]);
				if (e.direction > 0) {
					const char label[2]{char('X' + axis), 0};
					const float scale = cap * 1.65f;
					draw->AddText(ImGui::GetFont(), scale, {endpoint.x - scale * .31f, endpoint.y - scale * .5f},
					    IM_COL32(16, 20, 22, 255), label);
				}
				const float dx = mouse.x - endpoint.x, dy = mouse.y - endpoint.y;
				if (overAxes && dx * dx + dy * dy <= cap * cap)
					hit = e.direction;
			}
			draw->PopClipRect();
			if (hit)
				ImGui::SetTooltip("%s (%c%c)", text("viewport.align_axis", "Align to axis", nullptr).c_str(),
				    hit > 0 ? '+' : '-', char('X' + std::abs(hit) - 1));
			return clicked ? hit : 0;
		}

	}

	WorkspaceEditing::WorkspaceEditing(Application& app) : m_App(app) {
		m_HostScene = m_App.GetScenes().GetActiveHandle();
		m_App.SetEditorViewportActive(true);
	}

	WorkspaceEditing::~WorkspaceEditing() {
		std::string ignored;
		(void)Reset(ignored);
		m_App.SetEditorViewportActive(false);
	}

	bool WorkspaceEditing::Reset(std::string& error) {
		if (!StopPlay()) {
			error = m_Error;
			return false;
		}
		m_Picking.Cancel(m_App);
		m_Gizmo.Cancel(m_Error);
		m_Selection.Clear();
		m_App.CloseEditorPreview(SceneSurface);
		m_AssetEditors.Close(m_App);
		m_PendingScene.reset();
		m_PendingAuthor.reset();
		for (auto* doc : m_Documents.All())
			if (!doc->History().Dirty())
				RemoveRecovery(doc->Id());
		m_RestoredRecovery.clear();
		for (auto handle : m_AuthorScenes)
			if (m_App.GetScenes().GetScene(handle)) {
				const auto status = m_App.GetScenes().Unload(handle);
				if (!status.IsSuccess()) {
					m_Error = status.message;
					error = m_Error;
					return false;
				}
			}
		m_AuthorScenes.clear();
		m_PreGroveScene = {};
		m_GrovePath.clear();
		if (m_App.GetScenes().GetScene(m_HostScene))
			(void)m_App.GetScenes().SetActive(m_HostScene);
		m_Documents.Clear();
		m_Clipboard.reset();
		m_ClipboardDocument = {};
		m_AssetActions = {};
		m_GameView = false;
		m_OpenAsset = {};
		m_LastScene = {};
		m_Error.clear();
		m_AskDiscard = false;
		m_RightLook = false;
		m_Project.reset();
		m_Recovery.clear();
		m_RecoveryRoot.clear();
		error.clear();
		return true;
	}

	bool WorkspaceEditing::HasDirtyDocuments() const {
		return m_Documents.HasDirtyDocuments();
	}

	SceneDocument* WorkspaceEditing::ActiveScene() {
		if (auto* play = dynamic_cast<SceneDocument*>(m_Documents.Find(m_PlayDocument))) {
			if (play->Resolve() && m_App.GetScenes().GetScene(play->PlayHandle()) &&
			    m_App.GetScenes().GetActiveHandle() == play->PlayHandle())
				return play;
			if (!StopPlay())
				return nullptr;
		}
		auto& scenes = m_App.GetScenes();
		const auto handle = scenes.GetActiveHandle();
		if (!handle.HasValue())
			return nullptr;
		return m_Documents.SceneFor(scenes, handle, m_Error);
	}

	void WorkspaceEditing::Menu(const PanelText& text) {
		DocumentMenu();
		if (!ImGui::BeginMenu(text("menu.edit", "Edit", nullptr).c_str()))
			return;
		const auto item = [&](const char* key, const char* label, const char* shortcut, EditAction action) {
			if (ImGui::MenuItem(text(key, label, nullptr).c_str(), shortcut, false, CanEditAction(action)))
				PerformEditAction(action);
		};
		item("edit.undo", "Undo", "Ctrl+Z", EditAction::Undo);
		item("edit.redo", "Redo", "Ctrl+Y / Ctrl+Shift+Z", EditAction::Redo);
		ImGui::Separator();
		item("edit.copy", "Copy", "Ctrl+C", EditAction::Copy);
		item("edit.paste", "Paste", "Ctrl+V", EditAction::Paste);
		item("hierarchy.delete", "Delete subtree", "Delete", EditAction::Delete);
		item("hierarchy.rename", "Rename", "F2", EditAction::Rename);
		item("hierarchy.frame", "Frame selection", "F", EditAction::Frame);
		ImGui::EndMenu();
	}

	void WorkspaceEditing::SceneTree(const PanelText& text) {
		if (auto* doc = ActiveScene()) {
			ImGui::BeginDisabled(doc->Playing());
			const bool edited = m_Hierarchy.Draw(*doc, m_Selection, text, m_Error);
			if (m_Hierarchy.TakeFrameRequest())
				FrameSelection();
			/* 层级中的点击/创建/删除比上一帧的像素读回更新，迟到结果不可覆盖它。 */
			if (edited)
				m_Picking.Cancel(m_App);
			if (ImGui::IsWindowFocused() || edited)
				m_Documents.Activate(doc->Id());
			ImGui::EndDisabled();
		}
	}

	bool WorkspaceEditing::Inspect(const PanelText& text) {
		auto* doc = ActiveScene();
		if (doc && m_Selection.Resolve(*doc).HasValue()) {
			if (doc->Playing()) {
				ImGui::TextUnformatted(
				    UiText("ui.stop_play_to_edit_author_components", "Stop Play to edit author components."));
				return true;
			}
			if (ImGui::IsWindowFocused())
				m_Documents.Activate(doc->Id());
			AssetVersionRef<MaterialInstance> material;
			const bool inspected = m_Inspector.Draw(
			    *doc, m_Selection, text, material, m_Error, [&](size_t slot, const MeshRendererComponent& mesh) {
				    DrawMaterialPicker(
				        *doc, doc->Resolve()->MakeReference(m_Selection.Resolve(*doc)).persistent, slot, mesh);
			    });
			AuthorComponentControls(*doc);
			(void)DrawPhysicsNavigationInspector(
			    *doc, m_Selection.Resolve(*doc), m_Error, text, m_Inspector.PendingComponentType());
			DrawAudioUIInspector(
			    m_App, *doc, m_Selection.Resolve(*doc), text, m_Error, m_Inspector.PendingComponentType());
			if (material.HasValue())
				OpenBoundMaterial(material);
			return inspected;
		}
		return false;
	}

	bool WorkspaceEditing::HasSceneSelection() {
		const auto* doc = ActiveScene();
		return doc && m_Selection.Resolve(*doc).HasValue();
	}

	void WorkspaceEditing::ClearSceneSelection() {
		m_Picking.Cancel(m_App);
		m_Gizmo.Cancel(m_Error);
		m_Selection.Clear();
	}

	void WorkspaceEditing::ActivateAsset(AssetDocument& document) {
		ClearSceneSelection();
		m_Documents.Activate(document.Id());
	}

	void WorkspaceEditing::OpenAsset(AssetId id, const BuildPanel& build) {
		if (m_PlayDocument.HasValue()) {
			m_Error = "Stop Play before opening an author asset";
			return;
		}
		CancelPendingOpen();
		auto& assets = m_App.GetAssets();
		bool fallback = false;
		AssetVersionRef<EnvironmentAsset> environment;
		const auto& records = build.Result().records;
		const auto found =
		    std::find_if(records.begin(), records.end(), [id](const auto& entry) { return entry.id == id; });
		if (found == records.end()) {
			m_Error = "Selected asset is outside the installed cook result";
			return;
		}
		const auto type = found->type.ToString();
		if (build.Recipe()) {
			const auto ext = Tools::AssetSourceExtension(build.Recipe()->source);
			if (type == "scene") {
				const auto path = std::filesystem::canonical(build.Recipe()->source);
				if (m_Documents.FindByPath(path, m_Error)) {
					(void)OpenAuthor(path);
					return;
				}
				if (!m_Error.empty())
					return;
				AssetVersionRef<SceneAsset> fixed;
				if (!assets.Acquire(id, fixed, fallback, m_Error) || fallback)
					return;
				(void)InstallAuthorScene(*fixed.Get(), path);
				return;
			}
			if (type == "prefab" || type == "script" || type == "behavior_graph" || type == "material_definition" ||
			    ext == ".matgraph") {
				if (OpenAuthor(build.Recipe()->source)) {
					if (auto* doc = dynamic_cast<AssetDocument*>(m_Documents.Active()); doc && type != "scene") {
						doc->SetSourceIdentity(ext == ".matgraph" ? build.Result().primary : id);
						doc->AttachRecipe(build.Recipe());
						doc->SetCookReport(build.Result());
					}
				}
				return;
			}
		}
		if (type == "environment") {
			if (!assets.Acquire(id, environment, fallback, m_Error) || fallback) {
				m_Error = "The selected environment is not loaded";
				return;
			}
			auto* scene = ActiveScene();
			const auto entity = scene ? m_Selection.Resolve(*scene) : EntityId{};
			if (!scene || !entity.HasValue()) {
				m_Error = "Select a scene entity before assigning an environment";
				return;
			}
			EnvironmentComponent value;
			if (scene->Resolve()->GetWorld().Has<EnvironmentComponent>(entity))
				if (!scene->Resolve()->GetWorld().Get(entity, value).IsSuccess()) {
					m_Error = "Environment entity is unavailable";
					return;
				}
			value.environment = AssetHandle<EnvironmentAsset>{id};
			if (EditEnvironment(*scene, scene->Resolve()->MakeReference(entity).persistent, value, m_Error))
				m_Documents.Activate(scene->Id());
			return;
		}
		if (auto* document = m_AssetEditors.Open(id, type, assets, m_Documents, m_Error)) {
			ActivateAsset(*document);
			document->AttachRecipe(build.Recipe());
			document->SetCookReport(build.Result());
			m_OpenAsset = document->Id();
			m_AskDiscard = false;
			m_Error.clear();
		} else if (m_Error.empty())
			m_Error = "Select a loaded material instance or image";
	}

	/**
	 * @brief 将视口悬停、工具操纵状态与当前帧输入转换为编辑相机输入。
	 *
	 * @param[in] hovered 指针是否位于视口的场景图像内。
	 * @param[in] gizmoBusy 变换操纵器是否正在处理手势。
	 * @param[in] canAdjustSpeed 当前视图是否允许通过中键改变移动速度。
	 *
	 * @return 当前帧的编辑相机输入；视口或输入焦点不允许导航时，各移动、旋转和滚轮分量保持默认值。
	 */
	EditorCameraInput WorkspaceEditing::ViewportInput(bool hovered, bool gizmoBusy, bool canAdjustSpeed) {
		EditorCameraInput input;
		const auto& io = ImGui::GetIO();
		if (!hovered || gizmoBusy || ImGui::IsAnyItemActive() || ImGui::GetDragDropPayload() || io.WantTextInput ||
		    io.AppFocusLost || io.KeyCtrl || io.KeyAlt || io.KeySuper) {
			m_RightLook = false;
			return input;
		}

		/*
		 * 滚轮跟随悬停的图像；其他面板已消费的滚动，以及 Shift 横向滚动，不驱动相机。
		 * 键盘导航和右键转向仍要求视口获得焦点。
		 */
		const bool wheelAvailable = io.MouseWheel != 0 && !io.KeyShift &&
		    ImGui::GetCurrentContext()->WheelingWindowScrolledFrame != ImGui::GetFrameCount() &&
		    ImGui::TestKeyOwner(ImGuiKey_MouseWheelY, ImGui::GetCurrentWindowRead()->ID);
		const bool middleDown = canAdjustSpeed && ImGui::IsMouseDown(ImGuiMouseButton_Middle);
		if (wheelAvailable && !middleDown)
			input.wheel = io.MouseWheel;

		const bool rightPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
		const bool middlePressed = middleDown && ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
		if (rightPressed || middlePressed)
			ImGui::SetWindowFocus();
		if (!ImGui::IsWindowFocused()) {
			m_RightLook = false;
			return input;
		}
		m_App.AllowEditorViewportSystemKeys();
		const auto axis = [](ImGuiKey positive, ImGuiKey negative) {
			return float(ImGui::IsKeyDown(positive)) - float(ImGui::IsKeyDown(negative));
		};
		input.seconds = io.DeltaTime;
		const int frame = ImGui::GetFrameCount();
		const bool adjustingSpeed = middleDown && (middlePressed || m_SpeedGestureFrame == frame - 1);
		if (adjustingSpeed) {
			m_SpeedGestureFrame = frame;
			/* 忽略按下前的鼠标位移。上移 80 个逻辑像素或滚轮向上 4 格使速度翻倍。 */
			const double octaves = (middlePressed ? 0.0 : -double(io.MouseDelta.y) / 80.0) +
			    (wheelAvailable ? double(io.MouseWheel) / 4.0 : 0.0);
			if (octaves != 0 && std::isfinite(octaves)) {
				const double value = std::log2(double(m_ViewportSettings.cameraSpeed)) + octaves;
				m_ViewportSettings.cameraSpeed =
				    static_cast<float>(std::exp2(std::clamp(value, std::log2(double(ViewportSettings::MinCameraSpeed)),
				        std::log2(double(ViewportSettings::MaxCameraSpeed)))));
			}
		}
		input.speedMetresPerSecond = m_ViewportSettings.cameraSpeed;
		input.right = axis(ImGuiKey_D, ImGuiKey_A);
		input.forward = axis(ImGuiKey_W, ImGuiKey_S);
		input.up = axis(ImGuiKey_E, ImGuiKey_Q);
		if (rightPressed)
			m_RightLook = true;
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
			m_RightLook = false;
		if (m_RightLook && !rightPressed && !adjustingSpeed) {
			input.lookX = io.MouseDelta.x;
			input.lookY = io.MouseDelta.y;
		}
		return input;
	}

	void WorkspaceEditing::BlurPlayUI() {
		m_App.GetWindow().ClearTextInputTarget(SceneSurface);
		if (m_PlayUIActive || m_PlayUICapture) {
			UIInputEvent blur;
			blur.type = UIInputEvent::Type::Blur;
			bool ignored = false;
			m_App.RouteGameUIInput(blur, ignored, m_Error);
		}
		m_PlayUIActive = false;
		m_PlayUICapture = false;
	}

	/**
	 * @brief 转发运行模式界面输入。
	 *
	 * @param[in] x 视口图像左边缘的 ImGui 屏幕坐标。
	 * @param[in] y 视口图像上边缘的 ImGui 屏幕坐标。
	 * @param[in] width 视口图像在 ImGui 中的显示宽度。
	 * @param[in] height 视口图像在 ImGui 中的显示高度。
	 * @param[in] extent 游戏 UI 绘制目标的实际像素尺寸。
	 * @param[in] hovered 指针是否悬停在该视口图像上。
	 * @param[in] focused 视口所在工作区窗口是否有焦点。
	 *
	 * @return 本帧游戏界面已消费输入、文本输入正在进行或转发出错时为 true，调用方应暂停视口导航。
	 */
	bool WorkspaceEditing::RoutePlayUIInput(
	    float x, float y, float width, float height, Extent2D extent, bool hovered, bool focused) {
		const auto& io = ImGui::GetIO();
		if (io.AppFocusLost || (!hovered && !focused && !m_PlayUICapture)) {
			BlurPlayUI();
			return false;
		}
		bool blocked = false;
		const auto submit = [&](UIInputEvent event) {
			bool consumed = false;
			if (!m_App.RouteGameUIInput(event, consumed, m_Error))
				return false;
			blocked |= consumed;
			return true;
		};
		const int modifiers =
		    (io.KeyCtrl ? 1 : 0) | (io.KeyShift ? 2 : 0) | (io.KeyAlt ? 4 : 0) | (io.KeySuper ? 8 : 0);
		if (hovered || m_PlayUICapture) {
			UIInputEvent move;
			move.type = UIInputEvent::Type::Move;
			move.modifiers = modifiers;
			move.x = (io.MousePos.x - x) / width * extent.width;
			move.y = (io.MousePos.y - y) / height * extent.height;
			if (!submit(move))
				return true;
			for (int button = 0; button < 3; ++button) {
				if (hovered && ImGui::IsMouseClicked(button)) {
					UIInputEvent event;
					event.type = UIInputEvent::Type::ButtonDown;
					event.key = button;
					event.modifiers = modifiers;
					if (!submit(event))
						return true;
					if (blocked)
						m_PlayUICapture = true;
				}
				if (ImGui::IsMouseReleased(button)) {
					UIInputEvent event;
					event.type = UIInputEvent::Type::ButtonUp;
					event.key = button;
					event.modifiers = modifiers;
					if (!submit(event))
						return true;
				}
			}
			if (hovered && (io.MouseWheel != 0 || io.MouseWheelH != 0)) {
				UIInputEvent wheel;
				wheel.type = UIInputEvent::Type::Wheel;
				wheel.x = -io.MouseWheelH;
				wheel.y = -io.MouseWheel;
				wheel.modifiers = modifiers;
				if (!submit(wheel))
					return true;
			}
		}
		if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(1) && !ImGui::IsMouseDown(2))
			m_PlayUICapture = false;
		if (focused && !ImGui::IsAnyItemActive()) {
			const auto key = [&](ImGuiKey imgui, int platform) {
				if (!ImGui::IsKeyPressed(imgui, true) && !ImGui::IsKeyReleased(imgui))
					return true;
				UIInputEvent event;
				event.type = ImGui::IsKeyReleased(imgui) ? UIInputEvent::Type::KeyUp : UIInputEvent::Type::KeyDown;
				event.key = UIInputRouter::TranslatePlatformKey(platform);
				event.modifiers = modifiers;
				return submit(event);
			};
			for (int i = 0; i < 26; ++i)
				if (!key(ImGuiKey(ImGuiKey_A + i), Key::A + i))
					return true;
			for (int i = 0; i < 10; ++i)
				if (!key(ImGuiKey(ImGuiKey_0 + i), Key::D0 + i))
					return true;
			const std::pair<ImGuiKey, int> navigation[]{{ImGuiKey_Space, Key::Space}, {ImGuiKey_Enter, Key::Enter},
			    {ImGuiKey_Tab, Key::Tab}, {ImGuiKey_Backspace, Key::Backspace}, {ImGuiKey_Delete, Key::Delete},
			    {ImGuiKey_Escape, Key::Escape}, {ImGuiKey_LeftArrow, Key::Left}, {ImGuiKey_RightArrow, Key::Right},
			    {ImGuiKey_UpArrow, Key::Up}, {ImGuiKey_DownArrow, Key::Down}, {ImGuiKey_Home, Key::Home},
			    {ImGuiKey_End, Key::End}, {ImGuiKey_PageUp, Key::PageUp}, {ImGuiKey_PageDown, Key::PageDown},
			    {ImGuiKey_Insert, Key::Insert}};
			for (const auto& [imgui, platform] : navigation)
				if (!key(imgui, platform))
					return true;
			for (const auto codepoint : io.InputQueueCharacters) {
				UIInputEvent event;
				event.type = UIInputEvent::Type::Text;
				event.codepoint = uint32_t(codepoint);
				event.modifiers = modifiers;
				if (!submit(event))
					return true;
			}
		}
		const auto caret = m_App.GetGameUITextInput();
		if (focused && caret.active && !io.WantTextInput) {
			TextInputSnapshot region = caret;
			const auto mainPosition = ImGui::GetMainViewport()->Pos;
			region.x = x - mainPosition.x + caret.x / extent.width * width;
			region.y = y - mainPosition.y + caret.y / extent.height * height;
			region.width = caret.width / extent.width * width;
			region.height = caret.height / extent.height * height;
			auto* application = &m_App;
			if (!m_App.GetWindow().SetTextInputTarget(
			        SceneSurface, region,
			        [application](const TextComposition& composition) {
				        UIInputEvent input;
				        input.type = UIInputEvent::Type::Composition;
				        input.composition = composition;
				        bool consumed = false;
				        std::string error;
				        if (!application->RouteGameUIInput(input, consumed, error) && !error.empty())
					        CAT_CORE_WARN("Game UI IME: {0}", error);
			        },
			        m_Error))
				return true;
		} else
			m_App.GetWindow().ClearTextInputTarget(SceneSurface);
		m_PlayUIActive = blocked || m_PlayUICapture || caret.active;
		return blocked || caret.active;
	}

	bool WorkspaceEditing::PickViewport(
	    SceneDocument& doc, const RenderSurfaceSnapshot& frame, uint32_t x, uint32_t y, std::string& error) {
		if (doc.Playing()) {
			error = "Cannot edit selection during Play";
			return false;
		}
		return m_Picking.Request(m_App, doc, frame, x, y, error);
	}

	ImportPlacement WorkspaceEditing::MakeImportPlacement(float x, float y) {
		if (!m_Project || m_GameView || m_PlayDocument.HasValue() || !m_DropDocument.HasValue() || m_DropRect.z <= 0 ||
		    m_DropRect.w <= 0 || x < m_DropRect.x || y < m_DropRect.y || x >= m_DropRect.x + m_DropRect.z ||
		    y >= m_DropRect.y + m_DropRect.w)
			return {};
		auto* doc = ActiveScene();
		RenderSurfaceSnapshot frame;
		Math::Vec3 position;
		if (!doc || doc->Id() != m_DropDocument || !doc->Resolve() || !m_App.GetEditorPreview(SceneSurface, frame) ||
		    !frame.view.independentCamera || frame.sceneIdentity != doc->Resolve()->GetWorld().GetIdentity() ||
		    !FindViewportPlacement(*doc->Resolve(), frame.view, (x - m_DropRect.x) / m_DropRect.z,
		        (y - m_DropRect.y) / m_DropRect.w, position))
			return {};
		const auto project = m_Project->id;
		const auto epoch = m_DropEpoch;
		const auto world = frame.sceneIdentity;
		return [this, project, epoch, world, position](
		           BuildPanel& build, const std::filesystem::path& path, std::string& error) {
			const auto ext = Tools::AssetSourceExtension(path);
			if (ext != ".gltf" && ext != ".glb" && ext != ".fbx" && ext != ".obj" && ext != ".prefab")
				return true;
			auto* active = ActiveScene();
			if (!m_Project || m_Project->id != project || epoch != m_DropEpoch || !active || active->Playing() ||
			    !active->Resolve() || active->Resolve()->GetWorld().GetIdentity() != world) {
				error = "Source imported; its original author scene is no longer active";
				return false;
			}
			PersistentEntityId root;
			if (ext == ".prefab") {
				if (!InstantiateAuthorPrefab(
				        *active, m_App.GetAssets(), build.Result().primary, {}, root, error, position))
					return false;
			} else if (!build.PlaceModel(*active, position, root, error, build.Result().primary))
				return false;
			m_Selection.Select(*active, active->Resolve()->Resolve(root));
			m_Documents.Activate(active->Id());
			return true;
		};
	}

	/**
	 * @brief 绘制视口。
	 *
	 * @param[in,out] doc 当前编辑的场景文档。
	 * @param[in] text 根据翻译键生成当前语言的界面文案。
	 * @param[in] centerDockId 主窗口中央停靠节点的标识。
	 */
	void WorkspaceEditing::DrawViewport(SceneDocument* doc, const PanelText& text, uint32_t centerDockId) {
		m_DropRect = {};
		const StableId dropDocument = doc && !doc->Playing() ? doc->Id() : StableId{};
		if (m_DropDocument != dropDocument) {
			++m_DropEpoch;
			m_DropDocument = dropDocument;
		}
		/* 仅迁移这个稳定窗口：旧版保存的浮动位置或侧边 DockId 不影响其他面板。 */
		if (centerDockId)
			ImGui::SetNextWindowDockID(centerDockId, ImGuiCond_Always);
		ImGuiWindowClass viewportClass;
		viewportClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoDockingSplit |
		    ImGuiDockNodeFlags_NoDockingOverMe | ImGuiDockNodeFlags_NoWindowMenuButton;
		ImGui::SetNextWindowClass(&viewportClass);
		const bool shown = ImGui::Begin(text("panel.viewport", "Scene Viewport", "Catalpa.Viewport").c_str(), nullptr,
		    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse);
		if (!shown || !doc) {
			BlurPlayUI();
			m_Picking.Cancel(m_App);
			m_Gizmo.Cancel(m_Error);
			m_RightLook = false;
			m_App.CloseEditorPreview(SceneSurface);
			if (shown)
				ImGui::TextDisabled("%s", text("viewport.no_scene", "No active scene.", nullptr).c_str());
			ImGui::End();
			return;
		}
		if (ImGui::IsWindowFocused())
			m_Documents.Activate(doc->Id());
		if (ImGui::Selectable(text("viewport.scene_tab", "Scene", "SceneTab").c_str(), !m_GameView, 0,
		        {ImGui::GetFontSize() * 4, 0})) {
			m_GameView = false;
			BlurPlayUI();
		}
		ImGui::SameLine();
		if (ImGui::Selectable(
		        text("viewport.game_tab", "Game", "GameTab").c_str(), m_GameView, 0, {ImGui::GetFontSize() * 4, 0})) {
			m_GameView = true;
			m_Picking.Cancel(m_App);
			m_Gizmo.Cancel(m_Error);
		}
		const bool gameView = m_GameView;
		const auto renderHandle = doc->Playing() ? doc->PlayHandle() : doc->Handle();
		if (!gameView) {
			const bool wide = ImGui::GetContentRegionAvail().x >= ImGui::GetFrameHeight() * 20;
			if (wide) {
				DrawViewModes(m_Camera, m_ShowNormals, m_Error, text);
				ImGui::SameLine();
			}
			ImGui::BeginDisabled(doc->Playing());
			m_Gizmo.Toolbar(text, m_ViewportSettings);
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ToolbarButton(
			        "##grid", ToolIcon::Grid, text("viewport.grid", "Ground grid (1 m)", nullptr), m_ShowGrid))
				m_ShowGrid = !m_ShowGrid;
			ImGui::SameLine();
			if (ToolbarButton(
			        "##viewOptions", ToolIcon::Stats, text("viewport.options", "View options and statistics", nullptr)))
				ImGui::OpenPopup("ViewportOptions");
			if (ImGui::BeginPopup("ViewportOptions")) {
				if (!wide) {
					DrawViewModes(m_Camera, m_ShowNormals, m_Error, text);
					ImGui::Separator();
				}
				double gpuMs;
				if (m_App.GetGpuFrameMilliseconds(gpuMs))
					ImGui::Text("%s: %.2f", text("viewport.gpu_time", "GPU frame (ms)", nullptr).c_str(), gpuMs);
				uint64_t payload, budget;
				if (m_App.GetManagedGpuPayloadBytes(payload, budget))
					ImGui::Text("%s: %.1f / %.1f MiB",
					    text("viewport.gpu_payload", "Managed GPU payload", nullptr).c_str(),
					    double(payload) / (1024 * 1024), double(budget) / (1024 * 1024));
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			if (ToolbarButton("##resetView", ToolIcon::Reset, text("viewport.reset_camera", "Reset view", nullptr))) {
				m_Gizmo.Cancel(m_Error);
				m_RightLook = false;
				m_Picking.Cancel(m_App);
				m_ResetCamera = true;
			}
			ImGui::BeginDisabled(doc->Playing());
			DrawCameraSpeed(m_ViewportSettings.cameraSpeed, text);
			ImGui::EndDisabled();
		}

		const auto available = ImGui::GetContentRegionAvail(), origin = ImGui::GetCursorScreenPos();
		const auto density = ImGui::GetWindowViewport()->FramebufferScale;
		if (available.x < 1 || available.y < 1) {
			BlurPlayUI();
			m_Picking.Cancel(m_App);
			m_Gizmo.Cancel(m_Error);
			m_RightLook = false;
			m_App.CloseEditorPreview(SceneSurface);
			ImGui::End();
			return;
		}
		const Extent2D extent{uint32_t(std::clamp(available.x * density.x, 1.f, 4096.f)),
		    uint32_t(std::clamp(available.y * density.y, 1.f, 4096.f))};
		RenderSurfaceSnapshot frame;
		const auto* renderedScene = m_App.GetScenes().GetScene(renderHandle);
		const bool ready = renderedScene && (gameView || !m_ResetCamera) &&
		    m_App.GetEditorPreview(SceneSurface, frame) && frame.textureToken && frame.extent == extent &&
		    frame.view.independentCamera != gameView && frame.sceneIdentity == renderedScene->GetWorld().GetIdentity();
		if (!doc->Playing() && !gameView)
			m_Picking.Update(m_App, doc, ready ? &frame : nullptr, m_Selection, m_Error);
		EditorCameraInput input;
		if (ready) {
			ImGui::Image(ImTextureRef(static_cast<ImTextureID>(frame.textureToken)), available);
			const bool hovered = ImGui::IsItemHovered(), clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
			if (!doc->Playing() && !gameView) {
				const auto mainOrigin = ImGui::GetMainViewport()->Pos;
				m_DropRect = {origin.x - mainOrigin.x, origin.y - mainOrigin.y, available.x, available.y};
			}
			if (!doc->Playing() && !gameView && ImGui::BeginDragDropTarget()) {
				if (const auto* payload =
				        ImGui::AcceptDragDropPayload("CATALPA_SOURCE", ImGuiDragDropFlags_AcceptBeforeDelivery);
				    payload && payload->DataSize > 1 &&
				    static_cast<const char*>(payload->Data)[payload->DataSize - 1] == 0) {
					const auto mouse = ImGui::GetMousePos();
					Math::Vec3 placement;
					if (FindViewportPlacement(*doc->Resolve(), frame.view, (mouse.x - origin.x) / available.x,
					        (mouse.y - origin.y) / available.y, placement)) {
						auto* draw = ImGui::GetWindowDrawList();
						const auto projected = frame.view.matrices.viewProjection * Math::Vec4(placement, 1);
						if (projected.w > 1e-6f) {
							const ImVec2 marker{origin.x + (projected.x / projected.w + 1) * .5f * available.x,
							    origin.y + (projected.y / projected.w + 1) * .5f * available.y};
							draw->AddCircle(marker, ImGui::GetFontSize(), IM_COL32(53, 182, 129, 255), 24, 2);
						}
						if (payload->IsDelivery() && !ImGui::IsKeyDown(ImGuiKey_Escape))
							DropSourceIntoScene(
							    std::filesystem::u8path(static_cast<const char*>(payload->Data)), placement);
					}
				}
				ImGui::EndDragDropTarget();
			}
			if (gameView) {
				if (clicked)
					ImGui::SetWindowFocus();
				const bool uiConsumed = doc->Playing() &&
				    RoutePlayUIInput(
				        origin.x, origin.y, available.x, available.y, extent, hovered, ImGui::IsWindowFocused());
				if (doc->Playing() && !uiConsumed && hovered && ImGui::IsWindowFocused() &&
				    !ImGui::GetIO().WantTextInput)
					m_App.AllowEditorPlayInput();
				(void)m_App.SetEditorPreview(renderHandle, SceneSurface, extent, m_Error);
				ImGui::End();
				return;
			}
			BlurPlayUI();
			bool overAxes = false;
			const int axis = DrawOrientation(frame.view, origin, available, hovered, clicked, overAxes, text);
			if (axis) {
				auto pivot = m_Camera.Pose().translation + m_Camera.Pose().rotation * Math::Vec3{0, 0, -5};
				TransformComponent transform;
				const auto selected = m_Selection.Resolve(*doc);
				if (selected.HasValue() && renderedScene->GetTransform(selected, transform).IsSuccess() &&
				    transform.worldValid)
					pivot = Math::Vec3(transform.worldMatrix[3]);
				m_Gizmo.Cancel(m_Error);
				m_Picking.Cancel(m_App);
				m_Camera.AlignAxis(std::abs(axis) - 1, axis > 0 ? 1.f : -1.f, pivot, m_Error);
			}
			const bool gizmo = !doc->Playing() && (!overAxes || ImGuizmo::IsUsing()) &&
			    m_Gizmo.Draw(*doc, m_Selection, frame.view, origin.x, origin.y, available.x, available.y, m_Error,
			        m_ViewportSettings);
			input = ViewportInput(hovered && !overAxes, gizmo, !doc->Playing());
			if (!doc->Playing() && clicked && !gizmo && !overAxes) {
				const auto mouse = ImGui::GetIO().MousePos;
				const auto x = uint32_t(
				    std::clamp((mouse.x - origin.x) / available.x * extent.width, 0.f, float(extent.width - 1)));
				const auto y = uint32_t(
				    std::clamp((mouse.y - origin.y) / available.y * extent.height, 0.f, float(extent.height - 1)));
				PickViewport(*doc, frame, x, y, m_Error);
			}
		} else {
			BlurPlayUI();
			m_RightLook = false;
			m_Gizmo.Cancel(m_Error);
			ImGui::TextDisabled("%s", text("viewport.preparing", "Preparing scene image...", nullptr).c_str());
		}

		auto next = m_Camera;
		if (gameView) {
			(void)m_App.SetEditorPreview(renderHandle, SceneSurface, extent, m_Error);
			ImGui::End();
			return;
		}
		std::string viewError;
		if (m_ResetCamera) {
			/* 用真实输出尺寸筛选可作为导航起点的相机，坏的场景镜头不能锁住编辑视口。 */
			if (!next.Reset(renderedScene, viewError, {extent.width, extent.height})) {
				m_Error = std::move(viewError);
				ImGui::End();
				return;
			}
			if (!viewError.empty())
				m_Error = viewError;
		}
		EditorViewDisplay display;
		display.showNormals = m_ShowNormals;
		display.showGrid = m_ShowGrid;
		const auto selected = m_Selection.Resolve(*doc);
		if (!doc->Playing() && selected.HasValue())
			display.selected = {selected.world, selected.index, selected.generation};
		if (next.Navigate(input, viewError) &&
		    m_App.SetEditorCameraPreview(renderHandle, SceneSurface, extent, next.Pose(), next.Lens(),
		        next.HistoryRevision(), viewError, display)) {
			m_Camera = next;
			m_ResetCamera = false;
		} else {
			m_RightLook = false;
			m_Error = std::move(viewError);
		}
		ImGui::End();
	}

	void WorkspaceEditing::Draw(const PanelText& text, BuildPanel* build, uint32_t centerDockId) {
		UpdateDocuments();
		m_Documents.CloseStaleScenes();
		auto* scene = ActiveScene();
		if (scene && scene->Playing()) {
			SceneHandle candidate;
			std::string switchError;
			if (m_App.PollScriptSceneSwitch(candidate, switchError)) {
				const auto previous = scene->PlayHandle();
				if (!m_App.SetEditorPlayScene(candidate, m_PlayPaused, switchError) ||
				    !scene->AdoptPlayScene(candidate, switchError)) {
					(void)m_App.SetEditorPlayScene(previous, m_PlayPaused, m_Error);
					(void)m_App.GetScenes().Unload(candidate);
					m_Error = std::move(switchError);
				} else {
					m_Picking.Cancel(m_App);
					m_Gizmo.Cancel(m_Error);
					BlurPlayUI();
				}
			} else if (!switchError.empty())
				m_Error = std::move(switchError);
		}
		const auto id = scene ? scene->Id() : StableId{};
		if (m_LastScene != id) {
			m_OverrideRoot = {};
			m_OverridePaths.clear();
			m_Selection.Clear();
			m_Picking.Cancel(m_App);
			m_Gizmo.Cancel(m_Error);
			m_LastScene = id;
			m_RightLook = false;
			if (scene) {
				m_ResetCamera = true;
				m_Documents.Activate(id);
			}
		}
		ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
		/* ImGuizmo 在 BeginFrame 中创建透明的全屏辅助窗口；它应复用主窗口。 */
		ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
		ImGuizmo::BeginFrame();
		DrawViewport(scene, text, centerDockId);
		if (auto* asset = dynamic_cast<AssetDocument*>(m_Documents.Find(m_OpenAsset))) {
			bool open = true;
			bool discard = false;
			if (ImGui::Begin(text("panel.asset_editor", "Asset Editor", "Catalpa.AssetEditor").c_str(), &open)) {
				if (ImGui::IsWindowFocused() && m_Documents.Active() != asset)
					ActivateAsset(*asset);
				ImGui::AlignTextToFramePadding();
				const char* draftsText = UiText("document.assets", "Open drafts");
				ImGui::TextUnformatted(draftsText);
				const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
				const float remaining = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(draftsText).x - spacing;
				if (remaining >= ImGui::GetFontSize() * 8.f)
					ImGui::SameLine(0, spacing);
				ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 27.f));
				const bool choices = ImGui::BeginCombo("###OpenDrafts", DraftTitle(*asset).c_str());
				if (choices) {
					std::map<std::string, unsigned> names;
					std::map<std::string_view, unsigned> types;
					for (auto* candidate : m_Documents.Assets()) {
						auto label = DraftTitle(*candidate);
						const auto count = ++names[label];
						if (count > 1)
							label += " (" + std::to_string(count) + ")";
						const auto type = candidate->TypeKey();
						label += "###" + std::string(type) + std::to_string(++types[type]);
						if (ImGui::Selectable(label.c_str(), candidate == asset)) {
							CancelPendingOpen();
							m_OpenAsset = candidate->Id();
							asset = candidate;
							ActivateAsset(*asset);
							m_AskDiscard = false;
						}
						DraftTooltip(*candidate);
					}
					ImGui::EndCombo();
				} else
					DraftTooltip(*asset);
				if (auto material = asset->Material()) {
					if (material->definition.Get()->shadingModel == MaterialShadingModel::Unlit) {
						const auto* base = FindMaterialParameter(*material, "base_color");
						const auto* rgba = base ? std::get_if<std::array<float, 4>>(base) : nullptr;
						const bool plain = IsStandardUnlit(*material);
						const bool hdr = rgba && *std::max_element(rgba->begin(), rgba->begin() + 3) > 1.f;
						const bool copy = plain && rgba && !hdr;
						if (plain && hdr)
							ImGui::TextWrapped("%s",
							    UiText("material.pbr_copy_hdr",
							        "PBR base RGB must be in [0,1]. Create a blank PBR material and use emission for HDR colors."));
						ImGui::BeginDisabled(m_PlayDocument.HasValue());
						const auto button = copy
						    ? text("material.pbr_copy", "Create PBR copy from base color", "CreatePbrCopy")
						    : text("material.pbr_empty", "Create blank PBR material", "CreateEmptyPbr");
						if (ImGui::Button(button.c_str())) {
							CreatePbrDraft(copy ? material.get() : nullptr);
							if (auto* next = dynamic_cast<AssetDocument*>(m_Documents.Find(m_OpenAsset)))
								asset = next;
						}
						ImGui::SetItemTooltip("%s",
						    copy
						        ? UiText("material.pbr_copy_hint",
						              "The new draft inherits base color and face culling. The original material and object bindings are preserved.")
						        : UiText("material.pbr_empty_hint",
						              "Create a separate standard PBR draft. Parameters, textures and bindings remain with the original material."));
						ImGui::EndDisabled();
					}
					if (!m_Project)
						ImGui::TextWrapped("%s",
						    UiText("material.session_draft",
						        "Temporary session. To save a material, open an author project before creating it."));
				}
				m_AssetEditors.Draw(m_App, *asset, text, m_Error);
				DrawImportSettings(*asset, build, text, m_Error);
				if (m_AskDiscard) {
					if (ImGui::Button(UiLabel("ui.save_and_close", "Save and close").c_str()) && SaveDocument(*asset))
						discard = true;
					ImGui::SameLine();
					if (ImGui::Button(text("document.discard", "Discard draft and close", "Discard").c_str()))
						discard = true;
					ImGui::SameLine();
					if (ImGui::Button(text("document.keep", "Keep editing", "KeepEdit").c_str()))
						m_AskDiscard = false;
				}
			}
			ImGui::End();
			if (!open || discard) {
				if (asset->History().Dirty() && !discard)
					m_AskDiscard = true;
				else {
					CloseDocument(m_OpenAsset);
					const auto remaining = m_Documents.Assets();
					if (!remaining.empty()) {
						m_OpenAsset = remaining.front()->Id();
						ActivateAsset(*remaining.front());
					}
					m_AskDiscard = false;
				}
			}
		}
		if (!m_Error.empty()) {
			if (ImGui::Begin(text("edit.diagnostic", "Edit diagnostic", "Catalpa.EditDiagnostic").c_str())) {
				ImGui::TextWrapped("%s", m_Error.c_str());
				if (ImGui::Button(text("edit.dismiss", "Dismiss", "Dismiss").c_str()))
					m_Error.clear();
			}
			ImGui::End();
		}
	}
}
