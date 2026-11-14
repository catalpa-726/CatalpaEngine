#include "catpch.h"

#include "Panels/BrowserThumbnails.h"

#include "AssetPipeline/AssetBuildContext.h"
#include "AssetPipeline/AssetImportRegistry.h"
#include "AssetPipeline/Importers/TextureImporter.h"
#include "Core/Application.h"
#include "Core/Jobs/JobSystem.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Core/Id/StableIdGenerator.h"
#include "Core/Serialization/AuthorJson.h"
#include "Geometry/PrimitiveMesh.h"
#include "Materials/MaterialValidation.h"
#include "Assets/AssetManager.h"
#include "Renderer/RenderSurface.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "Shaders/ShaderAsset.h"
#include "Scene/Components/CameraComponent.h"
#include "Scene/Components/LightComponent.h"
#include "Scene/Components/MeshRendererComponent.h"

namespace Catalpa::Editor {
	namespace {
		bool ReadSource(const std::filesystem::path& root, const std::filesystem::path& path, size_t budget,
		    std::vector<std::byte>& bytes, std::string& error) {
			VirtualFileSystem files;
			VirtualPath file;
			return files.MountReadOnly("thumb", root, 0, error) &&
			    VirtualPath::Parse("thumb:/" + Tools::AssetPathText(path.lexically_relative(root)), file, error) &&
			    files.ReadFile(file, budget, bytes, error);
		}

		/**
		 * @brief 将只读纹理源解码为资产浏览器使用的像素缩略图。
		 * 只派生预览像素；源文件按原格式解码后释放，原图保持只读。
		 *
		 * @param[in] bytes 源图像文件的编码字节，按颜色纹理解码。
		 * @param[in] token 由解码任务和逐行缩放循环读取的取消标记。
		 * @param[in] side 方形缩略图边长，以像素计。
		 * @param[out] pixels 接收居中放置图像的 RGBA8 方形像素缓冲区。
		 * @param[out] error 接收解码或像素格式错误；缩放期间发现任务取消时直接返回失败。
		 *
		 * @return 源图像成功解码并形成缩略图时为 true，取消或解码失败时为 false。
		 */
		bool TextureTile(std::span<const std::byte> bytes, const CancellationToken& token, unsigned side,
		    std::vector<std::byte>& pixels, std::string& error) {
			Tools::AssetBuildRequest request;
			request.settings.maxWorkingBytes = 160 * 1024 * 1024;
			request.cancellation = token;
			Tools::AssetBuildContext context(std::move(request));
			ImageAsset image;
			if (!Tools::TextureImporter::Decode(context, bytes, "thumbnail", ImageSemantic::Color, image, error))
				return false;
			const auto& src = image.image;
			if (src.format != PixelFormat::RGBA8_SRGB || src.subresources.empty()) {
				error = "RGBA8";
				return false;
			}
			const auto& sub = src.subresources.front();
			const auto largest = std::max(sub.width, sub.height);
			const unsigned w = std::max(1u, unsigned(uint64_t(side) * sub.width / largest));
			const unsigned h = std::max(1u, unsigned(uint64_t(side) * sub.height / largest));
			pixels.resize(size_t(side) * side * 4);
			for (unsigned y = 0; y < h; ++y) {
				if (token.IsCancellationRequested())
					return false;
				for (unsigned x = 0; x < w; ++x) {
					const size_t sx = uint64_t(2 * x + 1) * sub.width / (2 * w);
					const size_t sy = uint64_t(2 * y + 1) * sub.height / (2 * h);
					const auto dest = size_t((side - h) / 2 + y) * side + (side - w) / 2 + x;
					std::memcpy(
					    pixels.data() + dest * 4, src.pixels.data() + sub.offset + sy * sub.rowPitch + sx * 4, 4);
				}
			}
			return true;
		}
	}

	void BrowserThumbnails::Reset() {
		m_Job.Cancel();
		if (m_Job.HasValue()) {
			std::string ignored;
			(void)Application::Get().GetJobs().Wait(m_Job, ignored);
		}
		m_Job = {};
		m_Work.reset();
		auto& app = Application::Get();
		if (m_AtlasId.HasValue())
			app.CloseEditorPreview(m_Base);
		if (m_MaterialScene.HasValue() || m_MaterialToken)
			app.CloseEditorPreview(m_Base + 1);
		if (m_MaterialScene.HasValue()) {
			(void)app.GetScenes().Unload(m_MaterialScene);
		}
		m_MaterialScene = {};
		m_Images = {};
		m_Materials = {};
		m_ImageToken = m_MaterialToken = 0;
		m_ImagesDirty = m_MaterialsDirty = false;
		m_Error.clear();
	}

