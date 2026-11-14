#pragma once
#include "Assets/AssetId.h"
#include "Assets/AssetHandle.h"
#include "ECS/World.h"
namespace Catalpa {
	struct UIDocumentAsset;
	struct UIComponent {
		AssetId document;
		bool enabled = true;
		int32_t order = 0;
		AssetVersionRef<UIDocumentAsset> documentVersion;
	};
	template <> struct ComponentTraits<UIComponent> {
		static CATALPA_API const ComponentType& Get();
	};
} /* 命名空间 Catalpa */
