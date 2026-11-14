#pragma once
#include "catpch.h"

#include "Core/Base.h"

namespace Catalpa {
	/* 领域无关的 UTF-8 文本表。失败保留旧语言；调用方单独维护稳定控件身份。 */
	class CATALPA_API LocalizationCatalog final {
	public:
		/**
		 * @brief 解析指定语言的翻译目录，并验证语言标识及文本。
		 *
		 * @param[in] json 完整 JSON 翻译目录。
		 * @param[in] expectedLanguage 预期语言标识，例如 zh-CN 或 en-US。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Load(std::string_view json, std::string_view expectedLanguage, std::string& error);
		/**
		 * @brief 加载文件。
		 *
		 * @param[in] path 文件系统中的目标路径。
		 * @param[in] expectedLanguage 字符串表应声明的语言标识，用于防止加载错误的语言文件。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool LoadFile(const std::filesystem::path& path, std::string_view expectedLanguage, std::string& error);
		const auto& Strings() const noexcept { return m_Strings; }
		const char* Text(const char* key, const char* fallback) const noexcept;
		const std::string& Language() const noexcept { return m_Language; }

	private:
		std::string m_Language = "en-US";
		std::map<std::string, std::string, std::less<>> m_Strings;
	};
} /* 命名空间 Catalpa */