	bool BrowserThumbnails::Start(const std::filesystem::path& root, Tile& tile, bool material) {
		auto work = std::make_shared<Work>();
		work->path = tile.path;
		work->material = material;
		if (!Application::Get().GetJobs().Submit(
		        [root, work](const CancellationToken& token) {
			        std::vector<std::byte> bytes;
			        if (token.IsCancellationRequested() ||
			            !ReadSource(
			                root, work->path, work->material ? 1024 * 1024 : 48 * 1024 * 1024, bytes, work->error))
				        return;
			        if (work->material)
				        work->source.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
			        else
				        (void)TextureTile(bytes, token, Side, work->pixels, work->error);
		        },
		        m_Job, tile.error))
			return false;
		m_Work = std::move(work);
		tile.pending = true;
		return true;
	}

	/**
	 * @brief 解析材质。
	 *
	 * @param[in,out] tile 读取其作者源文本，解析出的材质引用或失败原因写回该缩略图。
	 *
	 * @return 材质与引用纹理均已解析并取得可用于缩略图的固定版本时为 true；依赖仍在加载或解析失败时为 false。
	 */
	bool BrowserThumbnails::ResolveMaterial(Tile& tile) try {
		auto& assets = Application::Get().GetAssets();
		auto acquire = [&](AssetId id, auto& out) {
			if (assets.GetStatus(id) == AssetStatus::Unloaded && !assets.Request(id, tile.error))
				return false;
			const auto state = assets.GetStatus(id);
			if (state != AssetStatus::Ready) {
				if (state == AssetStatus::Failed || state == AssetStatus::Cancelled)
					tile.error = assets.GetDiagnostic(id);
				return false;
			}
			bool fallback = false;
			return assets.Acquire(id, out, fallback, tile.error) && !fallback;
		};
		const auto data = AuthorJson::Parse(tile.source);
		AssetVersionRef<MaterialDefinition> definition;
		std::shared_ptr<const MaterialInstance> material;
		if (data.at("kind") == "definition") {
			std::shared_ptr<const MaterialDefinition> parsed;
			if (!ParseMaterialDefinition(tile.source, parsed, tile.error))
				return false;
			auto resolved = std::make_shared<MaterialDefinition>(*parsed);
			if (!acquire(resolved->shader.id, resolved->shaderVersion))
				return false;
			StableId id;
			if (!GenerateStableId(id, tile.error) ||
			    !AssetVersionRef<MaterialDefinition>::TryCreate(AssetId{id}, 1, resolved, definition, tile.error) ||
			    !CreateMaterialInstance(definition, {}, {}, 1, material, tile.error))
				return false;
		} else {
			if (!acquire(AuthorJson::Identity<AssetId>(data.at("definitionId")), definition))
				return false;
			if (!ParseMaterialInstance(tile.source, definition, material, tile.error))
				return false;
		}
		auto resolved = std::make_shared<MaterialInstance>(*material);
		constexpr std::array<std::string_view, 5> slots{"baseColor_texture", "normal_texture",
		    "metallicRoughness_texture", "emissive_texture", "occlusion_texture"};
		for (size_t i = 0; i < slots.size(); ++i) {
			const auto* value = FindMaterialParameter(*material, slots[i]);
			const auto* image = value ? std::get_if<AssetHandle<ImageAsset>>(value) : nullptr;
			if (!image || !image->HasValue())
				continue;
			if (!acquire(image->id, resolved->textureVersions[i]))
				return false;
			const auto schema = std::find_if(definition.Get()->parameters.begin(), definition.Get()->parameters.end(),
			    [&](const auto& p) { return p.id == slots[i]; });
			if (schema == definition.Get()->parameters.end() ||
			    !ValidateMaterialTexture(*schema, *resolved->textureVersions[i].Get(), tile.error))
				return false;
		}
		StableId unique;
		return GenerateStableId(unique, tile.error) &&
		    AssetVersionRef<MaterialInstance>::TryCreate(
		        AssetId{unique}, resolved->revision, resolved, tile.material, tile.error);
	} catch (const std::exception& e) {
		tile.error = e.what();
		return false;
	}

