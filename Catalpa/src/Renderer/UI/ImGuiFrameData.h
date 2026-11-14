#pragma once

#include <cstdint>
#include <vector>

namespace Catalpa {

	/*
	 * ImGui 侧的 0 为无效 ID，1 专属字体图集；注册纹理从下一号起分配。
	 * 写入引擎帧包时仅将字体转换为内部 token 0，其余 ID 原样传递。
	 */
	inline constexpr uint64_t ImGuiFontAtlasId = 1;

	/* CPU 自有 UI 帧包；Renderer 不依赖 ImGui 对象或借用其下一帧会失效的内存。 */
	struct ImGuiVertex {
		float position[2]{}; /* 逻辑窗口坐标。 */
		float uv[2]{};
		float color[4]{}; /* RGB 为 sRGB 编码值，alpha 为线性覆盖率。 */
	};

	struct ImGuiDraw {
		uint32_t firstIndex = 0;
		uint32_t indexCount = 0;
		int32_t baseVertex = 0;
		float clipRect[4]{}; /* 逻辑坐标 minX/minY/maxX/maxY；Renderer 统一转换为 framebuffer 像素。 */
		uint64_t textureToken = 0; /* 0为字体；其他值只能由Renderer纹理注册表发放。 */
	};

	struct ImGuiFrameData {
		float displayPos[2]{};
		float displaySize[2]{};
		float framebufferScale[2]{1.0f, 1.0f};
		std::vector<ImGuiVertex> vertices;
		std::vector<uint32_t> indices;
		std::vector<ImGuiDraw> draws;
		/* 主窗口提交时，其他窗口也可能正在显示本帧才退休的预览纹理。 */
		std::vector<uint64_t> platformTextureTokens;
	};

}
