#include "catpch.h"

#include "Assets/AssetTestFixture.h"
#include "Assets/BuiltinAssets.h"
#include "Assets/BuiltinAssetIds.h"
#include "Geometry/GeometryAssetLoader.h"
#include "Geometry/PrimitiveMesh.h"
#include "Images/ImageAssetLoader.h"
#include "Materials/MaterialAssetLoader.h"
#include "Materials/MaterialValidation.h"
#include "Shaders/ShaderLibrary.h"
#include "Core/PlatformServices.h"

namespace Catalpa::Tests {
	namespace {
		/**
		 * @brief 验证各领域的资源编码器和解码预算约束。
		 *
		 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
		 */
		void DomainCodecs(TestContext& context) {
			std::string error;
			AssetDecodeContext decode;
			decode.maxResidentBytes = 1024 * 1024;
			ImageAsset pixel;
			pixel.sourceUri = "test:rgba";
			pixel.image.format = PixelFormat::RGBA8_UNorm;
			pixel.image.width = pixel.image.height = 1;
			pixel.image.subresources = {{0, 0, 0, 1, 1, 1, 0, 4, 4, 4}};
			pixel.image.pixels = {std::byte{255}, std::byte{0}, std::byte{255}, std::byte{255}};
			auto codec = ImageAssetLoader::Codec();
			std::vector<std::byte> bytes;
			AssetLoadResult result;
			const auto source =
			    MakeAssetLoadResult<ImageAsset>(std::make_shared<const ImageAsset>(pixel), sizeof(ImageAsset) + 100);
			context.Check(codec.encode(source, bytes, error), "Image codec exposes the shared cooked encoder");
			AssetContainer imageContainer;
			imageContainer.payload = bytes;
			context.Check(codec.loader.decode(imageContainer, decode, result, error),
			    "Image loader consumes the same explicit layout");
			if (result.value)
				context.Check(
				    std::static_pointer_cast<const ImageAsset>(result.value)->image.pixels == pixel.image.pixels,
				    "Image decode preserves original bytes without color conversion");
			const auto retained = result.value;
			bytes.pop_back();
			context.Check(!ImageAssetLoader::DecodePayload(bytes, decode, result, error) && result.value == retained,
			    "Truncated pixel buffer preserves previous output");
			imageContainer.payloadVersion = 2;
			context.Check(!codec.loader.decode(imageContainer, decode, result, error),
			    "Unsupported image payload version is rejected");
			decode.maxResidentBytes = 1;
			context.Check(!ImageAssetLoader::DecodePayload(imageContainer.payload, decode, result, error),
			    "Image layout respects a small resident cap");
			decode.maxResidentBytes = 1024 * 1024;
			MeshData cube;
			MeshAsset mesh;
			mesh.materialSlots = {"surface"};
			if (!CreateCubeMesh(1, cube).IsSuccess() || !CreateMeshResource(cube, mesh.mesh).IsSuccess()) {
				context.Check(false, "Create valid mesh codec fixture");
				return;
			}
			context.Check(GeometryAssetLoader::EncodeMesh(mesh, bytes, error) &&
			        GeometryAssetLoader::DecodeMesh(bytes, decode, result, error),
			    "Mesh cooked encoder and decoder round trip validated immutable geometry");
			std::shared_ptr<const MeshAsset> decodedMesh;
			if (result.cppType == std::type_index(typeid(MeshAsset)) && result.value)
				decodedMesh = std::static_pointer_cast<const MeshAsset>(result.value);
			context.Check(decodedMesh && decodedMesh->mesh->GetData().vertexData == cube.vertexData &&
			        decodedMesh->mesh->GetData().indexData == cube.indexData,
			    "Mesh streams retain byte values through explicit endian serialization");
			if (!decodedMesh)
				return;
			auto badMesh = bytes;
			badMesh[12] = std::byte{255};
			badMesh[13] = std::byte{255};
			context.Check(!GeometryAssetLoader::DecodeMesh(badMesh, decode, result, error),
			    "Oversized attribute table is rejected before allocation");
			decode.maxResidentBytes = 32;
			context.Check(!GeometryAssetLoader::DecodeMesh(bytes, decode, result, error),
			    "Mesh immutable-copy working budget is enforced");
			decode.maxResidentBytes = 1024 * 1024;
			std::shared_ptr<const MaterialInstance> material;
			if (!CreateBuiltinMaterialInstance(
			        BuiltinMaterialKind::Unlit, AssetTest::Id(71), 1, AssetTest::Id(72), material, error)) {
				context.Check(false, error);
				return;
			}
			ModelAsset model;
			ModelNode node;
			node.id = StableId::FromWords(7, 1);
			node.name = "root";
			model.nodes.push_back(node);
			ModelMeshInstance instance;
			instance.node = 0;
			instance.mesh = AssetHandle<MeshAsset>(AssetTest::Id(70));
			instance.materials = {AssetHandle<MaterialInstance>(AssetTest::Id(73))};
			model.meshes.push_back(instance);
			decode.dependencies = {{AssetTest::Id(70), 1, decodedMesh, std::type_index(typeid(MeshAsset))},
			    {AssetTest::Id(73), 1, material, std::type_index(typeid(MaterialInstance))}};
			context.Check(GeometryAssetLoader::EncodeModel(model, bytes, error) &&
			        GeometryAssetLoader::DecodeModel(bytes, decode, result, error),
			    "Model loader resolves fixed mesh/material dependencies");
			decode.dependencies.clear();
			context.Check(!GeometryAssetLoader::DecodeModel(bytes, decode, result, error),
			    "Model does not silently accept unresolved geometry dependencies");
			CancellationSource cancelled;
			cancelled.Cancel();
			decode.cancellation = cancelled.Token();
			context.Check(!GeometryAssetLoader::DecodeModel(bytes, decode, result, error),
			    "Domain decode observes cooperative cancellation");
		}
	}