	void BrowserThumbnails::BeginFrame() {
		if (m_Work && m_Job.IsComplete()) {
			auto& tiles = m_Work->material ? m_Materials : m_Images;
			for (auto& tile : tiles)
				if (tile.path == m_Work->path) {
					tile.pending = false;
					tile.error = m_Work->error.empty() ? m_Job.GetError() : m_Work->error;
					if (m_Work->material)
						tile.source = std::move(m_Work->source);
					else if (!m_Work->pixels.empty()) {
						tile.rgba = std::make_shared<const std::vector<std::byte>>(std::move(m_Work->pixels));
						m_ImagesDirty = true;
					}
					break;
				}
			m_Work.reset();
			m_Job = {};
		}
		for (auto& tile : m_Materials)
			if (!tile.source.empty() && tile.error.empty() && !tile.material.HasValue() && ResolveMaterial(tile)) {
				tile.source.clear();
				m_MaterialsDirty = true;
			}
		if (m_ImagesDirty)
			UploadImages();
		if (m_MaterialsDirty)
			UpdateMaterials();
		if (m_MaterialScene.HasValue()) {
			RenderSurfaceSnapshot snapshot;
			auto& app = Application::Get();
			if (app.GetEditorPreview(m_Base + 1, snapshot)) {
				m_MaterialToken = snapshot.textureToken;
				app.FreezeEditorPreview(m_Base + 1);
				(void)app.GetScenes().Unload(m_MaterialScene);
				m_MaterialScene = {};
			}
		}
	}

	void BrowserThumbnails::UploadImages() {
		constexpr uint32_t dim = Side * Columns;
		auto image = std::make_shared<ImageAsset>();
		auto& v = image->image;
		v.width = v.height = dim;
		v.format = PixelFormat::RGBA8_SRGB;
		v.colorSpace = ImageColorSpace::SRGB;
		v.alphaMode = ImageAlphaMode::Straight;
		v.pixels.resize(size_t(dim) * dim * 4);
		v.subresources.push_back({0, 0, 0, dim, dim, 1, 0, dim * 4, v.pixels.size(), v.pixels.size()});
		for (unsigned i = 0; i < Slots; ++i)
			if (const auto& pixels = m_Images[i].rgba) {
				for (unsigned y = 0; y < Side; ++y) {
					const size_t offset = ((i / Columns * Side + y) * dim + i % Columns * Side) * 4;
					std::memcpy(v.pixels.data() + offset, pixels->data() + size_t(y) * Side * 4, Side * 4);
				}
			}
		if (!m_AtlasId.HasValue() && !GenerateStableId(m_AtlasId.value, m_Error))
			return;
		AssetVersionRef<ImageAsset> fixed;
		auto& app = Application::Get();
		if (AssetVersionRef<ImageAsset>::TryCreate(m_AtlasId, ++m_Revision, image, fixed, m_Error) &&
		    app.SetEditorPreviewImage(m_Base, fixed, 0, 0, 15, m_Error)) {
			RenderSurfaceSnapshot snapshot;
			if (app.GetEditorPreview(m_Base, snapshot))
				m_ImageToken = snapshot.textureToken;
		}
		m_ImagesDirty = false;
	}

