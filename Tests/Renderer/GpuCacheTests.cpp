#include "TestRunner.h"
#include "Support/FakeRHI.h"
#include "Renderer/Resources/GeometryGpuCache.h"
#include "Renderer/Resources/TextureGpuCache.h"
#include "Renderer/Resources/MaterialGpuCache.h"
#include "Renderer/Resources/PipelineLibrary.h"
#include "Renderer/RendererFrameContext.h"
#include "Renderer/Lighting/LightData.h"
#include "Renderer/Uploads/UploadScheduler.h"
#include "Geometry/PrimitiveMesh.h"
#include "Materials/MaterialValidation.h"
#include "Assets/AssetContainerReader.h"
#include "Core/PlatformServices.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Shaders/ShaderLibrary.h"

namespace Catalpa::Tests {
	namespace {
		/* 只统计资源的逻辑所有权及失败路径。GPU 执行、提交完成和原生内存释放须另行验证。 */
		class CacheDevice final : public FakeDevice {
		public:
			uint32_t createdBuffers = 0, releasedBuffers = 0, createdTextures = 0, releasedTextures = 0,
			         createdPipelines = 0, releasedPipelines = 0;

			RHIStatus CreateBuffer(const RHIBufferDesc& desc, BufferHandle& out) override {
				RHIBufferStateSnapshot state;
				state.desc = desc;
				out = AddBuffer(state);
				++createdBuffers;
				return {};
			}

			RHIStatus WriteBuffer(BufferHandle, uint64_t, std::span<const std::byte>) override { return {}; }

			void ReleaseBuffer(BufferHandle value) override {
				if (value.HasValue())
					++releasedBuffers;
			}

			RHIFormatSupport GetFormatSupport(PixelFormat) const override {
				return {.sampledImage = true, .linearFiltering = true, .transferDestination = true};
			}

			RHIStatus CreateTexture(const RHITextureDesc&, TextureHandle& out) override {
				out = {GetDeviceId(), {createdTextures++, 1}};
				return {};
			}

			RHIStatus CreateTextureView(
			    TextureHandle texture, const RHITextureViewDesc&, TextureViewHandle& out) override {
				out = {texture.device, {texture.slot.index, texture.slot.generation}};
				return {};
			}

			void ReleaseTexture(TextureHandle value) override {
				if (value.HasValue())
					++releasedTextures;
			}

			RHIStatus CreateGraphicsPipeline(const RHIGraphicsPipelineDesc&, GraphicsPipelineHandle& out) override {
				out = {GetDeviceId(), {createdPipelines++, 1}};
				return {};
			}

			void ReleaseGraphicsPipeline(GraphicsPipelineHandle value) override {
				if (value.HasValue())
					++releasedPipelines;
			}
		};

		void CheckInvalidGeometryUploads(TestContext& test) {
			CacheDevice device;
			UploadScheduler uploads(device);
			const std::array<std::byte, 4> bytes{};
			const auto rejected = [&](std::span<const std::byte> data, RHIBufferUsage usage, const char* description) {
				/* 此非空哨兵用于检查失败后的输出，不持有实际分配的资源。 */
				BufferHandle output{device.GetDeviceId(), {0, 1}};
				const auto status = uploads.CreateBuffer(data, usage, output);
				test.Check(
				    status.code == RHIErrorCode::InvalidDescription && !output.HasValue() && device.createdBuffers == 0,
				    description);
			};

			rejected({}, RHIBufferUsage::Vertex | RHIBufferUsage::Storage,
			    "Empty geometry upload is rejected before allocating a buffer");
			rejected(bytes, RHIBufferUsage::None, "Geometry upload requires an explicit vertex or index role");
			rejected(bytes, RHIBufferUsage::Storage, "Storage-only upload cannot bypass the geometry upload contract");
			for (const auto extra : {RHIBufferUsage::Uniform, RHIBufferUsage::Indirect, RHIBufferUsage::TransferSource,
			         RHIBufferUsage::TransferDestination, static_cast<RHIBufferUsage>(1u << 31)}) {
				rejected(bytes, RHIBufferUsage::Vertex | extra,
				    "Unsupported geometry usage bits are rejected without publishing or allocating a buffer");
			}
		}
	}

