#pragma once
#include "Commands/CommandHistory.h"

namespace Catalpa::Editor {
	/* 编辑会话身份不写入作者文件；关闭会话一并撤销异步请求对它的访问资格。 */
	class Document {
	public:
		explicit Document(StableId id) : m_Id(id) {}

		virtual ~Document() = default;
		virtual std::string_view TypeKey() const noexcept { return "scene"; }
		Document(const Document&) = delete;
		Document& operator=(const Document&) = delete;

		StableId Id() const noexcept { return m_Id; }

		CommandHistory& History() noexcept { return m_History; }
		const CommandHistory& History() const noexcept { return m_History; }
		const std::filesystem::path& AuthorPath() const noexcept { return m_Path; }
		void SetAuthorPath(std::filesystem::path path) { m_Path = std::move(path); }
		virtual bool Serialize(std::string&, std::string& error) const {
			error = "This document has no writable author representation";
			return false;
		}

	private:
		StableId m_Id;
		CommandHistory m_History;
		std::filesystem::path m_Path;
	};
}