	/**
	 * @brief 更新材质。
	 */
	void BrowserThumbnails::UpdateMaterials() {
		auto& app = Application::Get();
		auto& scenes = app.GetScenes();
		if (m_MaterialToken || m_MaterialScene.HasValue())
			app.CloseEditorPreview(m_Base + 1);
		if (m_MaterialScene.HasValue()) {
			(void)scenes.Unload(m_MaterialScene);
			m_MaterialScene = {};
		}
		m_MaterialToken = 0;
		m_MaterialsDirty = false;
		MeshData data;
		MeshResourceRef resource;
		auto status = CreateSphereMeshWithTangents(.43f, data);
		if (status.IsSuccess())
			status = CreateMeshResource(data, resource);
		if (!status.IsSuccess()) {
			m_Error = status.message;
			return;
		}
		auto mesh = std::make_shared<MeshAsset>();
		mesh->mesh = resource;
		mesh->materialSlots = {"preview"};
		AssetVersionRef<MeshAsset> fixed;
		StableId meshId;
		if (!GenerateStableId(meshId, m_Error) ||
		    !AssetVersionRef<MeshAsset>::TryCreate(AssetId{meshId}, 1, mesh, fixed, m_Error))
			return;
		auto world = [&](WorldStatus s) {
			if (!s.IsSuccess())
				m_Error = s.message;
			return s.IsSuccess();
		};
		if (!world(scenes.CreateScene("Material thumbnails", m_MaterialScene)))
			return;
		auto* scene = scenes.GetScene(m_MaterialScene);
		auto create = [&](uint64_t n, Math::TransformTRS pose) {
			return world(scene->QueueCreate(PersistentEntityId::FromWords(m_Base, n), "preview", pose));
		};
		Math::TransformTRS camera, light;
		camera.translation = {0, 0, 4};
		light.rotation = Math::Quat(Math::Vec3(-.52f, -.55f, 0.f));
		if (!create(1, camera) || !create(2, light))
			return;
		for (unsigned i = 0; i < Slots; ++i)
			if (m_Materials[i].material.HasValue()) {
				Math::TransformTRS pose;
				pose.translation = {float(i % Columns) + .5f - float(Columns) * .5f,
				    float(Columns) * .5f - .5f - float(i / Columns), 0};
				if (!create(3 + i, pose))
					return;
			}
		if (!world(scene->CommitPending()))
			return;
		auto entity = [&](uint64_t n) { return scene->Resolve(PersistentEntityId::FromWords(m_Base, n)); };
		CameraComponent lens;
		lens.lens.projection = ProjectionType::Orthographic;
		lens.lens.orthographicHeight = float(Columns);
		LightComponent sun;
		sun.parameters.intensity = 4;
		if (!world(scene->QueueSetCamera(entity(1), lens)) || !world(scene->QueueSetLight(entity(2), sun)))
			return;
		for (unsigned i = 0; i < Slots; ++i)
			if (m_Materials[i].material.HasValue()) {
				MeshRendererComponent renderer;
				renderer.mesh = resource;
				renderer.meshVersion = fixed;
				renderer.materials = {m_Materials[i].material};
				if (!world(scene->QueueSetMeshRenderer(entity(3 + i), renderer)))
					return;
			}
		if (world(scene->CommitPending()))
			(void)app.SetEditorPreview(m_MaterialScene, m_Base + 1, {Columns * Side, Columns * Side}, m_Error);
	}

	bool BrowserThumbnails::Draw(const std::filesystem::path& root, const std::filesystem::path& path, ImVec2 min,
	    ImVec2 max, std::string& diagnostic) {
		const auto ext = Tools::AssetSourceExtension(path);
		const bool mat = ext == ".material";
		if (!mat && ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp" && ext != ".tga")
			return false;
		auto& tiles = mat ? m_Materials : m_Images;
		const auto frame = ImGui::GetFrameCount();
		auto it = std::find_if(tiles.begin(), tiles.end(), [&](const Tile& t) { return t.path == path; });
		if (it == tiles.end()) {
			if (m_Work)
				return false;
			it = std::min_element(
			    tiles.begin(), tiles.end(), [](const Tile& a, const Tile& b) { return a.frame < b.frame; });
			if (it->frame >= frame - 1 && !it->path.empty())
				return false;
			*it = {};
			it->path = path;
			it->frame = frame;
			Start(root, *it, mat);
		}
		it->frame = frame;
		diagnostic = it->error.empty() ? m_Error : it->error;
		const auto token = mat ? m_MaterialToken : m_ImageToken;
		if (!token || (mat ? !it->material.HasValue() : !it->rgba))
			return false;
		const auto index = unsigned(it - tiles.begin());
		const float dx = float(index % Columns) / Columns, dy = float(index / Columns) / Columns;
		const float edge = .5f / (Columns * Side), span = 1.f / Columns;
		ImGui::GetWindowDrawList()->AddImage(
		    ImTextureRef(ImTextureID(token)), min, max, {dx + edge, dy + edge}, {dx + span - edge, dy + span - edge});
		return true;
	}
}