	void RunGpuCacheTests(TestContext& test) {
		CheckInvalidGeometryUploads(test);
		CacheDevice device;
		UploadScheduler uploads(device);
		std::string error;
		MeshData cube;
		MeshResourceRef mesh;
		test.Check(CreateCubeMesh(1.0f, cube).IsSuccess() && CreateMeshResource(cube, mesh).IsSuccess(),
		    "Cache fixture uses validated geometry");
		if (!mesh)
			return;
		const auto id = AssetId::FromWords(0x1234, 1);
		auto data = std::make_shared<const MeshAsset>(MeshAsset{mesh, {"default"}});
		AssetVersionRef<MeshAsset> version;
		test.Check(
		    AssetVersionRef<MeshAsset>::TryCreate(id, 1, data, version, error), "Geometry cache gets fixed version");
		GeometryGpuCache geometry(device, uploads);
		std::shared_ptr<GeometryGpuResource> a, b;
		test.Check(geometry.Acquire(version, a).IsSuccess() && geometry.Acquire(version, b).IsSuccess() && a == b,
		    "Same geometry version has one GPU owner");
		test.Check(
		    device.createdBuffers == 4, "One vertex/index upload has exactly two destination and two staging buffers");

		/* 真实几何缓存必须同时支持顶点读取和计算读取，不能只让单独的上传入口通过。 */
		RHIBufferStateSnapshot vertexState, indexState;
		const bool haveVertices = a && device.GetBufferState(a->vertices, vertexState).IsSuccess();
		const bool haveIndices = a && device.GetBufferState(a->indices, indexState).IsSuccess();
		test.Check(haveVertices && vertexState.desc.memory == RHIMemoryDomain::Device &&
		        vertexState.desc.usage ==
		            (RHIBufferUsage::Vertex | RHIBufferUsage::Storage | RHIBufferUsage::TransferDestination),
		    "Geometry vertex destination preserves storage access and receives transfer writes");
		test.Check(haveIndices && indexState.desc.memory == RHIMemoryDomain::Device &&
		        indexState.desc.usage == (RHIBufferUsage::Index | RHIBufferUsage::TransferDestination),
		    "Geometry index destination retains its index and transfer roles");
		size_t stagingCount = 0;
		bool validStaging = true;
		for (const auto& buffer : device.buffers) {
			if (buffer.desc.memory != RHIMemoryDomain::Upload)
				continue;
			++stagingCount;
			validStaging &= buffer.desc.usage == RHIBufferUsage::TransferSource;
		}
		test.Check(stagingCount == 2 && validStaging,
		    "Geometry staging buffers remain upload-memory transfer sources without storage access");

		a.reset();
		b.reset();
		geometry.Prune();
		test.Check(geometry.Size() == 1 && device.releasedBuffers == 0,
		    "Unreferenced pending upload keeps target owners alive");
		AssetVersionRef<MeshAsset> collision;
		AssetVersionRef<MeshAsset>::TryCreate(id, 1, std::make_shared<const MeshAsset>(*data), collision, error);
		test.Check(
		    !geometry.Acquire(collision, a).IsSuccess(), "Conflicting payload for one identity/version is rejected");
		uploads.Shutdown();
		geometry.Clear();
		test.Check(device.releasedBuffers == 4, "Cancellation shutdown releases staging before cached destinations");

		ImageAsset image;
		image.image.width = 1;
		image.image.height = 1;
		image.image.format = PixelFormat::RGBA8_UNorm;
		image.image.pixels.assign(4, std::byte{255});
		image.image.subresources.push_back({0, 0, 0, 1, 1, 1, 0, 4, 4, 4});
		AssetVersionRef<ImageAsset> imageVersion;
		AssetVersionRef<ImageAsset>::TryCreate(
		    AssetId::FromWords(0x1234, 2), 1, std::make_shared<const ImageAsset>(image), imageVersion, error);
		TextureGpuCache textures(device, uploads);
		std::shared_ptr<TextureGpuResource> textureA, textureB;
		test.Check(textures.Acquire(imageVersion, textureA).IsSuccess() &&
		        textures.Acquire(imageVersion, textureB).IsSuccess() && textureA == textureB,
		    "Image version reuses exactly one texture/view");
		test.Check(device.createdTextures == 1, "Shared image does not create duplicate GPU textures");
		uploads.Shutdown();
		textures.Clear();
		textureA.reset();
		test.Check(device.releasedTextures == 0, "A retained texture lease survives cache invalidation");
		textureB.reset();
		test.Check(device.releasedTextures == 1, "Last texture owner retires through RHI release");

		AssetId shaderId;
		ShaderLibrary::BuiltinId("Unlit.slang", shaderId, error);
		std::shared_ptr<const MaterialInstance> instance;
		test.Check(CreateBuiltinMaterialInstance(
		               BuiltinMaterialKind::Unlit, AssetId::FromWords(0x1234, 3), 1, shaderId, instance, error),
		    "Unlit material factory");
		if (!instance)
			return;
		AssetVersionRef<MaterialInstance> materialVersion;
		AssetVersionRef<MaterialInstance>::TryCreate(
		    AssetId::FromWords(0x1234, 4), 1, instance, materialVersion, error);
		MaterialGpuCache materials(device);
		std::shared_ptr<MaterialGpuResource> materialA, materialB;
		const auto before = device.createdBuffers;
		test.Check(materials.Acquire(materialVersion, materialA).IsSuccess() &&
		        materials.Acquire(materialVersion, materialB).IsSuccess() && materialA == materialB,
		    "Fixed material version reuses immutable parameter buffer");
		test.Check(device.createdBuffers == before + 1, "Material cache owns only Material UBO, not Frame/View/Draw");
		materials.Clear();
		materialA.reset();
		materialB.reset();

		/* 使用本次构建生成的最终容器和元数据，CPU 替身不执行着色器。 */
		std::filesystem::path root;
		test.Check(GetExecutableDirectory(root, error), error);
		VirtualFileSystem files;
		if (!files.MountReadOnly("shaders", root / "Resources" / "Shaders", 0, error)) {
			test.Check(false, error);
			return;
		}
		std::vector<AssetRecord> catalog;
		if (!ShaderLibrary::ReadBuiltinCatalog(root / "Resources" / "Shaders", "shaders", catalog, error)) {
			test.Check(false, error);
			return;
		}
		/* 统一从当前manifest读取真实产物；不执行SDK，也不改动生成文件。 */
		const auto readShader = [&](std::string_view source, AssetVersionRef<ShaderAsset>& output) {
			AssetId shaderId;
			if (!ShaderLibrary::BuiltinId(source, shaderId, error))
				return false;
			const auto found =
			    std::find_if(catalog.begin(), catalog.end(), [&](const auto& r) { return r.id == shaderId; });
			if (found == catalog.end()) {
				error = "Current shader manifest is missing " + std::string(source);
				return false;
			}
			std::vector<std::byte> bytes;
			AssetContainer container;
			std::shared_ptr<const ShaderAsset> shader;
			if (!files.ReadFile(found->locator.path, 16 * 1024 * 1024, bytes, error) ||
			    !AssetContainerReader::Read(bytes, container, error) ||
			    !ShaderLibrary::DecodePayload(container.payload, shader, error))
				return false;
			if (container.id != found->id || container.version != found->version) {
				error = "Shader identity differs from current manifest";
				return false;
			}
			return AssetVersionRef<ShaderAsset>::TryCreate(container.id, container.version, shader, output, error);
		};
		AssetVersionRef<ShaderAsset> shaderVersion;
		if (!readShader("ImGui.slang", shaderVersion)) {
			test.Check(false, error);
			return;
		}
		const std::array<VertexAttribute, 3> attributes{
		    {{0, VertexFormat::Float2, 0}, {1, VertexFormat::Float2, 8}, {2, VertexFormat::Float4, 16}}};
		const std::array<RHIBindingLayoutEntry, 2> bindings{
		    {{0, RHIBindingType::SampledTexture, RHIShaderVisibility::Fragment},
		        {1, RHIBindingType::Sampler, RHIShaderVisibility::Fragment}}};
		RHIGraphicsPipelineDesc desc;
		desc.vertex = {shaderVersion, RHIShaderStage::Vertex, "vertexMain"};
		desc.fragment = {shaderVersion, RHIShaderStage::Fragment, "fragmentMain"};
		desc.colorFormat = PixelFormat::RGBA8_SRGB;
		desc.vertexLayout = {32, attributes};
		desc.pushConstants = {32, RHIShaderVisibility::VertexFragment};
		desc.bindingLayout = {bindings};
		test.Check(ValidateRHIPipelineShaderInterface(desc, error),
		    "Real cooked UI metadata agrees with the host pipeline ABI");
		auto integerUi = desc;
		integerUi.colorFormat = PixelFormat::R32_UInt;
		test.Check(!ValidateRHIPipelineShaderInterface(integerUi, error),
		    "Float4 UI fragment output cannot be bound to an unsigned integer picking target");
		PipelineLibrary pipelines(device);
		std::shared_ptr<PipelineResource> pipelineA, pipelineB;
		test.Check(pipelines.Acquire(desc, pipelineA).IsSuccess() && pipelines.Acquire(desc, pipelineB).IsSuccess() &&
		        pipelineA == pipelineB,
		    "Complete pipeline key reuses native owner");
		test.Check(device.createdPipelines == 1, "Repeated descriptor does not create a second pipeline");
		desc.colorFormat = PixelFormat::BGRA8_SRGB;
		std::shared_ptr<PipelineResource> other;
		test.Check(pipelines.Acquire(desc, other).IsSuccess() && other != pipelineA,
		    "Attachment format participates in the pipeline key");
		desc.pushConstants.size = 16;
		test.Check(!pipelines.Acquire(desc, other).IsSuccess(),
		    "Incorrect reflected ABI never reaches backend pipeline creation");
		pipelines.Clear();
		pipelineA.reset();
		pipelineB.reset();
		other.reset();
		test.Check(device.createdPipelines == device.releasedPipelines,
		    "All pipeline logical owners release through existing RHI");

		AssetVersionRef<ShaderAsset> unlit, depth, pbr;
		if (!readShader("Unlit.slang", unlit) || !readShader("Depth.slang", depth) || !readShader("Pbr.slang", pbr)) {
			test.Check(false, error);
			return;
		}
		const auto* pbrFragment = FindShaderEntry(pbr.Get()->metadata, "fragmentMain", ShaderStage::Fragment);
		for (uint32_t binding : {13u, 14u, 15u}) {
			const auto found = pbrFragment
			    ? std::find_if(pbrFragment->bindings.begin(), pbrFragment->bindings.end(),
			          [&](const auto& value) { return value.binding == binding && value.set == 0; })
			    : std::vector<ShaderBinding>::const_iterator{};
			test.Check(pbrFragment && found != pbrFragment->bindings.end() &&
			        found->imageDimension ==
			            (binding == 15 ? ShaderImageDimension::Texture2D : ShaderImageDimension::Cube),
			    "Real cooked PBR metadata retains Cube IBL versus 2D BRDF texture dimensions");
		}
		const auto* unlitFragment = FindShaderEntry(unlit.Get()->metadata, "fragmentMain", ShaderStage::Fragment);
		bool fogMatches = false;
		if (unlitFragment) {
			for (const auto& binding : unlitFragment->bindings) {
				if (binding.binding != 10 || binding.set != 0 || binding.blockSize != sizeof(PackedLightingData))
					continue;
				for (const auto& member : binding.members)
					if (member.name == "fogColorDensity" &&
					    member.offset == offsetof(PackedLightingData, fogColorDensity) && member.size == 16)
						fogMatches = true;
			}
		}
		test.Check(
		    fogMatches, "Real cooked Unlit distance fog consumes the same 2160-byte Lighting UBO tail as the renderer");
		const auto validateFrame = [&](const ShaderMetadata& metadata) {
			return ValidateFrameShaderInterface(metadata, "vertexMain", "fragmentMain", error);
		};
		const bool frameMatches = validateFrame(unlit.Get()->metadata);
		test.Check(
		    frameMatches, "Actual cooked Unlit uses the same Frame/View/Draw ABI validator as Renderer: " + error);
		if (!frameMatches)
			return;

		/* 只改动测试副本。包装数组和直接数组有相同的内存 ABI，原始元数据保留实际 SPIR-V 结构。 */
		auto flattened = unlit.Get()->metadata;
		for (auto& entry : flattened.entries)
			for (auto& binding : entry.bindings)
				for (auto& member : binding.members)
					if ((member.name == "viewProjectionColumns" || member.name == "worldColumns" ||
					        member.name == "normalColumns") &&
					    member.type.scalar == ShaderScalarKind::Structure && member.members.size() == 1) {
						auto leaf = member.members.front();
						leaf.name = member.name;
						leaf.offset = member.offset;
						member = std::move(leaf);
					}
		test.Check(validateFrame(flattened), "Equivalent direct float4 arrays retain the complete host byte layout");
		const auto viewColumns = [](ShaderMetadata& metadata) -> ShaderBlockMember* {
			for (auto& entry : metadata.entries)
				if (entry.stage == ShaderStage::Vertex && entry.name == "vertexMain")
					for (auto& binding : entry.bindings)
						if (binding.set == 0 && binding.binding == 1)
							for (auto& member : binding.members)
								if (member.name == "viewProjectionColumns")
									return &member;
			return nullptr;
		};
		auto fixture = unlit.Get()->metadata;
		auto* columns = viewColumns(fixture);
		if (!columns) {
			test.Check(false, "Actual Unlit exposes View columns");
			return;
		}
		columns->offset += 16;
		test.Check(!validateFrame(fixture), "Changed outer member offset cannot pass Frame/View/Draw ABI");
		fixture = unlit.Get()->metadata;
		columns = viewColumns(fixture);
		auto* leaf = columns->type.scalar == ShaderScalarKind::Structure ? &columns->members.front() : columns;
		leaf->arrayStride = 32;
		test.Check(!validateFrame(fixture), "Changed matrix-column stride is rejected");
		fixture = unlit.Get()->metadata;
		columns = viewColumns(fixture);
		leaf = columns->type.scalar == ShaderScalarKind::Structure ? &columns->members.front() : columns;
		leaf->type.scalar = ShaderScalarKind::SignedInteger;
		test.Check(!validateFrame(fixture), "Same-sized integer columns cannot replace float columns");
		fixture = unlit.Get()->metadata;
		columns = viewColumns(fixture);
		leaf = columns->type.scalar == ShaderScalarKind::Structure ? &columns->members.front() : columns;
		leaf->matrixStride = 16;
		test.Check(!validateFrame(fixture), "A matrix decoration cannot reinterpret a float4 array");
		fixture = unlit.Get()->metadata;
		columns = viewColumns(fixture);
		leaf = columns->type.scalar == ShaderScalarKind::Structure ? &columns->members.front() : columns;
		leaf->rowMajor = true;
		test.Check(!validateFrame(fixture), "Row-major metadata cannot reinterpret explicit host columns");
		fixture = unlit.Get()->metadata;
		columns = viewColumns(fixture);
		if (columns->type.scalar == ShaderScalarKind::Structure) {
			columns->members.front().offset = 16;
			test.Check(!validateFrame(fixture), "Array wrapper data must start at byte zero");
			fixture = unlit.Get()->metadata;
			columns = viewColumns(fixture);
			columns->members.front().name = "other";
			test.Check(!validateFrame(fixture), "Unknown structure wrappers are not silently accepted");
		}

		/* 实际前向管线及深度预通道组合还须通过 RHI 对跨阶段接口和原始反射元数据的检查。 */
		const std::array<RHIBindingLayoutEntry, 7> sceneBindings{
		    {{0, RHIBindingType::UniformBuffer, RHIShaderVisibility::Fragment, sizeof(FrameGpuParameters)},
		        {1, RHIBindingType::UniformBuffer, RHIShaderVisibility::VertexFragment, sizeof(ViewGpuParameters)},
		        {2, RHIBindingType::UniformBuffer, RHIShaderVisibility::VertexFragment, sizeof(DrawGpuParameters)},
		        {3, RHIBindingType::UniformBuffer, RHIShaderVisibility::Fragment, 80},
		        {4, RHIBindingType::SampledTexture, RHIShaderVisibility::Fragment},
		        {18, RHIBindingType::Sampler, RHIShaderVisibility::Fragment},
		        {10, RHIBindingType::UniformBuffer, RHIShaderVisibility::Fragment, sizeof(PackedLightingData)}}};
		RHIGraphicsPipelineDesc scenePipeline;
		scenePipeline.vertex = {unlit, RHIShaderStage::Vertex, "vertexMain"};
		scenePipeline.fragment = {unlit, RHIShaderStage::Fragment, "fragmentMain"};
		/* GPU派生流与原mesh布局分开；按实际反射消费固定canonical属性，不再复用旧pos/normal筛选。 */
		std::vector<VertexAttribute> sceneInputs;
		const auto canonical = GeometryGpuResource::GetVertexLayout();
		const auto* vertexMetadata = FindShaderEntry(unlit.Get()->metadata, "vertexMain", ShaderStage::Vertex);
		if (!vertexMetadata) {
			test.Check(false, "Actual Unlit vertex entry is present");
			return;
		}
		for (const auto& input : vertexMetadata->inputs) {
			const auto found = std::find_if(canonical.attributes.begin(), canonical.attributes.end(),
			    [&](const auto& value) { return value.location == input.location; });
			if (found == canonical.attributes.end()) {
				test.Check(false, "Real mesh shader input belongs to the canonical GPU stream");
				return;
			}
			sceneInputs.push_back(*found);
		}
		scenePipeline.vertexLayout = {canonical.stride, sceneInputs};
		scenePipeline.bindingLayout = {sceneBindings};
		scenePipeline.colorFormat = PixelFormat::RGBA32_Float;
		scenePipeline.depthFormat = PixelFormat::D32_Float;
		const bool forwardMatches = ValidateRHIPipelineShaderInterface(scenePipeline, error);
		test.Check(forwardMatches, "Actual cooked forward descriptor/stage ABI: " + error);
		const std::array<RHIBindingLayoutEntry, 6> depthBindings{
		    {{0, RHIBindingType::UniformBuffer, RHIShaderVisibility::Fragment, sizeof(FrameGpuParameters)},
		        {1, RHIBindingType::UniformBuffer, RHIShaderVisibility::Vertex, sizeof(ViewGpuParameters)},
		        {2, RHIBindingType::UniformBuffer, RHIShaderVisibility::VertexFragment, sizeof(DrawGpuParameters)},
		        {3, RHIBindingType::UniformBuffer, RHIShaderVisibility::Fragment, 80},
		        {4, RHIBindingType::SampledTexture, RHIShaderVisibility::Fragment},
		        {18, RHIBindingType::Sampler, RHIShaderVisibility::Fragment}}};
		scenePipeline.fragment = {depth, RHIShaderStage::Fragment, "fragmentMain"};
		scenePipeline.colorFormat = PixelFormat::Unknown;
		scenePipeline.bindingLayout = {depthBindings};
		const bool depthMatches = ValidateRHIPipelineShaderInterface(scenePipeline, error);
		test.Check(
		    depthMatches, "Actual Unlit vertex and Depth fragment preserve prepass descriptor/stage ABI: " + error);
		auto pbrPrepass = scenePipeline;
		pbrPrepass.vertex = {pbr, RHIShaderStage::Vertex, "vertexMain"};
		std::vector<VertexAttribute> pbrInputs;
		const auto* pbrVertex = FindShaderEntry(pbr.Get()->metadata, "vertexMain", ShaderStage::Vertex);
		if (!pbrVertex) {
			test.Check(false, "Actual PBR vertex entry is present for an exact-VS prepass");
			return;
		}
		for (const auto& input : pbrVertex->inputs) {
			const auto found = std::find_if(canonical.attributes.begin(), canonical.attributes.end(),
			    [&](const auto& value) { return value.location == input.location; });
			if (found == canonical.attributes.end()) {
				test.Check(false, "PBR prepass input belongs to canonical geometry");
				return;
			}
			pbrInputs.push_back(*found);
		}
		pbrPrepass.vertexLayout = {canonical.stride, pbrInputs};
		test.Check(ValidateRHIPipelineShaderInterface(pbrPrepass, error),
		    "Actual PBR vertex bytecode pairs with the common alpha-tested Depth fragment: " + error);
		std::shared_ptr<PipelineResource> exactPrepass;
		test.Check(pipelines.Acquire(pbrPrepass, exactPrepass).IsSuccess() && exactPrepass->vertex.Get() == pbr.Get() &&
		        exactPrepass->fragment.Get() == depth.Get(),
		    "Prepass pipeline retains the exact forward VS owner and a separate Depth FS owner");
		auto colorWithoutOutput = scenePipeline;
		colorWithoutOutput.colorFormat = PixelFormat::RGBA32_Float;
		test.Check(!ValidateRHIPipelineShaderInterface(colorWithoutOutput, error),
		    "A depth-only fragment cannot fabricate a color attachment output");
		AssetVersionRef<ShaderAsset> picking;
		if (!readShader("Picking.slang", picking)) {
			test.Check(false, error);
			return;
		}
		std::vector<VertexAttribute> pickingInputs;
		const auto* pickingVertex = FindShaderEntry(picking.Get()->metadata, "vertexMain", ShaderStage::Vertex);
		if (!pickingVertex) {
			test.Check(false, "Actual picking shader contains a vertex entry");
			return;
		}
		for (const auto& input : pickingVertex->inputs) {
			const auto found = std::find_if(canonical.attributes.begin(), canonical.attributes.end(),
			    [&](const auto& value) { return value.location == input.location; });
			if (found == canonical.attributes.end()) {
				test.Check(false, "Picking shader input belongs to canonical geometry");
				return;
			}
			pickingInputs.push_back(*found);
		}
		RHIGraphicsPipelineDesc pickingPipeline;
		pickingPipeline.vertex = {picking, RHIShaderStage::Vertex, "vertexMain"};
		pickingPipeline.fragment = {picking, RHIShaderStage::Fragment, "fragmentMain"};
		pickingPipeline.colorFormat = PixelFormat::R32_UInt;
		pickingPipeline.depthFormat = PixelFormat::D32_Float;
		pickingPipeline.vertexLayout = {canonical.stride, pickingInputs};
		pickingPipeline.bindingLayout = {depthBindings};
		test.Check(ValidateRHIPipelineShaderInterface(pickingPipeline, error),
		    "Actual uint picking output matches the single-channel unsigned R32 attachment");
		pickingPipeline.colorFormat = PixelFormat::RGBA8_UNorm;
		test.Check(!ValidateRHIPipelineShaderInterface(pickingPipeline, error),
		    "Unsigned picking output cannot be silently reinterpreted as a normalized RGBA color");
	}
}