	void RunAssetBudgetTests(TestContext& context) {
		using namespace AssetTest;
		DomainCodecs(context);
		std::string error;
		Fixture fixture;
		if (!fixture.Create(error)) {
			context.Check(false, error);
			return;
		}
		AssetRecord child, parent;
		if (!fixture.Save(Container(Id(30), 7), child, error)) {
			context.Check(false, error);
			return;
		}
		auto parentContainer = Container(Id(31), 9);
		parentContainer.dependencies = {{child.id, child.version, child.contentHash}};
		if (!fixture.Save(parentContainer, parent, error)) {
			context.Check(false, error);
			return;
		}
		VirtualFileSystem files;
		AssetRegistry registry;
		if (!fixture.Mount(files, error) || !registry.Replace({parent, child}, error)) {
			context.Check(false, error);
			return;
		}
		JobSystem jobs;
		if (!jobs.Start({2, 16}, error)) {
			context.Check(false, error);
			return;
		}
		AssetManager manager(jobs, files, registry, Config());
		context.Check(
		    manager.RegisterLoader(Loader(), error) && manager.Request(parent.id, error) && Await(manager, parent.id),
		    "Parent waits for dependency without blocking a worker");
		context.Check(manager.GetStatus(parent.id) == AssetStatus::Ready, manager.GetDiagnostic(parent.id));
		AssetVersionRef<Value> reference;
		bool fallback = false;
		context.Check(manager.Acquire(parent.id, reference, fallback, error) && !fallback,
		    "Parent owns a fixed published version");
		if (!reference.HasValue())
			return;
		const auto both = manager.GetBudgetStats().used;
		context.Check(both == sizeof(Value) * 2, "Dependency and parent payloads are each charged once");
		manager.Unload(child.id);
		context.Check(
		    manager.GetBudgetStats().used == both, "Parent version retains its dependency after child unload");
		manager.Unload(parent.id);
		context.Check(manager.GetBudgetStats().used == both && reference.Get()->number == 9,
		    "External parent reference also retains the dependency graph");
		reference = {};
		context.Check(
		    manager.GetBudgetStats().used == 0, "Final parent release frees its unreferenced dependency bytes");
		/* 此双节点环在提交IO前被拒绝；原因须返回调用方并保留在两个失败条目中。 */
		auto cycleParent = parent, cycleChild = child;
		cycleChild.dependencies = {parent.id};
		AssetRegistry cycle;
		const bool cycleInstalled =
		    cycle.Replace({cycleParent, cycleChild}, error) && manager.ReplaceRegistry(cycle, error);
		context.Check(cycleInstalled, "Install the dependency-cycle fixture: " + error);
		if (cycleInstalled) {
			error = "stale caller diagnostic";
			context.Check(!manager.Request(parent.id, error),
			    "Dependency cycle produces a diagnostic instead of waiting forever");
			context.Check(
			    error.find("cycle") != std::string::npos && error.find(parent.id.ToString()) != std::string::npos,
			    "Cycle failure has an actionable diagnostic: " + error);
			context.Check(manager.GetStatus(parent.id) == AssetStatus::Failed &&
			        manager.GetStatus(child.id) == AssetStatus::Failed &&
			        manager.GetDiagnostic(parent.id).find(error) != std::string::npos &&
			        manager.GetDiagnostic(child.id).find(error) != std::string::npos && !error.empty(),
			    "Both rejected dependency entries retain the caller's failure reason");
			context.Check(manager.GetPendingCount() == 0 && manager.GetBudgetStats().used == 0,
			    "This cycle is rejected before submitting IO or reserving decode memory");
		}
		AssetManagerConfig small = Config();
		small.memoryBudgetBytes = 1000;
		AssetManager limited(jobs, files, registry, small);
		context.Check(limited.RegisterLoader(Loader(), error), "Register budget-limited loader");
		context.Check(limited.PublishBuiltin<Value>(
		                  Id(80), 1, std::make_shared<const Value>(Value{99, {}}), sizeof(Value), error) &&
		        limited.SetFallback<Value>(Id(80), error),
		    "Fallback uses a real published identity and budget owner");
		context.Check(!limited.Request(child.id, error) && limited.GetStatus(child.id) == AssetStatus::Failed &&
		        limited.GetBudgetStats().rejected > 0,
		    "Decode is refused before worker submission when transient budget is insufficient");
		AssetVersionRef<Value> substitute;
		context.Check(limited.Acquire(child.id, substitute, fallback, error) && fallback &&
		        substitute.GetId() == Id(80) && substitute.Get()->number == 99,
		    "Failure reports a typed fallback rather than pretending requested data is ready");
		context.Check(limited.GetStatus(child.id) == AssetStatus::Failed && !limited.GetDiagnostic(child.id).empty(),
		    "Fallback does not erase the original failure diagnostic");
		context.Check(
		    !limited.PublishBuiltin<Value>(child.id, 1, std::make_shared<const Value>(), sizeof(Value), error),
		    "Builtin cannot overwrite cooked identity");
		context.Check(!limited.PublishBuiltin<Value>(Id(80), 1, std::make_shared<const Value>(), sizeof(Value), error),
		    "Builtin producer cannot reuse its existing revision");
		auto gate = std::make_shared<Gate>();
		auto boundedConfig = Config();
		boundedConfig.maxPending = 1;
		AssetManager bounded(jobs, files, registry, boundedConfig);
		context.Check(
		    bounded.RegisterLoader(Loader([gate](uint32_t, const CancellationToken&) { gate->Enter(); }), error) &&
		        bounded.Request(child.id, error),
		    "Start one bounded pending request");
		context.Check(gate->AwaitEntry(), "Pending request entered worker");
		context.Check(!bounded.Request(parent.id, error), "Pending capacity rejects excess graph work");
		bounded.Cancel(child.id);
		gate->release.store(true, std::memory_order_release);
		context.Check(Drain(bounded) && bounded.GetStatus(child.id) == AssetStatus::Cancelled &&
		        bounded.GetBudgetStats().used == 0,
		    "Cancelled late result is discarded and returns all transient budget");
		/* 使用格式正确但内容错误的依赖摘要，验证加载器确实将其与已加载字节比较。 */
		auto wrong = parentContainer;
		wrong.dependencies[0].contentHash = parent.contentHash;
		AssetRecord wrongRecord;
		context.Check(fixture.Save(wrong, wrongRecord, error),
		    "Create dependency-digest mismatch fixture using another real digest");
		AssetRegistry wrongRegistry;
		context.Check(wrongRegistry.Replace({child, wrongRecord}, error), "Build mismatch registry");
		AssetManager mismatch(jobs, files, wrongRegistry, Config());
		context.Check(mismatch.RegisterLoader(Loader(), error) && mismatch.Request(parent.id, error) &&
		        Await(mismatch, parent.id) && mismatch.GetStatus(parent.id) == AssetStatus::Failed,
		    "Container dependency digest must match the actual ready dependency version");
		context.Check(mismatch.GetDiagnostic(parent.id).find("digest") != std::string::npos,
		    "Dependency digest mismatch remains visible");
		auto historyConfig = Config();
		historyConfig.maxKnownVersions = 1;
		AssetManager history(jobs, files, registry, historyConfig);
		context.Check(history.RegisterLoader(Loader(), error) && history.Request(child.id, error) &&
		        Await(history, child.id) && history.GetStatus(child.id) == AssetStatus::Ready,
		    "One bounded identity history slot records a real published digest");
		context.Check(!history.Request(parent.id, error) && error.find("history") != std::string::npos,
		    "Version-history capacity is explicit and refuses growth before further IO");
		/* 使用本次构建的真正Unlit容器，内置材质不能靠未验证的空Shader对象提前发布。 */
		std::filesystem::path resources;
		std::vector<AssetRecord> shaderRecords;
		VirtualFileSystem shaderFiles;
		AssetRegistry shaderRegistry;
		AssetId unlit, pbr;
		if (!GetExecutableDirectory(resources, error) ||
		    !ShaderLibrary::ReadBuiltinCatalog(resources / "Resources" / "Shaders", "shaders", shaderRecords, error) ||
		    !shaderRegistry.Replace(shaderRecords, error) ||
		    !shaderFiles.MountReadOnly("shaders", resources / "Resources" / "Shaders", 0, error) ||
		    !ShaderLibrary::BuiltinId("Unlit.slang", unlit, error) ||
		    !ShaderLibrary::BuiltinId("Pbr.slang", pbr, error)) {
			context.Check(false, error);
			return;
		}
		AssetManager builtins(jobs, std::move(shaderFiles), std::move(shaderRegistry));
		context.Check(ShaderLibrary::RegisterAssetLoader(builtins, error) &&
		        RegisterGeometryAssetLoaders(builtins, error) && RegisterImageAssetLoader(builtins, error) &&
		        RegisterMaterialAssetLoaders(builtins, error),
		    "Runtime domain loaders register before builtin publication");
		context.Check(!RegisterBuiltinAssets(builtins, error),
		    "Builtin materials cannot publish before their actual shader becomes ready");
		context.Check(builtins.Request(unlit, error) && Await(builtins, unlit) && builtins.Request(pbr, error) &&
		        Await(builtins, pbr) && RegisterBuiltinAssets(builtins, error),
		    "Runtime builtin CPU assets publish after both fixed surface shader dependencies are ready");
		AssetVersionRef<ImageAsset> errorImage;
		AssetVersionRef<MaterialInstance> errorMaterial;
		context.Check(builtins.Acquire(Id(900), errorImage, fallback, error) && fallback &&
		        errorImage.GetId() == BuiltinAssetIds::ErrorImage,
		    "Missing image resolves the registered error image version");
		context.Check(builtins.Acquire(Id(901), errorMaterial, fallback, error) && fallback &&
		        errorMaterial.GetId() == BuiltinAssetIds::ErrorInstance,
		    "Missing material resolves the shared error material instance");
		AssetVersionRef<MaterialDefinition> actualDefinition;
		context.Check(builtins.Acquire(BuiltinAssetIds::ErrorDefinition, actualDefinition, fallback, error) &&
		        errorMaterial.HasValue() && errorMaterial.Get()->definition.Get() == actualDefinition.Get(),
		    "Builtin material instances share the actual published definition owner");
		AssetVersionRef<ShaderAsset> fixedShader;
		AssetVersionRef<ShaderAsset> fixedPbrShader;
		AssetVersionRef<MaterialDefinition> actualPbr;
		context.Check(builtins.Acquire(pbr, fixedPbrShader, fallback, error) &&
		        builtins.Acquire(BuiltinAssetIds::DefaultPbrDefinition, actualPbr, fallback, error) &&
		        actualPbr.Get()->shaderVersion.Get() == fixedPbrShader.Get() && actualPbr.Get()->shader.id == pbr,
		    "PBR builtin pins Pbr bytecode instead of accidentally sharing the Unlit shader");
		context.Check(builtins.Acquire(unlit, fixedShader, fallback, error) && actualDefinition.HasValue() &&
		        actualDefinition.Get()->shaderVersion.Get() == fixedShader.Get(),
		    "Builtin definition pins the exact ready shader version, not only its logical ID");
		if (actualDefinition.HasValue()) {
			auto inconsistent = *actualDefinition.Get();
			inconsistent.shader = AssetHandle<ShaderAsset>(Id(902));
			context.Check(!ValidateMaterialDefinition(inconsistent, error),
			    "A resolved shader version cannot be attached to another logical shader identity");
		}
		if (fixedShader.HasValue()) {
			/* 两个受管生成revision复用已验证的实际字节；版本测试不伪造编译结果或SPIR-V摘要。 */
			AssetSnapshot measured;
			if (!builtins.GetAsset(unlit, std::type_index(typeid(ShaderAsset)), measured, error)) {
				context.Check(false, error);
				return;
			}
			AssetManager revisions(jobs);
			context.Check(ShaderLibrary::RegisterAssetLoader(revisions, error) &&
			        RegisterImageAssetLoader(revisions, error) && RegisterMaterialAssetLoaders(revisions, error),
			    "Register a separate owner for explicit generated shader revisions");
			auto firstShader = fixedShader.GetShared();
			AssetSnapshot measuredPbr;
			if (!builtins.GetAsset(pbr, std::type_index(typeid(ShaderAsset)), measuredPbr, error)) {
				context.Check(false, error);
				return;
			}
			context.Check(
			    revisions.PublishBuiltin<ShaderAsset>(unlit, 1, firstShader, measured.asset.residentBytes, error) &&
			        revisions.PublishBuiltin<ShaderAsset>(
			            pbr, 1, fixedPbrShader.GetShared(), measuredPbr.asset.residentBytes, error) &&
			        RegisterBuiltinAssets(revisions, error),
			    "Publish material definitions bound to generated shader revision one");
			AssetVersionRef<MaterialInstance> oldMaterial;
			context.Check(revisions.Acquire(BuiltinAssetIds::DefaultUnlitInstance, oldMaterial, fallback, error),
			    "Retain a material snapshot before shader replacement");
			auto secondShader = fixedShader.GetShared();
			context.Check(
			    revisions.PublishBuiltin<ShaderAsset>(unlit, 2, secondShader, measured.asset.residentBytes, error),
			    "Publish another immutable shader revision for the same logical ID");
			AssetVersionRef<ShaderAsset> latest;
			context.Check(revisions.Acquire(unlit, latest, fallback, error) && latest.GetVersion() == 2 &&
			        oldMaterial.HasValue() && oldMaterial.Get()->definition.Get()->shaderVersion.GetVersion() == 1 &&
			        oldMaterial.Get()->definition.Get()->shaderVersion.Get() == firstShader.get(),
			    "Old material snapshot preserves its exact shader dependency instead of chasing the current ID");
			revisions.Shutdown();
			firstShader.reset();
			secondShader.reset();
			latest = {};
			context.Check(oldMaterial.HasValue() && oldMaterial.Get()->definition.Get()->shaderVersion.HasValue() &&
			        ValidateShaderAsset(*oldMaterial.Get()->definition.Get()->shaderVersion.Get(), error),
			    "Retained material keeps the old valid shader alive after manager shutdown");
		}
		builtins.Shutdown();
		context.Check(
		    errorImage.HasValue() && errorMaterial.HasValue(), "Fallback references survive manager shutdown");
	}
}
