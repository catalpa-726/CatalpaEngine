#include "catpch.h"

#include "TestRunner.h"
#include "Assets/AssetTestFixture.h"
#include "Assets/BuiltinAssetIds.h"
#include "AssetPipeline/AssetBuildContext.h"
#include "Camera/CameraProjection.h"
#include "Commands/SceneCommands.h"
#include "Geometry/PrimitiveMesh.h"
#include "Materials/MaterialValidation.h"
#include "Project/ProjectCreation.h"
#include "Runtime/Systems/RenderExtractSystem.h"
#include "Runtime/Systems/TransformSystem.h"
#include "Scene/SceneSerializer.h"
#include "Scene/SceneSubtree.h"
#include "Viewport/EditorCameraController.h"
#include "Viewport/ViewportPlacement.h"

namespace Catalpa::Tests {
	namespace {
		/** @brief 用新项目实际生成的场景核对默认天空、作者覆盖和显式禁用的呈现结果。 */
		void CheckProjectSky(const SceneAsset& saved, TestContext& t) {
			SceneManager scenes;
			SceneHandle handle;
			std::string error;
			if (!SceneSerializer::Instantiate(saved, scenes, nullptr, handle, error)) {
				t.Check(false, "Instantiate starter scene for sky rendering: " + error);
				return;
			}
			auto& scene = *scenes.GetScene(handle);
			RenderFramePacket editor, game;
			const auto defaultSky = [](const RenderFramePacket& frame) {
				return frame.environment && frame.environment->asset.id == BuiltinAssetIds::DefaultEnvironment &&
				    frame.environment->settings.drawBackground;
			};
			t.Check(TransformSystem::Update(scene).IsSuccess() &&
			        RenderExtractSystem::Extract(scene, 1, std::span<const RenderViewRequest>{}, editor).IsSuccess() &&
			        RenderExtractSystem::Extract(scene, 2, 640, 480, game).IsSuccess() && game.views.size() == 1 &&
			        defaultSky(editor) && defaultSky(game),
			    "Editor and game camera extraction both select the default sky of a new project");
			SceneAsset snapshot;
			t.Check(SceneSerializer::Capture(scene, snapshot, error) &&
			        std::none_of(snapshot.entities.begin(), snapshot.entities.end(),
			            [](const auto& entity) { return entity.environment.has_value(); }),
			    "Rendering the default sky keeps the saved author scene free of implicit environment components");
			EnvironmentComponent custom;
			custom.environment.id = AssetTest::Id(701);
			custom.settings.drawBackground = false;
			const auto owner = scene.Resolve(saved.entities.front().id);
			t.Check(scene.QueueSetEnvironment(owner, custom).IsSuccess() && scene.CommitPending().IsSuccess() &&
			        RenderExtractSystem::Extract(scene, 3, 640, 480, game).IsSuccess() && game.environment &&
			        game.environment->asset.id == custom.environment.id && !game.environment->settings.drawBackground,
			    "An authored environment can light a scene while keeping its background hidden");
			custom.enabled = false;
			t.Check(scene.QueueSetEnvironment(owner, custom).IsSuccess() && scene.CommitPending().IsSuccess() &&
			        RenderExtractSystem::Extract(scene, 4, 640, 480, game).IsSuccess() && !game.environment,
			    "Disabling an authored environment does not reactivate the default sky");
		}

