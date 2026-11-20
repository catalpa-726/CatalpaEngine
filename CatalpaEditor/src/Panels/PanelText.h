#pragma once
#include "catpch.h"
#include "Assets/AssetId.h"

namespace Catalpa::Editor {
	/** @brief 仅在点击组件列表后的下一帧，把对应控件滚入视野并展开。key 始终指向稳定的翻译键。 */
	struct InspectorSection {
		std::string_view key;
		bool ScrollTo(std::string_view target) const;
		bool Header(std::string_view target, const char* label, bool defaultOpen = false) const;
	};
	/* 文本来自既有JSON语言包，内部ID不随翻译变化；stableId=nullptr仅返回显示文本。 */
	using PanelText = std::function<std::string(const char* key, const char* fallback, const char* stableId)>;
	/** @brief 由工作区按类型列出资产；选中且可用时写入对应的稳定引用。 */
	using ComponentAssetPicker = std::function<bool(const char* label, std::string_view type, AssetId& asset)>;
}
