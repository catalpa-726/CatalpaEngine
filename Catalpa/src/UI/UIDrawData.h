#pragma once
#include "Assets/AssetHandle.h"
#include "Images/ImageAsset.h"

namespace Catalpa {
	struct UITextureData {
		uint64_t generation = 0;
		uint32_t width = 0, height = 0;
		/* RGB 是预乘 alpha 的颜色；srgb=true 时采样需先反预乘、解码，再预乘。 */
		bool srgb = true;
		std::vector<std::byte> rgba8;
		AssetVersionRef<ImageAsset> source;
		/* 内存配额令牌与所有在途快照共存。卸载 UI 上下文时，仍被快照引用的纹理额度继续保留。 */
		std::shared_ptr<const void> storageLease;
	};
	struct UIDrawVertex {
		float position[2]{}, uv[2]{}, color[4]{};
	};
	struct UIDrawBatch {
		uint32_t firstIndex = 0, indexCount = 0;
		int32_t baseVertex = 0;
		std::array<int32_t, 4> scissor{}; /* x/y/width/height，目标像素坐标。 */
		std::shared_ptr<const UITextureData> texture;
	};
	struct UIDrawData {
		uint32_t width = 0, height = 0;
		std::vector<UIDrawVertex> vertices; /* 已变换的像素坐标，颜色为线性预乘 RGBA。 */
		std::vector<uint32_t> indices;
		std::vector<UIDrawBatch> batches;
		std::vector<std::string> diagnostics;
	};
} /* 命名空间 Catalpa */
