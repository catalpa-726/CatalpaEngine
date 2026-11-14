#include "catpch.h"

#include "TestRunner.h"
#include "AssetCompiler/AssetCompilerOptions.h"
#include "AssetPipeline/AssetCooker.h"
#include "AssetPipeline/AssetArchiveWriter.h"
#include "AssetPipeline/Cookers/TextureCooker.h"
#include "AssetPipeline/AssetImportRegistry.h"
#include "AssetPipeline/Importers/GltfImporter.h"
#include "AssetPipeline/Importers/ImportGeometry.h"
#include "AssetPipeline/Importers/AssimpImporter.h"
#include "AssetPipeline/Importers/TextureImporter.h"
#include "Assets/AssetDependencyGraph.h"
#include "Assets/AssetManifest.h"
#include "Assets/AssetContainerReader.h"
#include "Images/ImageAssetLoader.h"
#include "Core/IO/AtomicFileWriter.h"
#include "Core/PlatformServices.h"
#include "Core/Id/StableIdGenerator.h"
#include "ECS/ComponentRegistry.h"
#include "Runtime/RuntimeRegistration.h"
#include "Scene/ModelInstantiation.h"
#include "Scene/Components/MeshRendererComponent.h"
#include "Scripting/ScriptAsset.h"
#include "Panels/BuildPanel.h"
#include "Log/LogSubscription.h"
#include "Log/Log.h"

#include <spdlog/sinks/sink.h>

namespace Catalpa::Tests {
	namespace {
		using namespace Tools;

		/**
		 * @brief bool操作结束后才构造错误文本视图，位置仍指向实际用例调用点。
		 *
		 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
		 * @param[in] success 标记操作是否成功。
		 * @param[out] error 被测操作返回的诊断信息，供失败断言显示。
		 * @param[in] location 断言调用位置，用于在失败时报告源文件和行号。
		 *
		 * @return 原操作的 success 值，便于调用者记录断言后决定是否继续。
		 */
		bool CheckOperation(TestContext& context, bool success, const std::string& error,
		    std::source_location location = std::source_location::current()) {
			context.Check(success, error, location);
			return success;
		}

		void CheckOperation(TestContext& context, bool success, std::string_view prefix, const std::string& error,
		    std::source_location location = std::source_location::current()) {
			context.Check(success, std::string(prefix) + error, location);
		}

		/* 所有写入/负例仅发生在独占临时项目；不使用或改写用户assets。 */
		struct Fixture {
			std::filesystem::path root, canonicalRoot;
			std::unique_ptr<ProjectSession> session;
			bool owned = false;

			~Fixture() {
				if (!owned || canonicalRoot.empty())
					return;
				std::error_code code;
				const auto current = std::filesystem::canonical(root, code);
				if (!code && current == canonicalRoot)
					std::filesystem::remove_all(root, code);
			}

			bool Store(const std::filesystem::path& path, std::string_view text, std::string& error) {
				return AtomicFileWriter::Write(root / path, text, error);
			}

			bool StoreBytes(const std::filesystem::path& path, std::span<const std::byte> bytes, std::string& error) {
				return AtomicFileWriter::Write(root / path, bytes, error);
			}

			bool Create(std::string& error) {
				StableId id;
				if (!GenerateStableId(id, error))
					return false;
				root = std::filesystem::temp_directory_path() / ("CatalpaImport-" + id.ToString());
				owned = std::filesystem::create_directory(root);
				if (!owned) {
					error = "Cannot create exclusive import fixture";
					return false;
				}
				canonicalRoot = std::filesystem::canonical(root);
				root = canonicalRoot;
				for (auto directory : {"Content", "Raw", "Other"})
					std::filesystem::create_directory(root / directory);
				return Store("Fixture.catalpaproj",
				           R"({"schemaVersion":1,"id":"11111111-2222-4333-8444-555555555555","name":"ImportFixture","contentRoot":"Content","externalSourceRoots":{"raw":"Raw"},"derivedDataRoot":"DDC","cookedRoot":"Cooked","metadata":"assets.json","buildProfiles":"profiles.json","buildProfile":"desktop-vulkan","targetPlatform":"windows","buildConfiguration":"Debug","startupScene":null,"modules":[]})",
				           error) &&
				    Store("Content/assets.json", R"({"schemaVersion":1,"assets":[]})", error) &&
				    Store("profiles.json",
				        R"({"schemaVersion":1,"profiles":{"desktop-vulkan":{"platform":"windows","architecture":"x86_64","shaderTarget":"spirv","shaderProfile":"spirv_1_5","targetEnvironment":"vulkan1.3","assetPipeline":"static-model-v1"}}})",
				        error) &&
				    LoadProject(root / "Fixture.catalpaproj", session, error);
			}

			AssetBuildRequest Request(const char* source, const std::filesystem::path& shaders) const {
				AssetBuildRequest r;
				r.project = session->config;
				r.source = root / "Raw" / source;
				r.shaderRoot = shaders;
				return r;
			}
		};

		bool Read(const std::filesystem::path& file, std::vector<std::byte>& bytes, std::string& error) {
			VirtualFileSystem fs;
			VirtualPath path;
			return fs.MountReadOnly("fixture", file.parent_path(), 0, error) &&
			    VirtualPath::Parse("fixture:/" + AssetPathText(file.filename()), path, error) &&
			    fs.ReadFile(path, 64 * 1024 * 1024, bytes, error);
		}

		std::string Replace(std::string text, std::string_view from, std::string_view to) {
			const auto at = text.find(from);
			if (at == std::string::npos)
				throw std::logic_error("Fixture replacement marker is missing");
			text.replace(at, from.size(), to);
			return text;
		}

		std::vector<std::byte> TriangleBytes(float width = 1) {
			const std::array<float, 24> values{
			    0, 0, 0, 0, 0, 1, 0, 0, width, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1};
			std::vector<std::byte> bytes;
			for (float value : values) {
				uint32_t bits;
				std::memcpy(&bits, &value, 4);
				for (unsigned i = 0; i < 4; ++i)
					bytes.push_back(std::byte((bits >> (i * 8)) & 255));
			}
			for (uint16_t value : {uint16_t(0), uint16_t(1), uint16_t(2)}) {
				bytes.push_back(std::byte(value & 255));
				bytes.push_back(std::byte(value >> 8));
			}
			return bytes;
		}

