#pragma once

#include "catpch.h"
#include "Renderer/RenderFramePacket.h"

namespace Catalpa {

    // 同一密封几何的可见实例组；组和组内实例都按来源首次出现次序保留，不排序指针。
    struct VisibleMeshGroup {
        MeshResourceRef mesh;
        std::vector<size_t> instanceIndices;
    };

    // 全部索引均指向来源packet.instances；分组不改变主索引顺序，不复制或归一化实例。
    struct VisibilityResult {
        std::vector<size_t> instanceIndices;
        std::vector<VisibleMeshGroup> groups;
    };

    // 只消费RenderScene已验证的自有值快照；不遍历World、不创建GPU对象。
    class VisibilitySystem {
    public:
        // 另验最终VP/lens与全部bounds；零尺寸直接成功发布空结果，不构建投影。
        // 任何非零尺寸的非法输入或分配失败保留output，隐藏/剔除不改变来源实例。
        [[nodiscard]] static CATALPA_API Math::MathStatus Build(const RenderView& view,
            std::span<const RenderMeshInstance> instances, VisibilityResult& output);
    };

}
