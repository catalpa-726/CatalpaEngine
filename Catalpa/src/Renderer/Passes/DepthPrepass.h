#pragma once

#include "catpch.h"
#include "Core/Base.h"
#include "Renderer/RenderGraph/RenderGraphPass.h"
#include "RHI/RHIError.h"

namespace Catalpa {
    class RenderGraph;

    // 同一深度目标的opaque预通道；后续Mask/形变必须与Forward共享同一顶点/可见性契约。
    class DepthPrepass {
    public:
        // 只为本引擎Vertex产物派生Invariant Position装饰，满足跨pipeline预通道Equal前提。
        // 受检克隆，不改原件/ID/算术；支持direct Output和Block成员，失败保留output。
        // 这是有限产物准备，不替代完整SPIR-V verifier或S04.3的ShaderMetadata。
        [[nodiscard]] static CATALPA_API RHIStatus PrepareInvariantVertexShader(
            std::span<const uint32_t> source, std::vector<uint32_t>& output);

        // 首draw清除、后续加载，全部Store；输入由值复制，不改作者draw或自行提交队列。
        [[nodiscard]] static RHIStatus Add(RenderGraph& graph, std::span<const RGIndexedDrawDesc> draws, float clearDepth);
    };
}
