#pragma once
#include "Core/Serialization/Archive.h"
#include "Core/Base.h"

namespace Catalpa {
	/* 迁移按单个版本前进，只在完整成功后交付候选值树。 */
	class CATALPA_API SchemaMigrationRegistry {
	public:
		using Step = std::function<bool(ArchiveValue&, std::string&)>;
		/**
		 * @brief 按类型和起始版本登记一个模式升级步骤。
		 *
		 * @param[in] type 模式类型标识。
		 * @param[in] from 该步骤接受的起始版本号。
		 * @param[in] step 负责迁移到下一版本的回调。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Register(std::string type, uint32_t from, Step, std::string& error);
		/**
		 * @brief 按版本顺序执行已登记的迁移，成功后交付目标版本数据。
		 *
		 * @param[in] type 模式类型标识。
		 * @param[in] target 期望到达的版本号。
		 * @param[in] source 包含 schemaVersion 的原始归档。
		 * @param[out] output 成功时写入迁移结果。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Migrate(std::string_view type, uint32_t target, const ArchiveValue& source, ArchiveValue& output,
		    std::string& error) const;

	private:
		std::map<std::pair<std::string, uint32_t>, Step> m_Steps;
	};
}
