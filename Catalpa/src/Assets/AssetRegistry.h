#pragma once

#include "catpch.h"

#include "Core/Base.h"
#include "Assets/AssetRecord.h"

namespace Catalpa {

	/* 尚未登记的依赖仍保留原 ID，供作者工具给出确定的缺失诊断。 */
	struct AssetDependencyIssue {
		AssetId asset;
		AssetId dependency;
	};

	/*
	 * 同步元数据索引；配置线程负责修改，读取期间不允许并发 Replace。
	 * 不创建领域资源、不解析 JSON，不将元数据存在等同于可加载或 GPU 已就绪。
	 * 移动后源表为空。记录与索引一起发布，防止复制失败留下不一致的状态。
	 */
	class CATALPA_API AssetRegistry {
	public:
		AssetRegistry() = default;
		AssetRegistry(const AssetRegistry&) = default;
		AssetRegistry(AssetRegistry&& other) { Swap(other); }
		AssetRegistry& operator=(AssetRegistry other) noexcept {
			Swap(other);
			return *this;
		}

		/**
		 * @brief 校验整批资产记录并替换注册表，记录当前尚未满足的依赖。
		 * 整表验证成功后一次替换；失败保留记录、索引、缺失诊断及已有查询指针。
		 * 允许非自引用的依赖缺失，由 GetMissingDependencies 报告，不阻止检查不完整项目。
		 *
		 * @param[in] records 用于整体替换旧目录的完整资产记录集合。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 整批记录及索引成功建立时为 true；可登记尚未找到的外部依赖。
		 */
		[[nodiscard]] bool Replace(const std::vector<AssetRecord>& records, std::string& error);

		/**
		 * @brief 按资产标识查找已注册记录。
		 * 返回注册表拥有的只读记录；指针仅在本表下一次成功替换/赋值/析构前有效。
		 *
		 * @param[in] id 在当前注册表中查找的稳定资产标识。
		 *
		 * @return 注册表持有的只读记录指针；无效或未注册的标识返回 nullptr。
		 */
		[[nodiscard]] const AssetRecord* Find(AssetId id) const noexcept;
		/**
		 * @brief 按虚拟路径查找注册记录，未命中时返回空指针。
		 *
		 * @param[in] path 待查询的虚拟资产路径。
		 *
		 * @return 注册表所持的匹配路径记录，路径无效或不存在时返回 nullptr。
		 */
		[[nodiscard]] const AssetRecord* Find(const VirtualPath& path) const noexcept;
		/**
		 * @brief 按定位器路径和类型查找注册记录，未命中时返回空指针。
		 *
		 * @param[in] locator 指定路径及定位类型的资产定位器。
		 *
		 * @return 路径和位置类型均相符的注册表记录；未找到或类型不符时返回 nullptr。
		 */
		[[nodiscard]] const AssetRecord* Find(const AssetLocator& locator) const noexcept;
		[[nodiscard]] const std::vector<AssetRecord>& GetRecords() const noexcept { return m_Records; }
		[[nodiscard]] const std::vector<AssetDependencyIssue>& GetMissingDependencies() const noexcept {
			return m_MissingDependencies;
		}

	private:
		void Swap(AssetRegistry& other) noexcept {
			m_Records.swap(other.m_Records);
			m_ById.swap(other.m_ById);
			m_ByPath.swap(other.m_ByPath);
			m_MissingDependencies.swap(other.m_MissingDependencies);
		}

		std::vector<AssetRecord> m_Records;
		std::map<AssetId, size_t> m_ById;
		std::map<std::string, size_t, std::less<>> m_ByPath;
		std::vector<AssetDependencyIssue> m_MissingDependencies;
	};

}