		const std::string Gltf =
		    R"({"asset":{"version":"2.0"},"buffers":[{"uri":"buffer.bin","byteLength":102}],"bufferViews":[{"buffer":0,"byteLength":96,"byteStride":32},{"buffer":0,"byteOffset":96,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":0,"byteOffset":12,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":0,"byteOffset":24,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"images":[{"uri":"pixels.png"}],"textures":[{"source":0}],"materials":[{"name":"Red","pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1],"metallicFactor":0.1,"roughnessFactor":0.2,"baseColorTexture":{"index":0}}},{"name":"Green","pbrMetallicRoughness":{"baseColorFactor":[0,1,0,1],"metallicFactor":0.7,"roughnessFactor":0.8}}],"meshes":[{"name":"Shared","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0},{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":1}]}],"nodes":[{"name":"First","mesh":0},{"name":"Second","mesh":0,"translation":[2,0,0]}],"scenes":[{"nodes":[0,1]}],"scene":0})";

		std::vector<std::byte> Png() {
			constexpr unsigned char values[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
			    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00,
			    0x72, 0xb6, 0x0d, 0x24, 0x00, 0x00, 0x00, 0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf,
			    0xc0, 0xf0, 0x1f, 0x0c, 0x19, 0x18, 0xfe, 0x83, 0x01, 0x00, 0x49, 0xc8, 0x09, 0xf7, 0x14, 0x63, 0x32,
			    0x9d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
			std::vector<std::byte> bytes;
			for (auto v : values)
				bytes.push_back(std::byte(v));
			return bytes;
		}

		std::vector<std::byte> Glb() {
			const auto from = std::string("\"uri\":\"buffer.bin\",");
			auto json = Replace(Gltf, from, "");
			while (json.size() % 4)
				json += ' ';
			auto bin = TriangleBytes();
			while (bin.size() % 4)
				bin.push_back(std::byte{0});
			std::vector<std::byte> bytes;
			auto u32 = [&](uint32_t n) {
				for (unsigned i = 0; i < 4; ++i)
					bytes.push_back(std::byte((n >> (8 * i)) & 255));
			};
			u32(0x46546c67);
			u32(2);
			u32(uint32_t(12 + 8 + json.size() + 8 + bin.size()));
			u32(uint32_t(json.size()));
			u32(0x4e4f534a);
			const auto jsonBytes = std::as_bytes(std::span(json.data(), json.size()));
			bytes.insert(bytes.end(), jsonBytes.begin(), jsonBytes.end());
			u32(uint32_t(bin.size()));
			u32(0x004e4942);
			bytes.insert(bytes.end(), bin.begin(), bin.end());
			return bytes;
		}

		constexpr std::string_view Obj = R"(mtllib triangle.mtl
o Triangle
v 0 0 0
v 1 0 0
v 0 1 0
vt 0 0
vt 1 0
vt 0 1
vn 0 0 1
usemtl Red
f 1/1/1 2/2/1 3/3/1
)";
		constexpr std::string_view Fbx = R"(; FBX 7.4.0 project file -- synthetic Catalpa test data
