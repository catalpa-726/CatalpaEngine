#pragma once

#include "catpch.h"

namespace Catalpa {

    enum class MeshTopology { TriangleList };

    // 索引元素范围，而非字节；实际顶点 = int64(rawIndex) + baseVertex。
    // firstVertex/vertexCount 约束调整后的顶点区间；materialSlot 不是 GPU 管线或材质对象。
    struct Submesh {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        int32_t baseVertex = 0;
        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
        uint32_t materialSlot = 0;
        MeshTopology topology = MeshTopology::TriangleList;
    };
}
