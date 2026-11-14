#include "catpch.h"

#include "TestRunner.h"
#include "Scene/SceneSerializer.h"
#include "Scene/PrefabReconciler.h"
#include "Core/Serialization/AuthorJson.h"
#include "Core/Serialization/SchemaMigrationRegistry.h"
#include "Documents/DocumentManager.h"
#include "Commands/SceneCommands.h"
#include "Commands/PrefabCommands.h"
#include "Documents/DocumentRecovery.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetTestFixture.h"
#include "Scripting/ScriptAsset.h"
#include "Assets/BuiltinAssetIds.h"
#include "Geometry/PrimitiveMesh.h"
#include "Animation/AnimationAssetLoader.h"
#include "Physics/PhysicsMaterialAsset.h"

namespace Catalpa::Tests {
	namespace {
		const auto Root = PersistentEntityId::FromWords(0x415554484f525445ULL, 1);
		const auto Child = PersistentEntityId::FromWords(0x415554484f525445ULL, 2);

		SceneAsset SimpleScene() {
			SceneAsset asset;
			asset.name = "Roundtrip";
			SceneEntityAsset root;
			root.id = Root;
			root.name = "Root";
			root.local.translation = {1, 2, 3};
			SceneEntityAsset child;
			child.id = Child;
			child.parent = Root;
			child.name = "Child";
			child.local.translation = {0, 1, 0};
			asset.entities = {child, root};
			return asset;
		}
	}