FBXHeaderExtension: {
    FBXHeaderVersion: 1003
    FBXVersion: 7400
    Creator: "Catalpa test fixture"
}
GlobalSettings: {
    Version: 1000
    Properties70: {
        P: "UpAxis", "int", "Integer", "",1
        P: "UpAxisSign", "int", "Integer", "",1
        P: "FrontAxis", "int", "Integer", "",2
        P: "FrontAxisSign", "int", "Integer", "",1
        P: "CoordAxis", "int", "Integer", "",0
        P: "CoordAxisSign", "int", "Integer", "",1
        P: "UnitScaleFactor", "double", "Number", "",1
        P: "OriginalUnitScaleFactor", "double", "Number", "",1
    }
}
Definitions: {
    Version: 100
    Count: 3
    ObjectType: "Model" { Count: 1 }
    ObjectType: "Geometry" { Count: 1 }
    ObjectType: "Material" { Count: 1 }
}
Objects: {
    Geometry: 1000, "Geometry::Triangle", "Mesh" {
        Vertices: *9 { a: 0,0,0,100,0,0,0,100,0 }
        PolygonVertexIndex: *3 { a: 0,1,-3 }
        LayerElementMaterial: 0 {
            Version: 101
            Name: ""
            MappingInformationType: "AllSame"
            ReferenceInformationType: "IndexToDirect"
            Materials: *1 { a: 0 }
        }
        Layer: 0 {
            Version: 100
            LayerElement: {
                Type: "LayerElementMaterial"
                TypedIndex: 0
            }
        }
    }
    Model: 1001, "Model::Triangle", "Mesh" {
        Version: 232
        Properties70: {
            P: "Lcl Translation", "Lcl Translation", "", "A",0,0,0
            P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
        }
        Shading: T
        Culling: "CullingOff"
    }
    Material: 1002, "Material::Red", "" {
        Version: 102
        ShadingModel: "phong"
        MultiLayer: 0
        Properties70: { P: "DiffuseColor", "Color", "", "A",1,0,0 }
    }
}
Connections: {
    C: "OO",1000,1001
    C: "OO",1002,1001
    C: "OO",1001,0
}
)";

		bool LoadAll(AssetManager& manager, const std::vector<AssetRecord>& records, std::string& error) {
			AssetDependencyGraph graph;
			if (!graph.Build(records, error))
				return false;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
			for (auto id : graph.Order()) {
				if (!manager.Request(id, error))
					return false;
				while (manager.GetStatus(id) == AssetStatus::Loading ||
				    manager.GetStatus(id) == AssetStatus::WaitingForDependencies) {
					if (std::chrono::steady_clock::now() > deadline) {
						error = "Asset load deadline exceeded";
						return false;
					}
					manager.Pump();
					std::this_thread::yield();
				}
				if (manager.GetStatus(id) != AssetStatus::Ready) {
					error = manager.GetDiagnostic(id);
					return false;
				}
			}
			return true;
		}

		/**
		 * @brief 4K边界独立于用户原件，验证旧64MiB上限之外的容器及CPU解码准入。
		 *
		 * @param[in,out] context 记录断言结果的测试上下文。
		 */
		void LargeImageContract(TestContext& context) {
			constexpr uint32_t side = 4096;
			constexpr size_t baseBytes = size_t(side) * side * 4;
			ImageAsset image;
			image.sourceUri = "synthetic:4k-rgba";
			auto& v = image.image;
			v.width = v.height = side;
			v.format = PixelFormat::RGBA8_SRGB;
			v.colorSpace = ImageColorSpace::SRGB;
			v.alphaMode = ImageAlphaMode::Straight;
			v.pixels.resize(baseBytes);
			v.pixels.front() = std::byte{0x91};
			v.pixels[3] = std::byte{0x80};
			v.pixels.back() = std::byte{0xff};
			v.subresources.push_back({0, 0, 0, side, side, 1, 0, side * 4, baseBytes, baseBytes});

			std::string error, baseHash;
			std::vector<std::byte> payload;
			AssetBuildProduct product;
			const auto encoded = ComputeAssetContentHash(v.pixels, baseHash, error) &&
			    TextureCooker::Encode(image, payload, error) &&
			    AssetArchiveWriter::Encode(AssetId::FromWords(5, 6), "image", payload, {}, product, error);
			CheckOperation(context, encoded, "4K RGBA plus metadata fits bounded cooked format: ", error);
			if (!encoded)
				return;
			image = {};
			payload.clear();
			payload.shrink_to_fit();
			AssetContainer container;
			const auto read = AssetContainerReader::Read(product.bytes, container, error, 96 * 1024 * 1024);
			product.bytes.clear();
			product.bytes.shrink_to_fit();
			AssetDecodeContext limits;
			limits.maxResidentBytes = 160 * 1024 * 1024;
			AssetLoadResult decoded;
			const auto loaded = read && ImageAssetLoader::DecodePayload(container.payload, limits, decoded, error);
			CheckOperation(context, loaded, "4K exact pixels decode without resize: ", error);
			if (!loaded)
				return;

			const auto& cooked = std::static_pointer_cast<const ImageAsset>(decoded.value)->image;
			size_t chainBytes = 0;
			for (uint32_t extent = side; extent > 0; extent >>= 1)
				chainBytes += size_t(extent) * extent * 4;
			context.Check(cooked.format == PixelFormat::RGBA8_SRGB && cooked.width == side && cooked.height == side &&
			        cooked.mipLevels == 13 && cooked.subresources.size() == 13 && cooked.pixels.size() == chainBytes,
			    "The 4K RGBA image retains the complete mip chain down to 1x1");

			const auto base = std::find_if(cooked.subresources.begin(), cooked.subresources.end(),
			    [](const auto& sub) { return sub.mip == 0 && sub.layer == 0 && sub.face == 0; });
			std::string decodedHash;
			const std::span<const std::byte> pixels(cooked.pixels);
			const bool exactBase = base != cooked.subresources.end() && base->width == side && base->height == side &&
			    base->rowPitch == side * 4 && base->byteLength == baseBytes && base->offset <= pixels.size() &&
			    baseBytes <= pixels.size() - base->offset &&
			    ComputeAssetContentHash(pixels.subspan(size_t(base->offset), baseBytes), decodedHash, error) &&
			    decodedHash == baseHash;
			context.Check(exactBase, "Decoded mip zero preserves every byte of the original 4K image");

			limits.maxResidentBytes = baseBytes;
			const auto previous = decoded.value;
			context.Check(!ImageAssetLoader::DecodePayload(container.payload, limits, decoded, error) &&
			        error.find("budget") != error.npos && decoded.value == previous,
			    "A base-level-only budget rejects the mip chain without replacing the loaded image");
		}

		/**
		 * @brief 在运行时加载构建产物并检查实例化场景记录。
		 *
		 * @param[in] manager 提供资产查找和版本访问的管理器。
		 * @param[in] built 提供模型记录及依赖、供运行时实例化验证的构建结果。
		 * @param[in,out] context 记录断言结果的测试上下文。
		 */
		void SceneContracts(AssetManager& manager, const AssetBuildResult& built, TestContext& context) {
			std::string error;
			bool fallback = false;
			AssetVersionRef<ModelAsset> model;
			CheckOperation(context, manager.Acquire(built.primary, model, fallback, error) && !fallback, error);
			if (!model.HasValue())
				return;
			std::vector<AssetVersionRef<MeshAsset>> meshes;
			std::vector<AssetVersionRef<MaterialInstance>> materials;
			for (const auto& record : built.records) {
				if (record.type.ToString() == "mesh") {
					AssetVersionRef<MeshAsset> v;
					if (manager.Acquire(record.id, v, fallback, error))
						meshes.push_back(v);
				} else if (record.type.ToString() == "material_instance") {
					AssetVersionRef<MaterialInstance> v;
					if (manager.Acquire(record.id, v, fallback, error))
						materials.push_back(v);
				}
			}
			auto schemas = std::make_shared<ComponentRegistry>();
			CheckOperation(context, RegisterRuntimeComponentSchemas(*schemas, error), error);
			Scene scene("Imported", schemas);
			std::shared_ptr<ModelInstantiationResult> first, second, cancelled, stale;
			const auto count = model.Get()->nodes.size();
			context.Check(ModelInstantiation::Queue(scene, model, meshes, materials, first, error) &&
			        scene.GetWorld().GetEntityCount() == 0,
			    "Queueing model does not publish entities before safe point");
			context.Check(scene.CommitPending().IsSuccess() && first && first->complete && first->success &&
			        scene.GetWorld().GetEntityCount() == count,
			    "Whole model commits with preserved hierarchy");
			context.Check(ModelInstantiation::Queue(scene, model, meshes, materials, second, error) &&
			        scene.CommitPending().IsSuccess() && second->success &&
			        scene.GetWorld().GetEntityCount() == count * 2,
			    "Repeated instantiation creates fresh identities");
			if (first && second && first->success && second->success) {
				context.Check(!first->roots.empty() && !second->roots.empty() &&
				        first->roots.front().persistent != second->roots.front().persistent,
				    "Instance identities differ while source mapping remains stable");
				for (const auto& instance : model.Get()->meshes) {
					if (instance.node >= first->nodes.size() || instance.node >= second->nodes.size()) {
						context.Check(false, "Both instances must map every model node");
						continue;
					}
					MeshRendererComponent a, b;
					const auto sa = scene.GetWorld().Get(scene.Resolve(first->nodes[instance.node].second), a),
					           sb = scene.GetWorld().Get(scene.Resolve(second->nodes[instance.node].second), b);
					context.Check(sa.IsSuccess() && sb.IsSuccess() && a.mesh == b.mesh &&
					        a.materials.size() == instance.materials.size(),
					    "Repeated models share immutable mesh payload and preserve material slots");
				}
			}
			if (model.Get()->meshes.empty() || model.Get()->meshes.front().materials.empty()) {
				context.Check(false, "The model fixture must have a mesh and an authored material");
				return;
			}
			const auto sourceId = model.Get()->meshes.front().materials.front().id;
			const auto sourceMapping = std::find_if(built.mappings.begin(), built.mappings.end(),
			    [&](const auto& m) { return m.asset == sourceId && m.kind == "material"; });
			context.Check(sourceMapping != built.mappings.end(), "Authored material has a source mapping");
			if (sourceMapping != built.mappings.end()) {
				const auto previewMapping =
				    std::find_if(built.mappings.begin(), built.mappings.end(), [&](const auto& m) {
					    return m.sourceKey == sourceMapping->sourceKey + "/preview" && m.kind == "material_preview";
				    });
				context.Check(previewMapping != built.mappings.end(), "Preview material has its own mapping");
				if (previewMapping != built.mappings.end()) {
					const auto preview = std::find_if(materials.begin(), materials.end(),
					    [&](const auto& m) { return m.GetId() == previewMapping->asset; });
					context.Check(preview != materials.end(), "Mapped preview material loaded");
					if (preview != materials.end()) {
						const std::array<ModelMaterialOverride, 1> overrides{{{sourceId, *preview}}};
						std::shared_ptr<ModelInstantiationResult> display;
						const auto queued =
						    ModelInstantiation::Queue(scene, model, meshes, materials, display, error, {}, overrides);
						context.Check(queued && scene.CommitPending().IsSuccess() && display->success,
						    "Explicit preview override instantiates through the same transaction");
						if (display && display->success && model.Get()->meshes.front().node >= display->nodes.size()) {
							context.Check(false, "Preview is missing the entity mapped to its source mesh");
						} else if (display && display->success) {
							MeshRendererComponent renderer;
							const auto loaded = scene.GetWorld().Get(
							    scene.Resolve(display->nodes[model.Get()->meshes.front().node].second), renderer);
							context.Check(loaded.IsSuccess() && !renderer.materials.empty() &&
							        renderer.materials.front().GetId() == preview->GetId() &&
							        model.Get()->meshes.front().materials.front().id == sourceId,
							    "Preview binding changes presentation without editing authored "
							    "model references");
						}
					}
				}
			}
			const auto before = scene.GetWorld().GetEntityCount();
			CheckOperation(
			    context, ModelInstantiation::Queue(scene, model, meshes, materials, cancelled, error), error);
			if (cancelled)
				cancelled->cancelRequested = true;
			context.Check(scene.CommitPending().IsSuccess() && cancelled && cancelled->complete &&
			        !cancelled->success && scene.GetWorld().GetEntityCount() == before,
			    "Cancellation consumes transaction and publishes no partial entities");
			PersistentEntityId parent;
			CheckOperation(context, GeneratePersistentEntityId(parent, error), error);
			context.Check(
			    scene.QueueCreate(parent, "Transient parent", {}).IsSuccess() && scene.CommitPending().IsSuccess(),
			    "Create isolated parent for stale-parent rollback");
			const auto parentEntity = scene.Resolve(parent);
			context.Check(
			    scene.QueueDestroySubtree(parentEntity).IsSuccess(), "Queue parent removal before model transaction");
			CheckOperation(
			    context, ModelInstantiation::Queue(scene, model, meshes, materials, stale, error, parentEntity), error);
			context.Check(scene.CommitPending().IsSuccess() && stale && stale->complete && !stale->success &&
			        scene.GetWorld().GetEntityCount() == before,
			    "Stale-parent transaction reports failure without publishing half a model or stopping scene");
			auto bad = meshes;
			if (!bad.empty())
				bad.pop_back();
			auto retained = first;
			context.Check(!ModelInstantiation::Queue(scene, model, bad, materials, first, error) && first == retained &&
			        scene.GetWorld().GetEntityCount() == before,
			    "Missing mesh rejects before queue and preserves previous output");
			const auto authored = std::find_if(
			    materials.begin(), materials.end(), [&](const auto& material) { return material.GetId() == sourceId; });
			if (authored != materials.end()) {
				auto ambiguous = materials;
				ambiguous.push_back(*authored);
				context.Check(!ModelInstantiation::Queue(scene, model, meshes, ambiguous, first, error) &&
				        first == retained && scene.GetWorld().GetEntityCount() == before,
				    "Duplicate material versions cannot create an ambiguous slot binding");
			}
		}

		/**
		 * @brief 各格式共用加载器和 world-space 包围盒，单位转换只应发生一次。
		 *
		 * @param[in] fixture 持有各格式测试原件与临时项目的夹具。
		 * @param[in] shaders 测试构建使用的着色器根目录。
		 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
		 */
		void CheckCrossFormatImports(
		    const Fixture& fixture, const std::filesystem::path& shaders, TestContext& context) {
			std::string error;
			for (auto file : {"model.glb", "triangle.obj", "triangle.fbx"}) {
				AssetBuildContext input(fixture.Request(file, shaders));
				AssetImportResult result;
				const auto importers = AssetImportRegistry::Defaults();
				const auto* importer = importers.Find(file);
				const bool imported = input.Initialize(error) && importer && importer->Import(input, result, error);
				context.Check(imported, std::string(file) + ": " + error);
				if (imported) {
					context.Check(result.primary.HasValue() && !result.assets.empty(),
					    "Other formats enter the same domain representation");
					std::vector<AssetVersionRef<MeshAsset>> fixedMeshes;
					for (const auto& asset : result.assets)
						if (const auto* mesh = std::get_if<MeshAsset>(&asset.value)) {
							AssetVersionRef<MeshAsset> reference;
							if (!CheckOperation(context,
							        AssetVersionRef<MeshAsset>::TryCreate(
							            asset.id, 1, std::make_shared<const MeshAsset>(*mesh), reference, error),
							        error))
								continue;
							fixedMeshes.push_back(std::move(reference));
						}
					const auto modelItem = std::find_if(result.assets.begin(), result.assets.end(),
					    [](const auto& asset) { return std::holds_alternative<ModelAsset>(asset.value); });
					if (modelItem == result.assets.end()) {
						context.Check(false, "Importer did not return a model");
						continue;
					}
					Aabb bounds;
					const auto bounded =
					    ComputeModelBounds(std::get<ModelAsset>(modelItem->value), fixedMeshes, bounds, error);
					const float expectedX = std::string_view(file) == "model.glb" ? 3.0f : 1.0f;
					context.Check(bounded && !bounds.empty && std::abs(bounds.maximum.x - expectedX) < 1e-4f &&
					        std::abs(bounds.maximum.y - 1.0f) < 1e-4f,
					    "Cross-format world dimensions agree in metres before any camera framing");
					if (std::string_view(file) == "triangle.fbx") {
						const auto& converted = std::get<ModelAsset>(modelItem->value);
						context.Check(!converted.nodes.empty() &&
						        std::abs(std::abs(converted.nodes.back().local.scale.x) - .01f) < 1e-6f,
						    "FBX centimetre metadata is converted once at the model root");
					}
				}
			}
		}

		/**
		 * @brief 第一个角的源法线平行于 dP/du，MikkTSpace 的切平面投影在该角为零。
		 *
		 * @param[in] request 为切线退化用例提供构建上下文、预算和导入选项的请求。
		 * @param[in,out] context 记录断言结果的测试上下文。
		 */
		void TangentCornerContract(const AssetBuildRequest& request, TestContext& context) {
			std::vector<ImportVertex> vertices(3);
			vertices[1].position = {1, 0, 0};
			vertices[2].position = {0, 1, 0};
			vertices[1].uv = {1, 0};
			vertices[2].uv = {0, 1};
			vertices[0].normal = {1, 0, 0};
			vertices[1].normal = vertices[2].normal = {0, 0, 1};

			AssetBuildContext input(request);
			MeshAsset mesh;
			std::string error;
			if (!CheckOperation(context,
			        input.Initialize(error) &&
			            BuildImportedMesh(input, vertices, {0, 1, 2}, true, true, false, mesh, error),
			        error))
				return;
			const auto& stream = mesh.mesh->GetData();
			const auto tangent = std::find_if(stream.attributes.begin(), stream.attributes.end(),
			    [](const auto& attr) { return attr.semantic == VertexSemantic::Tangent; });
			if (tangent == stream.attributes.end() || stream.vertexCount != 3) {
				context.Check(false, "The three input corners must retain an explicit tangent stream");
				return;
			}
			std::array<float, 4> first{}, second{};
			std::memcpy(first.data(), stream.vertexData.data() + tangent->offset, sizeof(first));
			std::memcpy(
			    second.data(), stream.vertexData.data() + stream.vertexStride + tangent->offset, sizeof(second));
			context.Check(std::abs(first[0]) < 1e-5f && std::abs(std::hypot(first[1], first[2]) - 1) < 1e-5f &&
			        std::abs(first[3]) == 1,
			    "Degenerate corner receives a unit tangent orthogonal to its source normal");
			context.Check(std::abs(second[0] - 1) < 1e-5f && std::abs(second[1]) < 1e-5f &&
			        std::abs(second[2]) < 1e-5f && second[3] == 1,
			    "Ordinary corner keeps the Mikk tangent and handedness");
			context.Check(std::any_of(input.diagnostics.begin(), input.diagnostics.end(),
			                  [](const auto& diagnostic) {
				                  return diagnostic.level == AssetDiagnosticLevel::Warning &&
				                      diagnostic.message.find("orthogonal fallback") != std::string::npos;
			                  }),
			    "Generated tangent repair is reported");

			const auto previous = mesh.mesh;
			vertices[0].tangent = {0, 0, 0, 1};
			context.Check(
			    !BuildImportedMesh(input, vertices, {0, 1, 2}, true, true, true, mesh, error) && mesh.mesh == previous,
			    "An invalid authored tangent still fails without changing the published mesh");
		}

		/**
		 * @brief OBJ 和 MTL 旁各有一个同名文件，贴图引用应按声明它的 MTL 定位。
		 *
		 * @param[in,out] fixture 持有本次测试输入与临时项目目录的夹具。
		 * @param[in] shaders 供本次烘焙读取的着色器根目录。
		 * @param[in,out] context 记录断言结果的测试上下文。
		 */
		void MaterialLibraryPathContract(Fixture& fixture, const std::filesystem::path& shaders, TestContext& context) {
			std::string error;
			const auto materialDir = fixture.root / "Raw" / "Materials";
			std::filesystem::create_directories(materialDir);
			const auto obj = Replace(std::string(Obj), "triangle.mtl", "Materials/nested.mtl");
			if (!CheckOperation(context,
			        fixture.Store("Raw/nested.obj", obj, error) &&
			            fixture.Store("Raw/Materials/nested.mtl", "newmtl Red\nKd 1 1 1\nmap_Kd albedo.png\n", error) &&
			            fixture.StoreBytes("Raw/Materials/albedo.png", Png(), error) &&
			            fixture.Store("Raw/albedo.png", "different file beside the OBJ", error),
			        error))
				return;
			AssetBuildContext input(fixture.Request("nested.obj", shaders));
			AssetImportResult result;
			if (!CheckOperation(
			        context, input.Initialize(error) && AssimpImporter{}.Import(input, result, error), error))
				return;
			const auto& inputs = input.Dependencies();
			context.Check(std::any_of(inputs.begin(), inputs.end(),
			                  [&](const auto& dep) { return dep.path == materialDir / "albedo.png"; }),
			    "MTL texture resolves relative to its material library");
		}

		/**
		 * @brief 验证 FBX 导入区分推导出的粗糙度越界与无效作者值。
		 *
		 * @param[in] fixture 持有本次测试输入与临时项目目录的夹具。
		 * @param[in] shaders 供本次烘焙读取的着色器根目录。
		 * @param[in,out] context 记录断言结果的测试上下文。
		 */
		void FbxRoughnessContract(Fixture& fixture, const std::filesystem::path& shaders, TestContext& context) {
			/* Ns=156.25 时 Assimp 恰好推导出 -0.25，可区分派生越界与作者显式提交同一数值。 */
			constexpr std::string_view diffuse = R"(P: "DiffuseColor", "Color", "", "A",1,0,0)";
			const auto makeSource = [&](std::string_view roughness) {
				std::string properties(diffuse);
				properties += "\n            "
				              R"(P: "ShininessExponent", "double", "Number", "",156.25)";
				if (!roughness.empty()) {
					properties += "\n            "
					              R"(P: "Maya|roughness", "double", "Number", "",)";
					properties += roughness;
				}
				return Replace(std::string(Fbx), diffuse, properties);
			};

			const auto request = fixture.Request("phong.fbx", shaders);
			std::string error;
			const auto checkValid = [&](std::string_view authored, float expected) {
				if (!CheckOperation(context, fixture.Store("Raw/phong.fbx", makeSource(authored), error), error))
					return;
				AssetBuildContext input(request);
				AssetImportResult imported;
				if (!CheckOperation(
				        context, input.Initialize(error) && AssimpImporter{}.Import(input, imported, error), error))
					return;
				const auto expectedMaterial =
				    std::find_if(imported.assets.begin(), imported.assets.end(), [](const auto& asset) {
					    const auto* material = std::get_if<ImportedMaterial>(&asset.value);
					    return material && material->sourceProperties.contains("phong.Ns");
				    });
				context.Check(expectedMaterial != imported.assets.end() &&
				        std::abs(std::get<ImportedMaterial>(expectedMaterial->value).roughness - expected) < 1e-6f,
				    "FBX preserves authored roughness or converts the original Phong exponent");
				const auto built = AssetCooker::Build(request);
				CheckOperation(context, built.success, "Cook FBX material: ", built.error);
			};
			checkValid({}, std::sqrt(2.0f / 158.25f));
			checkValid("0", 0.0f);

			if (!CheckOperation(context, fixture.Store("Raw/phong.fbx", makeSource("-0.25"), error), error))
				return;
			const auto invalid = AssetCooker::Build(request);
			context.Check(!invalid.success && invalid.error.find("roughness") != std::string::npos,
			    "Explicit out-of-range FBX roughness produces a diagnostic even when it equals "
			    "the Assimp-derived value");
		}

		/**
		 * @brief 归档将 PNG 放在 FBX 旁。导出时引用的目录后来恢复时，缓存必须重新选择该目录。
		 *
		 * @param[in,out] fixture 持有本次测试输入与临时项目目录的夹具。
		 * @param[in] shaders 供本次烘焙读取的着色器根目录。
		 * @param[in,out] context 记录断言结果的测试上下文。
		 */
		void RelocatedFbxTextureContract(Fixture& fixture, const std::filesystem::path& shaders, TestContext& context) {
			std::string error;
			auto source =
			    Replace(std::string(Fbx), "Count: 3\n", "Count: 4\n    ObjectType: \"Texture\" { Count: 1 }\n");
			source = Replace(source, "    Material: 1002,", R"(    Texture: 1003, "Texture::Albedo", "" {
        Type: "TextureVideoClip"
        RelativeFilename: "../maps/albedo.png"
    }
    Material: 1002,)");
			source = Replace(
			    source, "    C: \"OO\",1002,1001", "    C: \"OP\",1003,1002,\"DiffuseColor\"\n    C: \"OO\",1002,1001");
			std::filesystem::create_directories(fixture.root / "Raw/packed");
			if (!CheckOperation(context,
			        fixture.Store("Raw/packed/relocated.fbx", source, error) &&
			            fixture.StoreBytes("Raw/packed/albedo.png", Png(), error),
			        error))
				return;
			const auto request = fixture.Request("packed/relocated.fbx", shaders);
			AssetBuildContext input(request);
			AssetImportResult imported;
			if (!CheckOperation(
			        context, input.Initialize(error) && AssimpImporter{}.Import(input, imported, error), error))
				return;
			const auto& inputs = input.Dependencies();
			context.Check(std::any_of(inputs.begin(), inputs.end(),
			                  [&](const auto& file) { return file.path == fixture.root / "Raw/packed/albedo.png"; }) &&
			        !input.RelocatedTextures().empty(),
			    "FBX resolves a missing relative texture to the adjacent same-name file");
			AssetBuildContext absoluteInput(request);
			std::filesystem::path resolved;
			const auto authorPath = fixture.root.parent_path() / "foreign-author-directory" / "albedo.png";
			context.Check(absoluteInput.Initialize(error) &&
			        absoluteInput.Resolve(request.source, AssetPathText(authorPath), resolved, error,
			            SourceReferenceSyntax::FbxTexture) &&
			        resolved == fixture.root / "Raw/packed/albedo.png",
			    "Foreign author absolute paths can still name a packaged texture without opening "
			    "the external path");
			context.Check(!absoluteInput.Resolve(request.source, "../../../albedo.png", resolved, error,
			                  SourceReferenceSyntax::FbxTexture),
			    "A relative texture path that escapes its declared source root is rejected");

			const auto first = AssetCooker::Build(request);
			if (!CheckOperation(context, first.success && !first.cacheHit, first.error))
				return;
			const auto warm = AssetCooker::Build(request);
			context.Check(warm.success && warm.cacheHit && warm.key == first.key,
			    "Unchanged FBX texture selection hits the cache");

			auto mapped = request;
			mapped.settings.dependencyRemaps.emplace("../maps/albedo.png", "missing-explicit.png");
			context.Check(
			    !AssetCooker::Build(mapped).success, "A broken explicit FBX remap must not fall back silently");
			std::filesystem::create_directories(fixture.root / "Raw/maps");
			if (!CheckOperation(context, fixture.StoreBytes("Raw/maps/albedo.png", Png(), error), error))
				return;
			context.Check(
			    !input.InputsUnchanged(error), "FBX publication guard detects a restored authored texture path");
			const auto restored = AssetCooker::Build(request);
			context.Check(restored.success && !restored.cacheHit && restored.key != first.key,
			    "Restoring the authored texture path invalidates even an identical-byte fallback");
		}

		/**
		 * @brief 同一内容版本改在新根下提供。旧根没有该容器，读取成功只能来自新定位。
		 *
		 * @param[in] fixture 持有本次测试输入与临时项目目录的夹具。
		 * @param[in] image 需要在新目录中以同一版本读取的图像资产记录。
		 * @param[in] cookedRoot 包含图像构建产物的原始根目录。
		 * @param[in,out] jobs 执行并等待资产加载任务的作业系统。
		 * @param[in,out] context 记录断言结果的测试上下文。
		 */
		void RelocatedImageContract(const Fixture& fixture, const AssetRecord& image,
		    const std::filesystem::path& cookedRoot, JobSystem& jobs, TestContext& context) {
			std::string error;
			const auto oldRoot = fixture.root / "PreviousCooked";
			std::filesystem::create_directories(oldRoot);
			auto previous = image;
			if (!CheckOperation(
			        context, VirtualPath::Parse("old-cooked:/image.catasset", previous.locator.path, error), error))
				return;
			AssetManager moved(jobs);
			AssetRegistry catalog;
			if (!CheckOperation(context,
			        RegisterRuntimeAssetLoaders(moved, error) && catalog.Replace({previous}, error) &&
			            moved.InstallCookedCatalog(oldRoot, catalog, error) && catalog.Replace({image}, error) &&
			            moved.InstallCookedCatalog(cookedRoot, catalog, error),
			        error))
				return;
			CheckOperation(context, LoadAll(moved, catalog.GetRecords(), error),
			    "Load unchanged content from the moved cook root: ", error);
		}

		/**
		 * @brief 撤掉贴图并保留原文件，验证旧文件既不再参与缓存键，也不再占用活动目录中的记录。
		 *
		 * @param[in,out] fixture 持有本次测试输入与临时项目目录的夹具。
		 * @param[in] request 此测试的构建配置，用于重新构建并比较缓存键。
		 * @param[in] texture 要从已安装目录撤下的纹理资产标识。
		 * @param[in,out] manager 要安装更新后目录并验证撤下结果的资产管理器。
		 * @param[in,out] context 记录断言结果的测试上下文。
		 */
		void RemovedTextureContract(Fixture& fixture, const AssetBuildRequest& request, AssetId texture,
		    AssetManager& manager, TestContext& context) {
			auto untextured = Replace(Gltf, "\"images\":[{\"uri\":\"pixels.png\"}],", "");
			untextured = Replace(untextured, "\"textures\":[{\"source\":0}],", "");
			untextured = Replace(untextured, ",\"baseColorTexture\":{\"index\":0}", "");
			std::string error;
			if (!CheckOperation(context, fixture.Store("Raw/model.gltf", untextured, error), error))
				return;

			const auto built = AssetCooker::Build(request);
			CheckOperation(context, built.success && !built.cacheHit, "Cook after removing texture: ", built.error);
			if (!built.success)
				return;
			AssetRegistry complete;
			if (!CheckOperation(context, complete.Replace(built.publishedCatalog, error), error))
				return;
			context.Check(complete.Find(texture) == nullptr && complete.Find(built.primary) != nullptr,
			    "Published catalog withdraws the removed texture but keeps its model");
			if (!CheckOperation(context, manager.InstallCookedCatalog(built.cookedRoot, complete, error, true), error))
				return;
			context.Check(!manager.Request(texture, error), "Runtime cannot request a withdrawn texture");

			/* 专用临时夹具中的旧 PNG 已无引用，改变其字节不应触发模型重建。 */
			if (!CheckOperation(context, fixture.Store("Raw/pixels.png", "obsolete fixture content", error), error))
				return;
			const auto cached = AssetCooker::Build(request);
			context.Check(cached.success && cached.cacheHit && cached.key == built.key,
			    "A detached source file no longer affects the build fingerprint");
		}
	}

	void RunAuthorPipelineTests(TestContext& context) {
		/* 覆盖作者生产链的真实边界，而不只用内存对象绕过 Importer/Cook/Loader。 */
		Fixture fixture;
		std::filesystem::path executable;
		std::string error;
		if (!CheckOperation(context, fixture.Create(error) && GetExecutableDirectory(executable, error), error))
			return;
		const auto shaders = executable / "Resources" / "Shaders";
		if (!CheckOperation(context, fixture.Store("Raw/motion.lua", "return { speed = 2 }", error), error))
			return;
		const auto module = AssetCooker::Build(fixture.Request("motion.lua", shaders));
		if (!CheckOperation(context, module.success, module.error))
			return;

		ScriptAsset source;
		source.source =
		    "return { update = function(dt) local m = require('motion'); scene.translate(m.speed * dt, 0, 0) end }";
		source.modules.emplace("motion", module.primary);
		std::string author;
		if (!CheckOperation(context,
		        WriteScriptAsset(source, author, error) && fixture.Store("Raw/move.script", author, error), error))
			return;
		const auto request = fixture.Request("move.script", shaders);
		const auto built = AssetCooker::Build(request);
		if (!CheckOperation(context, built.success, built.error))
			return;

		JobSystem jobs;
		if (!CheckOperation(context, jobs.Start({2, 64}, error), error))
			return;
		AssetManager manager(jobs);
		if (!CheckOperation(context, RegisterRuntimeAssetLoaders(manager, error), error))
			return;
		const auto install = [&](const AssetBuildResult& result) {
			AssetRegistry catalog;
			return catalog.Replace(result.publishedCatalog, error) &&
			    manager.InstallCookedCatalog(result.cookedRoot, catalog, error, true) &&
			    LoadAll(manager, result.records, error);
		};
		if (!CheckOperation(context, install(built), error))
			return;
		AssetVersionRef<ScriptAsset> first;
		bool fallback = false;
		if (!CheckOperation(context, manager.Acquire(built.primary, first, fallback, error) && !fallback, error))
			return;
		const auto oldModule = first.Get()->moduleVersions.find("motion");
		if (!CheckOperation(
		        context, oldModule != first.Get()->moduleVersions.end(), "Cooked script pins its declared module"))
			return;
		context.Check(oldModule->second.Get()->source == "return { speed = 2 }",
		    "Author import, container dependencies and runtime module decode preserve source bytes");

		if (!CheckOperation(context, fixture.Store("Raw/motion.lua", "return { speed = 3 }", error), error))
			return;
		const auto nextModule = AssetCooker::Build(fixture.Request("motion.lua", shaders));
		if (!CheckOperation(context, nextModule.success, nextModule.error))
			return;
		const auto rebuilt = AssetCooker::Build(request);
		if (!CheckOperation(context, rebuilt.success, rebuilt.error))
			return;
		context.Check(rebuilt.primary == built.primary && rebuilt.key != built.key,
		    "Unchanged author text is recooked when its fixed module dependency changes");
		if (!CheckOperation(context, install(rebuilt), error))
			return;
		AssetVersionRef<ScriptAsset> second;
		if (!CheckOperation(context, manager.Acquire(rebuilt.primary, second, fallback, error) && !fallback, error))
			return;
		const auto newModule = second.Get()->moduleVersions.find("motion");
		context.Check(newModule != second.Get()->moduleVersions.end() && second.GetVersion() != first.GetVersion() &&
		        newModule->second.GetVersion() != oldModule->second.GetVersion() &&
		        newModule->second.Get()->source == "return { speed = 3 }" &&
		        oldModule->second.Get()->source == "return { speed = 2 }",
		    "Republished script and module versions do not mutate the retained old closure");

		std::vector<std::byte> before, after, preservedSource;
		if (!CheckOperation(context,
		        Read(rebuilt.manifest, before, error) && fixture.Store("Raw/move.script", "{bad author JSON", error),
		        error))
			return;
		const auto invalid = AssetCooker::Build(request);
		context.Check(!invalid.success && Read(rebuilt.manifest, after, error) && before == after,
		    "Invalid author source leaves the published catalog unchanged");
		context.Check(Read(request.source, preservedSource, error) &&
		        std::string(reinterpret_cast<const char*>(preservedSource.data()), preservedSource.size()) ==
		            "{bad author JSON",
		    "Failed author Cook never repairs or overwrites source data");
		manager.Shutdown();
		jobs.Shutdown();
	}

	void RunAssetPipelineTests(TestContext& context) {
		std::string error;
		std::filesystem::path executable;
		if (!GetExecutableDirectory(executable, error)) {
			CheckOperation(context, false, error);
			return;
		}
		const auto shaders = executable / "Resources" / "Shaders";
		Fixture fixture;
		if (!fixture.Create(error)) {
			CheckOperation(context, false, error);
			return;
		}
		const auto initial = TriangleBytes();
		const auto pixels = Png();
		if (!fixture.Store("Raw/model.gltf", Gltf, error) || !fixture.StoreBytes("Raw/buffer.bin", initial, error) ||
		    !fixture.StoreBytes("Raw/pixels.png", pixels, error) ||
		    !fixture.StoreBytes("Raw/model.glb", Glb(), error) || !fixture.Store("Raw/triangle.obj", Obj, error) ||
		    !fixture.Store("Raw/triangle.mtl", "newmtl Red\nKd 1 0 0\nNs 30\nmap_Kd pixels.png\n", error) ||
		    !fixture.Store("Raw/triangle.fbx", Fbx, error)) {
			CheckOperation(context, false, error);
			return;
		}
		/* 从原始 glTF 导入场景，核对网格复用、图像和材质之间的关系。 */
		AssetBuildRequest request = fixture.Request("model.gltf", shaders);
		AssetBuildContext gltfContext(request);
		AssetImportResult gltf;
		CheckOperation(context, gltfContext.Initialize(error) && GltfImporter{}.Import(gltfContext, gltf, error),
		    "glTF interleaved/UV/multi-material: ", error);
		if (!gltf.primary.HasValue())
			return;
		const auto model = std::find_if(gltf.assets.begin(), gltf.assets.end(),
		    [](const auto& asset) { return std::holds_alternative<ModelAsset>(asset.value); });
		context.Check(model != gltf.assets.end(), "glTF import contains a domain model");
		if (model == gltf.assets.end())
			return;
		const auto& domain = std::get<ModelAsset>(model->value);
		context.Check(domain.meshes.size() == 4 && domain.meshes[0].mesh == domain.meshes[2].mesh,
		    "Multiple nodes share primitive mesh identities without flattening hierarchy");
		if (domain.meshes.size() < 2 || domain.meshes[0].materials.empty() || domain.meshes[1].materials.empty()) {
			context.Check(false, "The first two primitives must both have a material slot");
			return;
		}
		context.Check(
		    domain.meshes[0].materials[0] != domain.meshes[1].materials[0], "Primitive material mapping is preserved");
		const auto authored = std::find_if(gltf.assets.begin(), gltf.assets.end(),
		    [&](const auto& asset) { return asset.id == domain.meshes[0].materials[0].id; });
		context.Check(authored != gltf.assets.end() && std::holds_alternative<ImportedMaterial>(authored->value),
		    "Domain model keeps authored material IDs, not presentation-only preview IDs");
		context.Check(std::any_of(gltf.mappings.begin(), gltf.mappings.end(),
		                  [](const auto& m) { return m.kind == "node" && m.node.HasValue(); }),
		    "Source node IDs are represented separately from model AssetId");
		const auto image = std::find_if(gltf.assets.begin(), gltf.assets.end(),
		    [](const auto& asset) { return std::holds_alternative<ImageAsset>(asset.value); });
		context.Check(image != gltf.assets.end(), "Referenced glTF image is imported");
		if (image == gltf.assets.end())
			return;
		const auto& decoded = std::get<ImageAsset>(image->value).image;
		context.Check(decoded.width == 2 && decoded.height == 2 && decoded.colorSpace == ImageColorSpace::SRGB &&
		        decoded.pixels.size() == 16 && decoded.pixels[0] == std::byte{255} &&
		        decoded.pixels[1] == std::byte{0} && decoded.pixels[8] == std::byte{0} &&
		        decoded.pixels[10] == std::byte{255},
		    "PNG decode retains top/bottom pixel order and explicit color-space semantics");
		auto again = request;
		again.project.externalSourceRoots.emplace("aaa", fixture.root / "Other");
		AssetBuildContext moved(again);
		context.Check(moved.Initialize(error) && moved.Identity("model") == gltf.primary,
		    "Adding another source root does not renumber existing source identities");
		auto overlap = request;
		overlap.project.cookedRoot = fixture.root / "Raw";
		AssetBuildContext overlapping(overlap);
		context.Check(!overlapping.Initialize(error), "Cooked root cannot overlap original source directories");
		std::filesystem::path outside = request.source;
		context.Check(
		    !gltfContext.Resolve(request.source, "../profiles.json", outside, error) && outside == request.source,
		    "Dependency resolution rejects source-root escape without replacing prior path");
		auto boundedRequest = request;
		boundedRequest.settings.maxSourceBytes = 8;
		AssetBuildContext boundedContext(boundedRequest);
		AssetImportResult boundedOutput = gltf;
		const auto boundedImport =
		    boundedContext.Initialize(error) && GltfImporter{}.Import(boundedContext, boundedOutput, error);
		context.Check(!boundedImport && boundedOutput.primary == gltf.primary,
		    "Source byte bound rejects import before parsing or publishing output");
		CheckCrossFormatImports(fixture, shaders, context);
		TangentCornerContract(request, context);
		MaterialLibraryPathContract(fixture, shaders, context);
		FbxRoughnessContract(fixture, shaders, context);
		RelocatedFbxTextureContract(fixture, shaders, context);

		const auto negative = [&](std::string source) {
			if (!CheckOperation(context, fixture.Store("Raw/bad.gltf", source, error), error))
				return;
			AssetBuildContext input(fixture.Request("bad.gltf", shaders));
			AssetImportResult result = gltf;
			const bool accepted = input.Initialize(error) && GltfImporter{}.Import(input, result, error);
			context.Check(!accepted && result.primary == gltf.primary && !error.empty(),
			    "Invalid glTF rejected with diagnostic while previous output remains intact");
		};
		negative(Replace(Gltf, "buffer.bin", "missing.bin"));
		negative(Replace(Gltf, "pixels.png", "missing.png"));
		negative(Replace(Gltf, "\"count\":3,\"type\":\"VEC3\"", "\"count\":999,\"type\":\"VEC3\""));
		negative(Replace(Gltf, "\"name\":\"First\",\"mesh\":0", "\"name\":\"First\",\"mesh\":0,\"children\":[0]"));
		negative(Replace(Gltf, "\"byteOffset\":24,\"componentType\":5126", "\"byteOffset\":24,\"componentType\":5123"));
		negative(Replace(Gltf, "\"asset\"", "\"extensionsUsed\":[\"EXT_meshopt_compression\"],\"asset\""));
		if (!CheckOperation(
		        context, fixture.Store("Raw/remapped.gltf", Replace(Gltf, "buffer.bin", "missing.bin"), error), error))
			return;
		/* 为缺失的源文件指定映射，并区分有效重映射和损坏的几何输入。 */
		auto remap = fixture.Request("remapped.gltf", shaders);
		remap.settings.dependencyRemaps.emplace("missing.bin", "buffer.bin");
		AssetBuildContext remapContext(remap);
		AssetImportResult remapped;
		context.Check(remapContext.Initialize(error) && GltfImporter{}.Import(remapContext, remapped, error) &&
		        !remapContext.diagnostics.empty(),
		    "Explicit dependency remapping resolves without editing the source document");
		auto badIndices = initial;
		badIndices[100] = std::byte{9};
		if (!CheckOperation(context, fixture.StoreBytes("Raw/buffer.bin", badIndices, error), error))
			return;
		negative(Gltf);
		if (!CheckOperation(context, fixture.StoreBytes("Raw/buffer.bin", initial, error), error))
			return;
		std::vector<std::byte> sourceBefore;
		if (!CheckOperation(context, Read(request.source, sourceBefore, error), error))
			return;
		const auto cooked = AssetCooker::Build(request);
		CheckOperation(context, cooked.success, "Cook complete catalog: ", cooked.error);
		if (!cooked.success)
			return;
		AssetDependencyGraph graph;
		CheckOperation(context, graph.Build(cooked.records, error), error);
		/* 在已发布的依赖图中构造环，检查失败后仍可使用原有顺序。 */
		const auto originalOrder = graph.Order();
		auto cyclic = cooked.records;
		if (cyclic.empty()) {
			context.Check(false, "Cook did not publish any records");
			return;
		}
		cyclic.front().dependencies.push_back(cyclic.front().id);
		context.Check(!graph.Build(cyclic, error) && graph.Order() == originalOrder,
		    "Invalid dependency replacement preserves the last valid graph");
		const auto cached = AssetCooker::Build(request);
		context.Check(cached.success && cached.cacheHit && cached.key == cooked.key && cached.primary == cooked.primary,
		    "Second build uses verified cache with identical stable identity");
		const auto object = std::find_if(cooked.records.begin(), cooked.records.end(),
		    [](const auto& record) { return record.type.ToString() == "image"; });
		if (object == cooked.records.end()) {
			context.Check(false, "Cook did not publish the fixture image");
			return;
		}
		const auto objectPath = cooked.cookedRoot / std::filesystem::u8path(object->locator.path.RelativePath());
		std::vector<std::byte> objectBytes;
		const auto objectRead = Read(objectPath, objectBytes, error);
		CheckOperation(context, objectRead, error);
		if (!objectRead || objectBytes.empty())
			return;
		const auto shortBytes = std::span(objectBytes).first(objectBytes.size() - 1);
		const auto shortened = AtomicFileWriter::Write(objectPath, shortBytes, error);
		CheckOperation(context, shortened, error);
		if (!shortened)
			return;
		const auto truncated = AssetCooker::Build(request);
		context.Check(!truncated.success && !truncated.cacheHit,
		    "Truncated immutable artifact cannot be accepted as a cache hit or silently replaced");
		const auto restored = AtomicFileWriter::Write(objectPath, objectBytes, error);
		CheckOperation(context, restored, error);
		if (!restored)
			return;
		const auto objectTime = std::filesystem::last_write_time(objectPath);
		std::vector<std::byte> sourceAfter;
		context.Check(Read(request.source, sourceAfter, error) && sourceAfter == sourceBefore,
		    "Import/Cook never edits source bytes");
		std::vector<std::byte> oldManifest;
		if (!CheckOperation(context, Read(cooked.manifest, oldManifest, error), error))
			return;
		CancellationSource cancellation;
		cancellation.Cancel();
		auto cancelledRequest = request;
		cancelledRequest.cancellation = cancellation.Token();
		context.Check(!AssetCooker::Build(cancelledRequest).success, "Cancelled build does not report success");
		std::vector<std::byte> sameManifest;
		context.Check(Read(cooked.manifest, sameManifest, error) && sameManifest == oldManifest,
		    "Cancelled build preserves previous main manifest");
		JobSystem jobs;
		if (!jobs.Start({2, 64}, error)) {
			CheckOperation(context, false, error);
			return;
		}
		AssetManager manager(jobs);
		CheckOperation(context, RegisterRuntimeAssetLoaders(manager, error), error);
		AssetRegistry registry;
		CheckOperation(context,
		    registry.Replace(cooked.publishedCatalog, error) &&
		        manager.InstallCookedCatalog(cooked.cookedRoot, registry, error, true) &&
		        LoadAll(manager, cooked.records, error),
		    "Runtime loads actual cooked domain dependencies: ", error);
		if (manager.GetStatus(cooked.primary) == AssetStatus::Ready)
			SceneContracts(manager, cooked, context);
		AssetVersionRef<ModelAsset> oldModel;
		bool fallback = false;
		if (!CheckOperation(context, manager.Acquire(cooked.primary, oldModel, fallback, error) && !fallback, error))
			return;
		auto changed = TriangleBytes();
		changed[27] = std::byte{0x3f};
		if (!CheckOperation(context, fixture.StoreBytes("Raw/buffer.bin", changed, error), error))
			return;
		const auto rebuilt = AssetCooker::Build(request);
		context.Check(
		    rebuilt.success && !rebuilt.cacheHit && rebuilt.primary == cooked.primary && rebuilt.key != cooked.key,
		    "Dependency content change invalidates source closure while logical identity remains stable");
		if (rebuilt.success) {
			const auto first = std::find_if(
			    cooked.records.begin(), cooked.records.end(), [&](const auto& r) { return r.id == cooked.primary; });
			const auto next = std::find_if(
			    rebuilt.records.begin(), rebuilt.records.end(), [&](const auto& r) { return r.id == cooked.primary; });
			context.Check(
			    first != cooked.records.end() && next != rebuilt.records.end() && first->version != next->version,
			    "Model content version includes exact mesh dependency versions");
			const auto reused = std::find_if(rebuilt.records.begin(), rebuilt.records.end(),
			    [&](const auto& record) { return record.id == object->id; });
			context.Check(reused != rebuilt.records.end() && reused->contentHash == object->contentHash &&
			        reused->locator.path == object->locator.path && reused->locator.kind == object->locator.kind &&
			        std::filesystem::last_write_time(objectPath) == objectTime,
			    "Unchanged image artifact is reused without a write while the geometry closure changes");
			CheckOperation(context,
			    registry.Replace(rebuilt.publishedCatalog, error) &&
			        manager.InstallCookedCatalog(rebuilt.cookedRoot, registry, error, true) &&
			        LoadAll(manager, rebuilt.records, error),
			    error);
			AssetVersionRef<ModelAsset> newModel;
			context.Check(manager.Acquire(rebuilt.primary, newModel, fallback, error) && oldModel.HasValue() &&
			        oldModel.GetVersion() != newModel.GetVersion() &&
			        oldModel.Get()->nodes.size() == newModel.Get()->nodes.size(),
			    "Reload keeps old model snapshot alive while publishing new version");
		}
		/* 在写入不可变产物时取消，主目录应仍指向上一次成功构建。 */
		auto scale = request;
		scale.settings.unitScale = 2;
		CancellationSource midCancel;
		scale.cancellation = midCancel.Token();
		scale.progress = [&](std::string_view phase, size_t, size_t) {
			if (phase == "Writing immutable cooked artifacts")
				midCancel.Cancel();
		};
		if (!CheckOperation(context, Read(cooked.manifest, oldManifest, error), error))
			return;
		const auto stopped = AssetCooker::Build(scale);
		context.Check(!stopped.success && Read(cooked.manifest, sameManifest, error) && sameManifest == oldManifest,
		    "Mid-build cancellation cannot publish an incomplete batch");
		{
			SceneManager scenes;
			Editor::BuildPanel panel(jobs, manager, scenes);
			CheckOperation(context, panel.Start(request, error), error);
			panel.CloseProject();
			jobs.CollectCompleted();
			context.Check(!panel.Busy() && jobs.IsRunning(),
			    "Project close waits only its own task and keeps shared scheduler running");
		}
		RelocatedImageContract(fixture, *object, cooked.cookedRoot, jobs, context);
		RemovedTextureContract(fixture, request, object->id, manager, context);
		manager.Shutdown();
		jobs.Shutdown();
		LargeImageContract(context);
		AssetBuildRequest parsed;
		std::vector<std::string> args{"--cook-assets", "--project", AssetPathText(fixture.root / "Fixture.catalpaproj"),
		    "--source", AssetPathText(request.source), "--shader-root", AssetPathText(shaders), "--remap",
		    "missing.png=pixels.png"};
		context.Check(AssetCompilerOptions::Parse(args, parsed, error) &&
		        parsed.settings.dependencyRemaps.contains("missing.png"),
		    "CLI accepts explicit roots and remaps through common options parser");
		args.push_back("--unknown");
		args.push_back("x");
		context.Check(!AssetCompilerOptions::Parse(args, parsed, error), "Unknown command option is not ignored");
		LogSubscription subscription;
		if (!Log::GetCoreLogger() || Log::GetCoreLogger()->sinks().empty()) {
			context.Check(false, "Core logger must have a console sink");
			return;
		}
		const auto console = Log::GetCoreLogger()->sinks().front();
		const auto consoleLevel = console->level();
		console->set_level(spdlog::level::off);
		for (int i = 0; i < 2100; ++i)
			CAT_CORE_TRACE("Bounded log test {0}", i);
		console->set_level(consoleLevel);
		uint64_t dropped = 0;
		const auto logs = subscription.Drain(2048, dropped);
		context.Check(
		    logs.size() <= 2048 && dropped >= 52, "Log subscription is bounded and reports overwritten records");
	}
}
