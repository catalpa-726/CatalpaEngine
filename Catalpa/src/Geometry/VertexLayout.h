#pragma once

#include "catpch.h"

// 定义顶点属性格式、location、offset 和 stride
namespace Catalpa {

    enum class VertexFormat { Float2, Float3, Float4 };
    // CPU 几何描述保留语义；旧的纯管线布局可继续使用 Unknown。
    enum class VertexSemantic { Unknown, Position, Normal, TexCoord0, Color0, Tangent };

    [[nodiscard]] constexpr uint32_t GetVertexFormatByteSize(VertexFormat format) noexcept
    {
        switch (format) {
        case VertexFormat::Float2: return 8;
        case VertexFormat::Float3: return 12;
        case VertexFormat::Float4: return 16;
        default: return 0;
        }
    }

    struct VertexAttribute {
        uint32_t location = 0;
        VertexFormat format = VertexFormat::Float3;
        uint32_t offset = 0;
        VertexSemantic semantic = VertexSemantic::Unknown;
    };

    // 单个逐顶点流；attributes 仅在同步创建管线期间借用。
    struct VertexLayout {
        uint32_t stride = 0;
        std::span<const VertexAttribute> attributes{};
    };

}