	void RunSceneAuthoringTests(TestContext& test) {
		std::string text, error;
		static_assert(std::is_nothrow_move_constructible_v<PrefabInstanceComponent> &&
		    std::is_nothrow_move_assignable_v<PrefabInstanceComponent>);
		ValueMap<int, std::string> values{{1, "original"}};
		auto copied = values;
		copied.at(1) = "copy";
		auto movedValues = std::move(copied);
		test.Check(values.at(1) == "original" && movedValues.at(1) == "copy" && copied.empty(),
		    "Component maps deeply copy and move without sharing mutable storage");
		copied[2] = "reused";
		test.Check(
		    !movedValues.contains(2) && copied.at(2) == "reused", "Moved component maps can be independently reused");
		/* 用同一父子场景校验往返序列化，再逐项破坏归档结构，检查失败后保留原结果。 */
		auto asset = SimpleScene();
		SceneAsset decoded;
		test.Check(SceneSerializer::Write(asset, text, error) && SceneSerializer::Read(text, decoded, error) &&
		        decoded.entities.size() == 2 && decoded.entities.front().id == Root,
		    "Scene roundtrip preserves stable IDs and orders hierarchy");
		const auto good = text;
		auto malformed = AuthorJson::Json::parse(good);
		malformed["unknown"] = true;
		test.Check(!SceneSerializer::Read(malformed.dump(), decoded, error) && decoded.entities.size() == 2,
		    "Unknown scene fields fail without replacing output");
		malformed = AuthorJson::Json::parse(good);
		malformed["entities"][0]["parent"] = Child.ToString();
		test.Check(!SceneSerializer::Read(malformed.dump(), decoded, error) && error.find("cycle") != error.npos,
		    "Scene hierarchy cycle has a diagnostic");
		malformed = AuthorJson::Json::parse(good);
		malformed["entities"][1]["id"] = Root.ToString();
		test.Check(!SceneSerializer::Read(malformed.dump(), decoded, error), "Duplicate scene identities are rejected");
		MeshData geometry;
		MeshResourceRef inlineMesh;
		auto inlineScene = SimpleScene();
		if (CreateCubeMeshWithTangents(1, geometry).IsSuccess() &&
		    CreateMeshResource(geometry, inlineMesh).IsSuccess()) {
			auto& renderer = inlineScene.entities.front().mesh.emplace();
			renderer.procedural = inlineMesh;
			renderer.materials = {BuiltinAssetIds::DefaultUnlitInstance};
			test.Check(SceneSerializer::Write(inlineScene, text, error) &&
			        SceneSerializer::Read(text, decoded, error) && decoded.entities.back().mesh &&
			        decoded.entities.back().mesh->procedural,
			    "Inline author geometry roundtrips without inventing a cooked locator");
		} else
			test.Check(false, "Create inline geometry fixture");
		/* 用旧版本文档验证迁移路径及不受支持的版本。 */
		SchemaMigrationRegistry migrations;
		auto legacy = AuthorJson::Json::parse(good);
		legacy["schemaVersion"] = 0;
		test.Check(!SceneSerializer::ReadMigrated(legacy.dump(), migrations, decoded, error),
		    "Missing explicit migration is rejected");
		test.Check(migrations.Register(
		               "scene", 0, [](ArchiveValue&, std::string&) { return true; }, error) &&
		        SceneSerializer::ReadMigrated(legacy.dump(), migrations, decoded, error),
		    "Registered migration produces a validated candidate rather than changing source bytes");
		legacy["schemaVersion"] = 2;
		test.Check(!SceneSerializer::ReadMigrated(legacy.dump(), migrations, decoded, error),
		    "Future scene versions are rejected");

		JobSystem jobs;
		AssetManager assets(jobs);
		SceneManager scenes;
		SceneHandle handle;
		test.Check(SceneSerializer::Instantiate(asset, scenes, &assets, handle, error),
		    "Prepare a full candidate scene before activation");
		auto* scene = scenes.GetScene(handle);
		if (!scene)
			return;
		(void)scenes.SetActive(handle);
		SceneAsset captured;
		test.Check(SceneSerializer::Capture(*scene, captured, error) && captured.entities.size() == 2,
		    "Capture author data without runtime registry layouts");
		auto broken = asset;
		broken.entities[0].parent = PersistentEntityId::FromWords(5, 8);
		SceneHandle untouched = handle;
		test.Check(!SceneSerializer::Instantiate(broken, scenes, &assets, untouched, error) && untouched == handle &&
		        scenes.GetActiveHandle() == handle,
		    "Failed scene loading preserves the current handle and active scene");

		/* 进入作者文档，检查编辑历史与文件保存、恢复之间的边界。 */
		Editor::DocumentManager documents;
		auto* doc = documents.SceneFor(scenes, handle, error);
		if (!doc) {
			test.Check(false, error);
			return;
		}
		doc->History().MarkSaved();
		test.Check(Editor::RenameEntity(*doc, Root, "Changed", "Root", error) && doc->History().Dirty(),
		    "Author edit marks the document dirty");
		test.Check(
		    doc->History().Undo(*doc, error) && !doc->History().Dirty(), "Undo to the saved checkpoint becomes clean");
		test.Check(
		    doc->History().Redo(*doc, error) && doc->History().Dirty(), "Redo away from the checkpoint becomes dirty");
		AssetTest::Fixture files;
		if (files.Create(error)) {
			const auto target = files.root / "Roundtrip.scene";
			files.files.push_back(target);
			test.Check(Editor::DocumentRecovery::Save(*doc, target, files.root, error) && !doc->History().Dirty(),
			    "Successful atomic author save establishes a checkpoint");
			auto oldPath = doc->AuthorPath();
			test.Check(Editor::RenameEntity(*doc, Root, "Unsaved", "Changed", error), "Prepare an unsaved author edit");
			test.Check(
			    !Editor::DocumentRecovery::Save(*doc, files.root.parent_path() / "Outside.scene", files.root, error) &&
			        doc->History().Dirty() && doc->AuthorPath() == oldPath,
			    "Failed save keeps dirty state, old path and old file");
			std::string stored;
			test.Check(Editor::DocumentRecovery::Read(target, stored, error) && stored.find("Changed") != stored.npos &&
			        stored.find("Unsaved") == stored.npos,
			    "Failed save leaves the previous file unchanged");
			const auto recovery = files.root / (doc->Id().ToString() + ".recovery.json");
			files.files.push_back(recovery);
			test.Check(Editor::DocumentRecovery::Checkpoint(*doc, files.root, error) &&
			        Editor::DocumentRecovery::Read(recovery, stored, error) &&
			        AuthorJson::Json::parse(stored).at("content").get<std::string>().find("Unsaved") !=
			            std::string::npos &&
			        doc->History().Dirty(),
			    "Recovery writes a separate current draft without advancing the saved checkpoint");
			const auto wrongExtension = files.root / "DoNotOverwrite.fbx";
			test.Check(!Editor::DocumentRecovery::Save(*doc, wrongExtension, files.root, error) &&
			        !std::filesystem::exists(wrongExtension),
			    "Author save cannot overwrite a source model with scene JSON");

			const auto luaPath = files.root / "Draft.lua";
			const auto scriptPath = files.root / "Draft.script";
			files.files.push_back(luaPath);
			files.files.push_back(scriptPath);
			Editor::AssetDocument luaDraft{StableId::FromWords(8, 1), {}, "script", "return {"};
			luaDraft.SetAuthorPath(luaPath);
			luaDraft.History().MarkUnsaved();
			test.Check(!documents.CanSaveTo(luaDraft, target, error) &&
			        !documents.CanSaveTo(luaDraft, files.root / "ROUNDTRIP.SCENE", error) &&
			        documents.CanSaveTo(*doc, target, error),
			    "Save As cannot overwrite another open document through a differently cased Windows path");
			const auto alias = files.root / "RoundtripAlias.scene";
			std::error_code linkError;
			std::filesystem::create_hard_link(target, alias, linkError);
			if (!linkError) {
				files.files.push_back(alias);
				test.Check(!documents.CanSaveTo(luaDraft, alias, error),
				    "Save As rejects hard-link aliases of another open document");
			}
			test.Check(!Editor::DocumentRecovery::Save(luaDraft, scriptPath, files.root, error) &&
			        luaDraft.History().Dirty() && !std::filesystem::exists(scriptPath),
			    "Save As cannot relabel raw Lua as a structured script document");
			test.Check(Editor::DocumentRecovery::Save(luaDraft, luaPath, files.root, error),
			    "Lua syntax errors remain saveable drafts in the original source format");
			auto* opened = documents.OpenSource({}, "script", "return {}", luaPath, error);
			const auto openCount = documents.All().size();
			test.Check(opened &&
			        documents.OpenSource({}, "script", "must not replace draft", luaPath, error) == opened &&
			        documents.OpenSource({}, "script", "must not replace draft", files.root / "DRAFT.LUA", error) ==
			            opened &&
			        documents.All().size() == openCount && *opened->Source() == "return {}",
			    "Opening the same author file or casing alias activates the existing draft without replacing it");
			const auto luaAlias = files.root / "DraftAlias.lua";
			std::filesystem::create_hard_link(luaPath, luaAlias, linkError);
			if (!linkError) {
				files.files.push_back(luaAlias);
				test.Check(documents.FindByPath(luaAlias, error) == opened &&
				        documents.OpenSource({}, "script", "must not replace draft", luaAlias, error) == opened &&
				        documents.All().size() == openCount,
				    "Opening a hard-link alias reuses the same author document");
			}

			ScriptAsset script;
			script.source = "return {}";
			std::string structured;
			test.Check(WriteScriptAsset(script, structured, error), "Prepare a structured script draft");
			Editor::AssetDocument recovered{StableId::FromWords(8, 2), {}, "script", structured};
			recovered.History().MarkUnsaved();
			test.Check(!Editor::DocumentRecovery::Save(recovered, luaPath, files.root, error) &&
			        recovered.AuthorPath().empty() && recovered.History().Dirty(),
			    "Recovered script JSON cannot overwrite Lua source through a mismatched extension");
			test.Check(Editor::DocumentRecovery::Save(recovered, scriptPath, files.root, error),
			    "Structured scripts save with their module declarations intact");

			Editor::AssetDocument escaped{
			    StableId::FromWords(8, 3), {}, "script", std::string(3 * 1024 * 1024, '\x01')};
			escaped.History().MarkUnsaved();
			const auto escapedPath = files.root / (escaped.Id().ToString() + ".recovery.json");
			files.files.push_back(escapedPath);
			test.Check(Editor::DocumentRecovery::Checkpoint(escaped, files.root, error) &&
			        !Editor::DocumentRecovery::Read(escapedPath, stored, error) &&
			        Editor::DocumentRecovery::Read(
			            escapedPath, stored, error, Editor::DocumentRecovery::MaxRecoveryBytes) &&
			        AuthorJson::Parse(stored, Editor::DocumentRecovery::MaxRecoveryBytes)
			                .at("content")
			                .get<std::string>() == *escaped.Source() &&
			        escaped.History().Dirty(),
			    "Recovery's bounded envelope budget includes JSON escaping without changing draft bytes");

			std::string binary = "CANI1";
			for (unsigned byte = 0; byte < 256; ++byte)
				binary += char(byte);
			Editor::AssetDocument binaryDraft{StableId::FromWords(8, 4), {}, "animation_clip", binary};
			const auto binaryRecovery = files.root / (binaryDraft.Id().ToString() + ".recovery.json");
			files.files.push_back(binaryRecovery);
			std::string recoveredType, recoveredContent;
			test.Check(Editor::DocumentRecovery::Checkpoint(binaryDraft, files.root, error) &&
			        Editor::DocumentRecovery::Read(binaryRecovery, stored, error) &&
			        AuthorJson::Json::parse(stored).at("contentEncoding") == "base64" &&
			        Editor::DocumentRecovery::DecodeCheckpoint(stored, recoveredType, recoveredContent, error) &&
			        recoveredType == "animation_clip" && recoveredContent == binary,
			    "Binary animation drafts recover every byte through bounded canonical base64");
			auto malformed = AuthorJson::Json::parse(stored);
			malformed["content"] = "YQ=A";
			test.Check(
			    !Editor::DocumentRecovery::DecodeCheckpoint(malformed.dump(), recoveredType, recoveredContent, error) &&
			        recoveredContent == binary,
			    "Malformed recovery base64 does not replace the previous draft");
			const AuthorJson::Json legacy{{"schemaVersion", 1}, {"document", binaryDraft.Id().ToString()},
			    {"type", "script"}, {"path", ""}, {"content", "return {}"}};
			test.Check(
			    Editor::DocumentRecovery::DecodeCheckpoint(legacy.dump(), recoveredType, recoveredContent, error) &&
			        recoveredContent == "return {}",
			    "Legacy plain UTF-8 recovery envelopes remain compatible");
			for (const auto type : {"skeleton", "animation_clip", "mesh_skin_binding", "blend_tree", "particle_effect",
			         "collision_shape", "physics_material", "navmesh", "terrain", "ui_document"})
				test.Check(Editor::DocumentRecovery::AuthorType(
				               "." + std::string(Editor::DocumentRecovery::AuthorSuffix(type)), {}) == type,
				    "Every new authored domain has a matching open/save extension");
			PhysicsMaterialAsset physics;
			test.Check(WritePhysicsMaterial(physics, structured, error), "Prepare authored physics material");
			Editor::AssetDocument physicsDraft{StableId::FromWords(8, 5), {}, "physics_material", structured};
			const auto physicsPath = files.root / "Surface.physics-material";
			files.files.push_back(physicsPath);
			test.Check(Editor::DocumentRecovery::Save(physicsDraft, physicsPath, files.root, error) &&
			        Editor::DocumentRecovery::Read(physicsPath, stored, error) && stored == structured,
			    "New domain Save validates and atomically preserves its author source");
			Editor::AssetDocument fontPreview{StableId::FromWords(8, 6), {}, "font", "{}"};
			test.Check(!Editor::DocumentRecovery::Save(fontPreview, files.root / "Original.ttf", files.root, error) &&
			        !std::filesystem::exists(files.root / "Original.ttf"),
			    "Read-only font preview cannot overwrite an original font file");
		} else
			test.Check(false, error);

		/* 从同一预制体创建多个实例，核对持久实体映射与组件引用。 */
		PrefabAsset prefab{SimpleScene(), {}};
		const auto referenceScript = AssetId::FromWords(0x5052454641425445ULL, 2);
		ScriptAsset noOp;
		noOp.source = "return {}";
		test.Check(RegisterScriptAssetLoader(assets, error) &&
		        assets.PublishBuiltin<ScriptAsset>(
		            referenceScript, 1, std::make_shared<const ScriptAsset>(noOp), 4096, error),
		    "Publish the prefab's typed script dependency");
		prefab.scene.entities.back().script = ScriptComponent{referenceScript, true, {{"target", Child}}};
		const auto prefabId = AssetId::FromWords(0x5052454641425445ULL, 1);
		test.Check(RegisterPrefabAssetLoader(assets, error), "Register the real prefab runtime decoder");
		test.Check(
		    assets.PublishBuiltin<PrefabAsset>(prefabId, 1, std::make_shared<const PrefabAsset>(prefab), 4096, error),
		    "Publish a CPU prefab fixture");
		PersistentEntityId first, second;
		test.Check(Editor::InstantiateAuthorPrefab(*doc, assets, prefabId, {}, first, error) &&
		        Editor::InstantiateAuthorPrefab(*doc, assets, prefabId, {}, second, error) && first != second,
		    "Multiple prefab instances have separate identities");
		PrefabInstanceComponent provenance;
		if (!scene->GetWorld().Get(scene->Resolve(first), provenance).IsSuccess()) {
			test.Check(false, "Prefab provenance is present");
			return;
		}
		const auto instanceChild = provenance.entities.at(Child);
		ScriptComponent instanceScript;
		test.Check(scene->GetWorld().Get(scene->Resolve(first), instanceScript).IsSuccess() &&
		        std::get<PersistentEntityId>(instanceScript.properties.at("target")) == instanceChild,
		    "Prefab script entity properties are remapped to that instance's local identities");
		TransformComponent transform;
		scene->GetTransform(scene->Resolve(instanceChild), transform);
		auto moved = transform.local;
		moved.translation = {4, 5, 6};
		test.Check(Editor::ChangeLocal(*doc, instanceChild, transform.local, moved, error),
		    "Edit a prefab instance without editing its source");
		prefab.scene.entities.back().name = "New base root";
		test.Check(
		    assets.PublishBuiltin<PrefabAsset>(prefabId, 2, std::make_shared<const PrefabAsset>(prefab), 4096, error) &&
		        Editor::UpdateAuthorPrefab(*doc, assets, first, false, error),
		    "Update a prefab base and preserve supported overrides");
		TransformComponent updated;
		IdentityComponent identity;
		scene->GetTransform(scene->Resolve(instanceChild), updated);
		scene->GetIdentity(scene->Resolve(first), identity);
		test.Check(updated.local.translation == moved.translation && identity.name == "New base root",
		    "Prefab update keeps entity IDs, instance TRS and new unmodified base fields");
		test.Check(doc->History().Undo(*doc, error) && scene->Resolve(instanceChild).HasValue(),
		    "Prefab base update is one undo transaction");
		test.Check(doc->History().Redo(*doc, error) && Editor::UpdateAuthorPrefab(*doc, assets, first, true, error),
		    "Redo and revert overrides use the document protocol");
		scene->GetTransform(scene->Resolve(instanceChild), updated);
		test.Check(updated.local.translation == Math::Vec3(0, 1, 0), "Revert restores the loaded base local transform");
		/* 在基底失效和嵌套形成环时，检查已有实例及其代次。 */
		auto invalidBase = prefab;
		auto& missingMesh = invalidBase.scene.entities.front().mesh.emplace();
		missingMesh.mesh = AssetId::FromWords(9, 99);
		missingMesh.materials = {BuiltinAssetIds::DefaultUnlitInstance};
		const auto generation = scene->Resolve(first);
		SceneSubtreeSnapshot unused;
		const PrefabResolver noNested;
		test.Check(!PrefabReconciler::Prepare(*scene, first, 3, invalidBase, &assets, noNested, false, unused, error) &&
		        scene->Resolve(first) == generation,
		    "Missing prefab dependencies leave the existing instance and generations unchanged");

		auto cyclic = prefab;
		cyclic.nested.push_back({StableId::FromWords(7, 1), prefabId, Root, {}});
		const PrefabResolver resolve = [&](AssetId, std::shared_ptr<const PrefabAsset>& out, uint64_t& version,
		                                   std::string&) {
			out = std::make_shared<const PrefabAsset>(cyclic);
			version = 3;
			return true;
		};
		PersistentEntityId rejected;
		test.Check(
		    !PrefabInstantiation::Instantiate(*scene, prefabId, 3, cyclic, &assets, resolve, {}, rejected, error) &&
		        error.find("cycle") != error.npos,
		    "Nested prefab cycles fail before creating any entity");

		const auto outerId = AssetId::FromWords(0x5052454641425445ULL, 3);
		PrefabAsset outer{SimpleScene(), {}};
		outer.nested.push_back({StableId::FromWords(7, 2), prefabId, Root, {}});
		PersistentEntityId outerRoot;
		test.Check(
		    assets.PublishBuiltin<PrefabAsset>(outerId, 1, std::make_shared<const PrefabAsset>(outer), 4096, error) &&
		        Editor::InstantiateAuthorPrefab(*doc, assets, outerId, {}, outerRoot, error),
		    "Instantiate a real nested prefab and record its attachment baseline");
		PrefabInstanceComponent outerProvenance;
		PersistentEntityId nestedRoot;
		if (scene->GetWorld().Get(scene->Resolve(outerRoot), outerProvenance).IsSuccess() &&
		    SceneSerializer::Capture(*scene, captured, error)) {
			for (const auto& node : captured.entities)
				if (node.parent == outerRoot && node.prefab && node.prefab->source == prefabId)
					nestedRoot = node.id;
		}
		if (!nestedRoot.HasValue()) {
			test.Check(false, "Locate the nested instance root");
			return;
		}
		const auto otherParent = outerProvenance.entities.at(Child);
		test.Check(
		    Editor::ReparentEntity(*doc, nestedRoot, otherParent, error), "Prepare a nested root parent override");
		const auto nestedGeneration = scene->Resolve(nestedRoot);
		test.Check(!Editor::UpdateAuthorPrefab(*doc, assets, outerRoot, false, error) &&
		        error.find("parent override") != std::string::npos && scene->Resolve(nestedRoot) == nestedGeneration,
		    "Base update rejects nested reparenting without replacing or moving the instance");
		RelationshipComponent relation;
		test.Check(scene->GetRelationship(nestedGeneration, relation).IsSuccess() &&
		        relation.parent == scene->Resolve(otherParent),
		    "Rejected nested update preserves the author parent");
		test.Check(
		    doc->History().Undo(*doc, error) && Editor::UpdateAuthorPrefab(*doc, assets, outerRoot, false, error),
		    "Undoing the conflicting reparent permits the normal nested base update");
	}
}
