#pragma once
#include "Core/Localization/LocalizationCatalog.h"

namespace Catalpa::Editor {
	/*
	 * 语言目录由工作区持有，嵌套编辑器或弹窗只在所属工作区绘制期间借用它。
	 * 给 ImGui 可变参数接口使用的译文须保留格式占位符及其顺序。
	 */
	class EditorTextScope {
	public:
		explicit EditorTextScope(const LocalizationCatalog& catalog) : m_Previous(s_Current) { s_Current = &catalog; }
		~EditorTextScope() { s_Current = m_Previous; }
		EditorTextScope(const EditorTextScope&) = delete;
		EditorTextScope& operator=(const EditorTextScope&) = delete;
		static const char* Get(const char* key, const char* fallback) noexcept {
			return s_Current ? s_Current->Text(key, fallback) : fallback;
		}

	private:
		inline static thread_local const LocalizationCatalog* s_Current = nullptr;
		const LocalizationCatalog* m_Previous;
	};

	inline const char* UiText(const char* key, const char* fallback) noexcept {
		return EditorTextScope::Get(key, fallback);
	}
	inline std::string UiLabel(const char* key, const char* fallback) {
		return std::string(UiText(key, fallback)) + "###" + key;
	}
	/**
	 * @brief 将选项拼成 ImGui 组合框要求的以空字符分隔的列表。
	 *
	 * @param[in] options 按界面显示顺序排列的各项文本，用于构造组合框的选项列表。
	 *
	 * @return 按选项顺序拼接的字符串，每项后附一个零字节，供 ImGui 组合框读取。
	 */
	inline std::string UiChoices(std::initializer_list<const char*> options) {
		std::string list;
		for (const char* option : options) {
			list += option;
			list.push_back('\0');
		}
		return list;
	}
	/**
	 * @brief 验证翻译格式中的占位符是否与后备格式兼容，再选择可用格式。
	 *
	 * @param[in] key 用于在当前语言资源中查找格式串的键。
	 * @param[in] fallback 缺少译文或译文占位符不兼容时使用的格式串。
	 *
	 * @return 占位符序列兼容时返回译文，否则返回调用者提供的后备格式串；返回指针借用相应文本。
	 */
	inline const char* UiFormat(const char* key, const char* fallback) noexcept {
		const char* translated = UiText(key, fallback);
		const auto next = [](const char*& cursor) -> std::string_view {
			cursor = std::strchr(cursor, '%');
			if (!cursor)
				return {};
			const char* start = cursor++;
			cursor += std::strspn(cursor, "-+ #0*.0123456789hljztL");
			if (*cursor)
				++cursor;
			return {start, static_cast<size_t>(cursor - start)};
		};
		const char* a = fallback;
		const char* b = translated;
		for (;;) {
			const auto spec = next(a);
			if (spec != next(b))
				return fallback;
			if (spec.empty())
				return translated;
		}
	}
}
