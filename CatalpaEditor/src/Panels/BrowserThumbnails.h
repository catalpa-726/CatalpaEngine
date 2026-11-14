#pragma once

#include "catpch.h"

#include "Core/Jobs/JobHandle.h"
#include "Materials/MaterialInstance.h"
#include "Scene/SceneHandle.h"
#include "Scene/PersistentEntityId.h"
#include "imgui.h"

namespace Catalpa::Editor {
	/* 两张独立图集：解码后的文件纹理与引擎实际着色的材质球共用各自的 surface。 */
	class BrowserThumbnails {
	public:
		explicit BrowserThumbnails(uint64_t id) : m_Base(id) {}
		/**
		 * @brief 释放缩略图任务及预览资源，清理缓存状态。
		 */
		void Reset();
		/**
		 * @brief 开始帧。
		 */
		void BeginFrame();
		/**
		 * @brief 绘制给定资源的缩略图。
		 *
		 * @param[in] root 根目录路径。
		 * @param[in] path 文件系统中的目标路径。
		 * @param[in] min 缩略图绘制矩形左上角的屏幕坐标。
		 * @param[in] max 缩略图绘制矩形右下角的屏幕坐标。
		 * @param[out] diagnostic 接收该图块加载或预览失败时的原因。
		 *
		 * @return 本帧已为该文件绘制有效缩略图时为 true；资源尚未就绪或不可预览时为 false。
		 */
		bool Draw(const std::filesystem::path& root, const std::filesystem::path& path, ImVec2 min, ImVec2 max,
		    std::string& diagnostic);

	private:
		static constexpr unsigned Side = 64, Columns = 4, Slots = Columns * Columns;
		struct Tile {
			std::filesystem::path path;
			int frame = -1;
			std::shared_ptr<const std::vector<std::byte>> rgba;
			std::string source, error;
			AssetVersionRef<MaterialInstance> material;
			bool pending = false;
		};
		struct Work {
			std::filesystem::path path;
			bool material = false;
			std::vector<std::byte> pixels;
			std::string source, error;
		};
		void UploadImages();
		void UpdateMaterials();
		bool Start(const std::filesystem::path&, Tile&, bool material);
		bool ResolveMaterial(Tile&);
		uint64_t m_Base;
		uint64_t m_Revision = 0;
		bool m_ImagesDirty = false, m_MaterialsDirty = false;
		std::array<Tile, Slots> m_Images, m_Materials;
		std::shared_ptr<Work> m_Work;
		JobHandle m_Job;
		SceneHandle m_MaterialScene;
		AssetId m_AtlasId;
		uint64_t m_ImageToken = 0, m_MaterialToken = 0;
		std::string m_Error;
	};
}
