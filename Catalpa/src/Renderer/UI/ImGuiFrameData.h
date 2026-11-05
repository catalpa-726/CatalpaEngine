#pragma once

#include <cstdint>
#include <vector>

namespace Catalpa {

    // CPU 自有 UI 帧包；Renderer 不依赖 ImGui 对象或借用其下一帧会失效的内存。
    struct ImGuiVertex {
        float position[2]{}; // 逻辑窗口坐标。
        float uv[2]{};
        float color[4]{}; // RGB 为 sRGB 编码值，alpha 为线性覆盖率。
    };

    struct ImGuiDraw {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        int32_t baseVertex = 0;
        float clipRect[4]{}; // 逻辑坐标 minX/minY/maxX/maxY；Renderer 统一转换为 framebuffer 像素。
        uint64_t textureToken = 0; // 0为字体；其他值只能由Renderer纹理注册表发放。
    };

    struct ImGuiFrameData {
        float displayPos[2]{};
        float displaySize[2]{};
        float framebufferScale[2]{ 1.0f, 1.0f };
        std::vector<ImGuiVertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<ImGuiDraw> draws;
    };

}