		/**
		 * @brief 通过正式项目会话打开新工程，验证入口登记与场景源数据对应。
		 * @param[in,out] t 接收文件生成、重开、目录扫描和失败保留结果。
		 */
		void CheckProjectWorkflow(TestContext& t) {
			AssetTest::Fixture temp;
			std::string error;
			if (!temp.Create(error)) {
				t.Check(false, error);
				return;
			}
			const std::string name = "工作室 Sandbox";
			const auto folder = temp.root / std::filesystem::u8path(name);
			const auto file = folder / std::filesystem::u8path(name + ".catalpaproj");
			const auto content = folder / "Content";
			const auto second = folder / "Alternate.catalpaproj";
			temp.files = {second, file, folder / "BuildProfiles.json", content / "asset_metadata.json",
			    content / "Scenes/Main.scene", content / "Scenes", content, folder};
			std::filesystem::path opened;
			if (!Tools::CreateProject(temp.root, name, opened, error)) {
				t.Check(false, "Create starter project: " + error);
				return;
			}
			std::unique_ptr<Tools::ProjectSession> session;
			const bool loaded =
			    opened == file && Tools::LoadProject(file, session, error) && session->config.startupScene.has_value();
			t.Check(loaded, "Reopen Unicode project and read an explicit startup asset");
			if (!loaded)
				return;
			const auto projectId = session->config.id;
			const auto startup = *session->config.startupScene;
			Tools::AssetBuildRequest cookRequest;
			cookRequest.project = session->config;
			cookRequest.source = content / "Scenes/Main.scene";
			cookRequest.shaderRoot = temp.root / "Shaders";
			temp.files.push_back(cookRequest.shaderRoot);
			std::error_code shaderError;
			std::filesystem::create_directory(cookRequest.shaderRoot, shaderError);
			Tools::AssetBuildContext cook(std::move(cookRequest));
			t.Check(!shaderError && cook.Initialize(error) && startup == cook.Identity("scene"),
			    "Starter metadata and the cooker identify the same runtime startup scene");
			VirtualPath path;
			std::vector<std::byte> bytes;
			SceneAsset scene;
			const bool bound = VirtualPath::Parse("content:/Scenes/Main.scene", path, error) &&
			    session->assets.Find(startup) && session->assets.Find(path) == session->assets.Find(startup) &&
			    session->files.ReadFile(path, 1024 * 1024, bytes, error) &&
			    SceneSerializer::Read({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, scene, error);
			t.Check(bound && scene.name == "Main" &&
			        std::any_of(scene.entities.begin(), scene.entities.end(),
			            [](const auto& entity) { return entity.camera && entity.camera->enabled; }),
			    "Startup metadata resolves to the authored scene with an enabled camera");
			if (bound && !scene.entities.empty())
				CheckProjectSky(scene, t);
			session.reset();
			const auto untouched = temp.root / "unchanged";
			opened = untouched;
			t.Check(!Tools::CreateProject(temp.root, name, opened, error) && opened == untouched &&
			        Tools::LoadProject(file, session, error) && session->config.id == projectId &&
			        session->config.startupScene == startup,
			    "Existing destination is preserved with its original project and startup identities");
			t.Check(!Tools::CreateProject(temp.root, "../escape", opened, error) && opened == untouched,
			    "Invalid project directory name does not publish any result");

			std::vector<std::filesystem::path> found;
			t.Check(Tools::FindProjectFiles(temp.root, found, error) && found.empty(),
			    "Directory import does not search nested projects");
			t.Check(Tools::FindProjectFiles(folder, found, error) && found == std::vector{file},
			    "Import identifies the single project in its root");
			std::error_code ec;
			std::filesystem::copy_file(file, second, ec);
			t.Check(!ec && Tools::FindProjectFiles(folder, found, error) && found.size() == 2 &&
			        std::is_sorted(found.begin(), found.end()) &&
			        std::find(found.begin(), found.end(), file) != found.end() &&
			        std::find(found.begin(), found.end(), second) != found.end(),
			    "Import returns both projects for an explicit choice");
		}

		/**
		 * @brief 在真实作者场景上检查整棵子树的快照、引用、历史及冲突处理。
		 * @param[in,out] t 接收复制、连续粘贴和撤销、重做的断言结果。
		 */
		void CheckClipboardWorkflow(TestContext& t) {
			SceneManager manager;
			SceneHandle handle, other;
			std::string error;
			if (!manager.CreateScene("Clipboard", handle).IsSuccess() ||
			    !manager.CreateScene("Other", other).IsSuccess()) {
				t.Check(false, "Create independent author scenes");
				return;
			}
			Editor::SceneDocument doc(StableId::FromWords(880, 1), manager, handle);
			Editor::SceneDocument foreign(StableId::FromWords(880, 2), manager, other);
			auto& scene = *doc.Resolve();
			PersistentEntityId parent, original, child, external;
			const bool created = CreateAuthorEntity(doc, "parent", {}, parent, error) &&
			    CreateAuthorEntity(doc, "original", parent, original, error) &&
			    CreateAuthorEntity(doc, "child", original, child, error) &&
			    CreateAuthorEntity(doc, "outside", {}, external, error);
			ScriptComponent script;
			script.script = AssetTest::Id(47);
			script.properties = {{"inside", child}, {"outside", external}, {"literal", child.ToString()}};
			ScriptComponent childScript;
			childScript.script = AssetTest::Id(49);
			childScript.properties = {{"owner", original}};
			BehaviorGraphComponent graph;
			graph.graph = AssetTest::Id(48);
			graph.variables = {{"moves", int64_t{3}}, {"step", .25}};
			if (!created || !EditScript(doc, original, script, error) || !EditScript(doc, child, childScript, error) ||
			    !EditBehaviorGraph(doc, child, graph, error)) {
				t.Check(false, "Build clipboard fixture: " + error);
				return;
			}
			auto& history = doc.History();
			const auto initial = history.Position();
			SceneSubtreeSnapshot snapshot;
			t.Check(
			    scene.CaptureForUndo(scene.Resolve(original), snapshot).IsSuccess() && history.Position() == initial,
			    "Copy captures a subtree without changing document history");
			t.Check(
			    RenameEntity(doc, original, "changed after copy", "original", error), "Edit the source after copying");
			PersistentEntityId first, second;
			const auto beforePaste = history.Position();
			if (!PasteAuthorSubtree(doc, snapshot, first, error) || !PasteAuthorSubtree(doc, snapshot, second, error)) {
				t.Check(false, "Paste twice: " + error);
				return;
			}
			t.Check(first != second && original != first && original != second && history.Position() == beforePaste + 2,
			    "Each paste has a fresh root and a single history entry");
			auto inspect = [&](PersistentEntityId copy, PersistentEntityId& copiedChild) {
				ScriptComponent copiedScript, copiedChildScript;
				BehaviorGraphComponent copiedGraph;
				RelationshipComponent rootRelation, childRelation;
				IdentityComponent identity;
				const auto entity = scene.Resolve(copy);
				if (!scene.GetWorld().Get(entity, copiedScript).IsSuccess() ||
				    !scene.GetIdentity(entity, identity).IsSuccess() ||
				    !scene.GetRelationship(entity, rootRelation).IsSuccess() || identity.name != "original (Copy)" ||
				    rootRelation.parent != scene.Resolve(parent) || copiedScript.script != script.script ||
				    copiedScript.properties.size() != script.properties.size())
					return false;
				const auto inside = copiedScript.properties.find("inside");
				const auto* target = inside == copiedScript.properties.end()
				    ? nullptr
				    : std::get_if<PersistentEntityId>(&inside->second);
				if (!target || *target == child || !target->HasValue())
					return false;
				copiedChild = *target;
				const auto resolved = scene.Resolve(*target);
				auto expected = script.properties;
				expected["inside"] = *target;
				auto childProperties = childScript.properties;
				childProperties["owner"] = copy;
				return copiedScript.properties == expected &&
				    scene.GetRelationship(resolved, childRelation).IsSuccess() && childRelation.parent == entity &&
				    scene.GetWorld().Get(resolved, copiedChildScript).IsSuccess() &&
				    copiedChildScript.script == childScript.script && copiedChildScript.properties == childProperties &&
				    scene.GetWorld().Get(resolved, copiedGraph).IsSuccess() && copiedGraph.graph == graph.graph &&
				    copiedGraph.variables == graph.variables;
			};
			PersistentEntityId firstChild, secondChild;
			t.Check(inspect(first, firstChild) && inspect(second, secondChild) && firstChild != secondChild,
			    "Each sibling copy remaps internal references, retains external IDs, and preserves captured names, literal strings and assets");
			t.Check(history.Undo(doc, error) && scene.Resolve(firstChild).HasValue() &&
			        scene.Resolve(original).HasValue() && !scene.Resolve(secondChild).HasValue() &&
			        !scene.Resolve(second).HasValue(),
			    "Undo removes the complete last paste alone");
			const auto oldChild = secondChild;
			t.Check(history.Redo(doc, error) && inspect(second, secondChild) && secondChild == oldChild,
			    "Redo restores the same copied identities and internal links");
			PersistentEntityId output = original;
			t.Check(!PasteAuthorSubtree(foreign, snapshot, output, error) && output == original &&
			        !foreign.History().CanUndo() && foreign.Resolve()->GetWorld().GetEntityCount() == 0,
			    "Cross-scene paste preserves scene, output and history");
			t.Check(DeleteAuthorEntity(doc, external, error), "Remove external target after copy");
			const auto count = scene.GetWorld().GetEntityCount();
			const auto position = history.Position();
			t.Check(!PasteAuthorSubtree(doc, snapshot, output, error) && output == original &&
			        history.Position() == position && scene.GetWorld().GetEntityCount() == count,
			    "Stale external reference rejects the entire paste without adding history");
			/* 先恢复外部实体、撤销第二份粘贴，再通过场景 API 删除外部实体，保留待重做命令。 */
			if (!history.Undo(doc, error) || !history.Undo(doc, error) || !history.CanRedo() ||
			    !scene.QueueDestroySubtree(scene.Resolve(external)).IsSuccess() || !scene.CommitPending().IsSuccess()) {
				t.Check(false, "Set up an out-of-history removal while a paste is waiting to be redone");
				return;
			}
			const auto beforeRedo = history.Position(), liveCount = scene.GetWorld().GetEntityCount();
			t.Check(!history.Redo(doc, error) && !error.empty() && history.Position() == beforeRedo &&
			        history.CanRedo() && scene.GetWorld().GetEntityCount() == liveCount &&
			        !scene.Resolve(second).HasValue() && !scene.Resolve(secondChild).HasValue() &&
			        scene.Resolve(firstChild).HasValue(),
			    "Redo revalidates external references before restoring any node or advancing document history");
		}

		/**
		 * @brief 通过公开命令核对相机和网格组件的添加及移除可以完整撤销。
		 * @param[in,out] t 接收实体、组件和固定几何引用的检查结果。
		 */
		void CheckComponentWorkflow(TestContext& t) {
			SceneManager scenes;
			SceneHandle handle;
			std::string error;
			if (!scenes.CreateScene("Components", handle).IsSuccess()) {
				t.Check(false, "Create component scene");
				return;
			}
			Editor::SceneDocument doc(StableId::FromWords(881, 1), scenes, handle);
			PersistentEntityId id, second;
			if (!CreateAuthorEntity(doc, "camera", {}, id, error) ||
			    !CreateAuthorEntity(doc, "mesh", {}, second, error)) {
				t.Check(false, error);
				return;
			}
			auto& scene = *doc.Resolve();
			const auto entity = scene.Resolve(id), meshEntity = scene.Resolve(second);
			auto& history = doc.History();
			auto& world = scene.GetWorld();
			CameraComponent a, b;
			t.Check(AddAuthorCamera(doc, id, error) && AddAuthorCamera(doc, second, error) &&
			        world.Get(entity, a).IsSuccess() && world.Get(meshEntity, b).IsSuccess() && a.viewId &&
			        a.viewId != b.viewId,
			    "Adding two cameras yields distinct nonzero view IDs");
			const auto checkpoint = history.Position();
			t.Check(!AddAuthorCamera(doc, id, error) && history.Position() == checkpoint,
			    "Duplicate camera leaves history unchanged");
			t.Check(RemoveAuthorComponent(doc, id, "CameraComponent", error) && !world.Has<CameraComponent>(entity) &&
			        history.Undo(doc, error) && world.Get(entity, b).IsSuccess() && b.viewId == a.viewId &&
			        history.Redo(doc, error) && !world.Has<CameraComponent>(entity),
			    "Camera removal is reversible without changing view identity");
			MeshData cube;
			MeshRendererComponent mesh, observed;
			if (!CreateCubeMesh(1, cube).IsSuccess() || !CreateMeshResource(cube, mesh.mesh).IsSuccess()) {
				t.Check(false, "Prepare real validated cube geometry");
				return;
			}
			mesh.color = {.1f, .25f, .5f, 1.f};
			t.Check(SetAuthorMesh(doc, second, mesh, error) && world.Get(meshEntity, observed).IsSuccess() &&
			        observed.mesh == mesh.mesh && observed.color == mesh.color && history.Undo(doc, error) &&
			        !world.Has<MeshRendererComponent>(meshEntity) && history.Redo(doc, error),
			    "Mesh assignment preserves geometry and is one reversible command");
			t.Check(RemoveAuthorComponent(doc, second, "MeshRendererComponent", error) && history.Undo(doc, error) &&
			        world.Get(meshEntity, observed).IsSuccess() && observed.mesh == mesh.mesh &&
			        observed.color == mesh.color,
			    "Undo mesh removal restores the same geometry binding and instance color");
			/* 同一网格的两个子网格使用不同槽。只替换第二槽，核对历史、渲染提取与作者场景落盘顺序。 */
			auto& firstPart = cube.submeshes.front();
			auto secondPart = firstPart;
			firstPart.indexCount = 3;
			secondPart.firstIndex += 3;
			secondPart.indexCount -= 3;
			secondPart.materialSlot = 1;
			cube.submeshes.push_back(secondPart);
			AssetVersionRef<MaterialInstance> original, replacement;
			std::shared_ptr<const MaterialInstance> unlit, pbr;
			if (!CreateMeshResource(cube, mesh.mesh).IsSuccess() ||
			    !AssetVersionRef<MeshAsset>::TryCreate(AssetTest::Id(8820), 1,
			        std::make_shared<MeshAsset>(MeshAsset{mesh.mesh, {"first", "second"}}), mesh.meshVersion, error) ||
			    !CreateBuiltinMaterialInstance(
			        BuiltinMaterialKind::Unlit, AssetTest::Id(8821), 1, AssetTest::Id(8822), unlit, error) ||
			    !CreateBuiltinMaterialInstance(
			        BuiltinMaterialKind::Pbr, AssetTest::Id(8823), 1, AssetTest::Id(8824), pbr, error) ||
			    !AssetVersionRef<MaterialInstance>::TryCreate(AssetTest::Id(8825), 1, unlit, original, error) ||
			    !AssetVersionRef<MaterialInstance>::TryCreate(AssetTest::Id(8826), 1, pbr, replacement, error)) {
				t.Check(false, "Prepare a mesh and two materials: " + error);
				return;
			}
			mesh.materials = {original, original};
			if (!SetAuthorMesh(doc, second, mesh, error)) {
				t.Check(false, "Assign initial material slots: " + error);
				return;
			}
			mesh.materials[1] = replacement;
			const auto position = history.Position();
			const auto matches = [&](const MeshRendererComponent& renderer, AssetId last) {
				return renderer.mesh == mesh.mesh && renderer.materials.size() == 2 &&
				    renderer.materials[0].GetId() == original.GetId() && renderer.materials[1].GetId() == last;
			};
			t.Check(SetAuthorMesh(doc, second, mesh, error) && history.Position() == position + 1 &&
			        world.Get(meshEntity, observed).IsSuccess() && matches(observed, replacement.GetId()) &&
			        history.Undo(doc, error) && world.Get(meshEntity, observed).IsSuccess() &&
			        matches(observed, original.GetId()) && history.Redo(doc, error) &&
			        world.Get(meshEntity, observed).IsSuccess() && matches(observed, replacement.GetId()),
			    "Replacing the second material slot is a single reversible edit");
			RenderFramePacket frame;
			t.Check(TransformSystem::Update(scene).IsSuccess() &&
			        RenderExtractSystem::Extract(scene, 1, std::span<const RenderViewRequest>{}, frame).IsSuccess() &&
			        frame.instances.size() == 1 && frame.instances.front().materials.size() == 2 &&
			        frame.instances.front().materials[0].Get() == original.Get() &&
			        frame.instances.front().materials[1].Get() == replacement.Get(),
			    "Render extraction uses the replacement from the selected material slot");
			SceneAsset saved, reloaded;
			std::string serialized;
			const bool read = SceneSerializer::Capture(scene, saved, error) &&
			    SceneSerializer::Write(saved, serialized, error) && SceneSerializer::Read(serialized, reloaded, error);
			const auto node = std::ranges::find(reloaded.entities, second, &SceneEntityAsset::id);
			t.Check(read && node != reloaded.entities.end() && node->mesh &&
			        node->mesh->mesh == mesh.meshVersion.GetId() &&
			        node->mesh->materials == std::vector<AssetId>{original.GetId(), replacement.GetId()},
			    "The serialized and reopened scene retains both material asset identities in order");
		}
		/**
		 * @brief 创建同时需要跳过前导索引、应用正基顶点才能命中的测试三角形。
		 * @param[out] mesh 返回经过正式资源校验的不可变几何。
		 * @return 顶点和索引布局通过校验时为真。
		 */
		bool MakeOffsetTriangle(MeshResourceRef& mesh) {
			MeshData data;
			const float positions[][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {-1, -1, 0}, {1, -1, 0}, {0, 1, 0}};
			const uint16_t indices[] = {0, 0, 0, 0, 1, 2};
			data.vertexCount = 6;
			data.vertexStride = sizeof(positions[0]);
			data.attributes = {{0, VertexFormat::Float3, 0, VertexSemantic::Position}};
			data.vertexData.resize(sizeof(positions));
			std::memcpy(data.vertexData.data(), positions, sizeof(positions));
			data.indexType = MeshIndexType::UInt16;
			data.indexData.resize(sizeof(indices));
			std::memcpy(data.indexData.data(), indices, sizeof(indices));
			data.submeshes = {{3, 3, 3, 3, 3, 0, MeshTopology::TriangleList}};
			data.localBounds = {{-1, -1, 0}, {1, 1, 0}, false};
			return CreateMeshResource(data, mesh).IsSuccess();
		}

		/**
		 * @brief 将取景结果重新投影，验证完整世界包围盒在正式显示矩阵的六个裁剪面以内。
		 * @param[in] camera 已执行聚焦的独立编辑相机。
		 * @param[in] viewport 实际显示尺寸。
		 * @param[in] bounds 需完全可见的世界空间包围盒。
		 * @return 所有八个角点投影后均在可见范围内时为真。
		 */
		bool BoxFitsViewport(
		    const Editor::EditorCameraController& camera, CameraViewport viewport, const Aabb& bounds) {
			Math::Mat4 world;
			CameraMatrices matrices;
			if (!Math::ComposeTRS(camera.Pose(), world).IsSuccess() ||
			    !BuildCameraMatrices(world, camera.Lens(), viewport, matrices).IsSuccess())
				return false;
			for (int corner = 0; corner < 8; ++corner) {
				const Math::Vec4 point{(corner & 1) ? bounds.maximum.x : bounds.minimum.x,
				    (corner & 2) ? bounds.maximum.y : bounds.minimum.y,
				    (corner & 4) ? bounds.maximum.z : bounds.minimum.z, 1};
				const auto clip = matrices.viewProjection * point;
				if (!(clip.w > 0 && std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w && clip.z >= 0 &&
				        clip.z <= clip.w))
					return false;
			}
			return true;
		}

		/**
		 * @brief 用实际层级、子网格和投影验证视口放置、子树范围及横竖视口聚焦。
		 * @param[in,out] t 接收射线命中、可见范围和失败不写入的检查结果。
		 */
		void CheckViewportWorkflow(TestContext& t) {
			Scene scene("Placement");
			const auto parent = PersistentEntityId::FromWords(882, 1), child = PersistentEntityId::FromWords(882, 2),
			           farther = PersistentEntityId::FromWords(882, 3);
			Math::TransformTRS rootPose, childPose, farPose;
			rootPose.translation = {0, 0, -5};
			rootPose.scale = {2, 3, 2};
			childPose.translation = {0, 0, 1};
			farPose.translation = {0, 0, -7};
			MeshRendererComponent mesh;
			if (!MakeOffsetTriangle(mesh.mesh) || !scene.QueueCreate(parent, "root", rootPose).IsSuccess() ||
			    !scene.QueueCreate(farther, "far", farPose).IsSuccess() || !scene.CommitPending().IsSuccess() ||
			    !scene.QueueCreate(child, "child", childPose, scene.Resolve(parent)).IsSuccess() ||
			    !scene.CommitPending().IsSuccess() ||
			    !scene.QueueSetMeshRenderer(scene.Resolve(child), mesh).IsSuccess() ||
			    !scene.QueueSetMeshRenderer(scene.Resolve(farther), mesh).IsSuccess() ||
			    !scene.CommitPending().IsSuccess() || !TransformSystem::Update(scene).IsSuccess()) {
				t.Check(false, "Prepare scaled mesh hierarchy for viewport geometry");
				return;
			}
			const auto near = [](Math::Vec3 a, Math::Vec3 b) { return glm::length(a - b) < .001f; };
			RenderView view;
			view.viewport = {400, 400};
			view.lens.orthographicHeight = 4;
			Math::Vec3 hit;
			for (const auto projection : {ProjectionType::Perspective, ProjectionType::Orthographic}) {
				view.lens.projection = projection;
				for (const auto reversed : {false, true}) {
					view.lens.reversedZ = reversed;
					const bool ready =
					    BuildCameraMatrices(Math::Mat4{1}, view.lens, view.viewport, view.matrices).IsSuccess();
					t.Check(ready && Editor::FindViewportPlacement(scene, view, .5f, .5f, hit) && near(hit, {0, 0, -3}),
					    "Center ray picks the nearest scaled triangle in both projections and depth directions");
					if (projection == ProjectionType::Orthographic) {
						t.Check(ready && Editor::FindViewportPlacement(scene, view, .5f, .375f, hit) &&
						        near(hit, {0, .5f, -3}),
						    "Top-side Vulkan image coordinate unprojects above the center");
						t.Check(ready && Editor::FindViewportPlacement(scene, view, .9f, .05f, hit) &&
						        near(hit, {1.6f, 1.8f, -5.1f}),
						    "An AABB intersection outside the indexed triangle falls through to the placement fallback");
					}
				}
			}
			view.lens.projection = ProjectionType::Perspective;
			view.lens.infiniteFar = true;
			t.Check(BuildCameraMatrices(Math::Mat4{1}, view.lens, view.viewport, view.matrices).IsSuccess() &&
			        Editor::FindViewportPlacement(scene, view, .5f, .5f, hit) && near(hit, {0, 0, -3}),
			    "Infinite reversed-Z perspective unprojects the center ray without dividing by a far point at infinity");
			view.lens.infiniteFar = false;
			view.lens.farPlane = 2;
			t.Check(BuildCameraMatrices(Math::Mat4{1}, view.lens, view.viewport, view.matrices).IsSuccess() &&
			        Editor::FindViewportPlacement(scene, view, .5f, .5f, hit) && near(hit, {0, 0, -2}),
			    "Clipped geometry cannot receive a drop and fallback stays within the finite view");
			hit = {1, 2, 3};
			t.Check(!Editor::FindViewportPlacement(scene, view, 1.1f, .5f, hit) && hit == Math::Vec3{1, 2, 3},
			    "A drop outside the displayed image does not replace its destination");
			Scene empty("Ground");
			Math::TransformTRS downward;
			downward.translation = {0, 2, 5};
			downward.rotation = glm::angleAxis(-Math::Pi / 4, Math::Vec3{1, 0, 0});
			Math::Mat4 world;
			view.lens.farPlane = 100;
			t.Check(Math::ComposeTRS(downward, world).IsSuccess() &&
			        BuildCameraMatrices(world, view.lens, view.viewport, view.matrices).IsSuccess() &&
			        Editor::FindViewportPlacement(empty, view, .5f, .5f, hit) && near(hit, {0, 0, 3}),
			    "A downward ray through an empty scene intersects the world ground plane");

			std::string error;
			Aabb bounds;
			if (!Editor::SelectionBounds(scene, scene.Resolve(parent), bounds, error) || bounds.empty ||
			    !near(bounds.minimum, {-2, -3, -5}) || !near(bounds.maximum, {2, 3, -3})) {
				t.Check(false,
				    "Subtree bounds include the root pivot and child geometry but exclude the unrelated farther mesh");
				return;
			}
			t.Check(true, "Scaled subtree bounds use the nearest child's world matrix");
			const auto center = (bounds.minimum + bounds.maximum) * .5f;
			const auto radius = glm::length(bounds.maximum - center);
			Editor::EditorCameraController camera;
			for (const auto projection : {ProjectionType::Perspective, ProjectionType::Orthographic})
				for (const auto viewport : {CameraViewport{160, 800}, CameraViewport{800, 160}}) {
					t.Check(camera.SetProjection(projection, error) && camera.Frame(center, radius, error, viewport) &&
					        BoxFitsViewport(camera, viewport, bounds),
					    "Focusing the selection fits all eight world-space corners in landscape and portrait");
				}
			const Aabb large{{-300, -300, -300}, {300, 300, 300}, false};
			t.Check(camera.Frame({}, glm::length(large.maximum), error, view.viewport) &&
			        camera.Lens().farPlane > 1000 && BoxFitsViewport(camera, view.viewport, large),
			    "The independent editor camera extends finite clipping when framing a large selection");
			const auto revision = camera.HistoryRevision();
			t.Check(!camera.Frame(center, -1, error, view.viewport) && !camera.Frame(center, radius, error, {0, 800}) &&
			        !camera.Frame(center, std::numeric_limits<float>::max(), error, view.viewport) && !error.empty() &&
			        camera.HistoryRevision() == revision,
			    "Invalid bounds or viewport cannot change editor camera history");
		}
	}

	void RunEditorWorkflowTests(TestContext& t) {
		CheckProjectWorkflow(t);
		CheckClipboardWorkflow(t);
		CheckComponentWorkflow(t);
		CheckViewportWorkflow(t);
	}
}
