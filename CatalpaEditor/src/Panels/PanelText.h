#pragma once
#include "catpch.h"

namespace Catalpa::Editor {
	/* 文本来自既有JSON语言包，内部ID不随翻译变化；stableId=nullptr仅返回显示文本。 */
	using PanelText = std::function<std::string(const char* key, const char* fallback, const char* stableId)>;
}
