[[vk::binding(0, 0)]] ByteAddressBuffer inputs;
[[vk::binding(1, 0)]] ByteAddressBuffer pyramid;
[[vk::binding(2, 0)]] RWByteAddressBuffer commands;
struct Parameters {
    float4 vp[4];
    uint4 counts;
    uint4 mips[16];
};
[[vk::binding(3, 0)]] ConstantBuffer<Parameters> p;

/**
 * 包围盒跨越近裁剪面时跳过遮挡测试，以保留可能可见的几何。
 * @param id x 为候选物体索引，同时定位包围盒和对应的间接绘制命令。
 */
[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID) {
    uint index = id.x;
    if (index >= p.counts.x)
        return;
    float3 minimum = asfloat(inputs.Load4(index * 48)).xyz,
           maximum = asfloat(inputs.Load4(index * 48 + 16)).xyz;
    uint4 command = inputs.Load4(index * 48 + 32);
    float2 low = 1e30, high = -1e30;
    float closest = p.counts.z != 0 ? 0 : 1;
    bool nearCross = false;
    bool left = true, right = true, top = true, bottom = true, nearPlane = true, farPlane = true;
    for (uint corner = 0; corner < 8; ++corner) {
        float3 v =
            float3((corner & 1) != 0 ? maximum.x : minimum.x, (corner & 2) != 0 ? maximum.y : minimum.y,
                   (corner & 4) != 0 ? maximum.z : minimum.z);
        float4 clip = p.vp[0] * v.x + p.vp[1] * v.y + p.vp[2] * v.z + p.vp[3];
        if (any(isnan(clip)) || any(isinf(clip))) {
            nearCross = true;
            left = false;
            right = false;
            top = false;
            bottom = false;
            nearPlane = false;
            farPlane = false;
            continue;
        }
        left = left && (clip.x < -clip.w);
        right = right && (clip.x > clip.w);
        top = top && (clip.y < -clip.w);
        bottom = bottom && (clip.y > clip.w);
        nearPlane = nearPlane && (clip.z < 0);
        farPlane = farPlane && (clip.z > clip.w);
        if (clip.w <= 1e-5 || clip.z <= 0 || clip.z >= clip.w)
            nearCross = true;
        else {
            float3 ndc = clip.xyz / clip.w;
            float2 uv = ndc.xy * 0.5 + 0.5;
            low = min(low, uv);
            high = max(high, uv);
            closest = p.counts.z != 0 ? max(closest, ndc.z) : min(closest, ndc.z);
        }
    }
    bool visible = !(left || right || top || bottom || nearPlane || farPlane);
    if (visible && !nearCross && p.counts.w != 0) {
        uint level = 0;
        float2 dimensions = float2(p.mips[0].yz);
        float extent = max((high.x - low.x) * dimensions.x, (high.y - low.y) * dimensions.y);
        while (level + 1 < p.counts.y && extent > 1) {
            extent *= 0.5;
            ++level;
        }
        uint4 mip = p.mips[level];
        /*
         * 每级按2x2源像素归约，奇数边缘也沿用这组坐标。
         * 先映射到基础层再右移；直接用取整后的mip尺寸缩放UV会漏采样并错误剔除。
         */
        uint2 begin = min(uint2(saturate(low) * dimensions) >> level, mip.yz - 1);
        uint2 end = min(uint2(saturate(high) * dimensions) >> level, mip.yz - 1);
        float farthest = p.counts.z != 0 ? 1 : 0;
        for (uint y = begin.y; y <= end.y; ++y)
            for (uint x = begin.x; x <= end.x; ++x) {
                float d = asfloat(pyramid.Load(mip.x + (y * mip.y + x) * 4));
                farthest = p.counts.z != 0 ? min(farthest, d) : max(farthest, d);
            }
        visible = p.counts.z != 0 ? closest + 1e-5 >= farthest : closest - 1e-5 <= farthest;
    }
    /* 间接绘制命令按 uint 写入；有符号字面量会生成同一 binding 的另一种缓冲视图。 */
    commands.Store(index * 20, command.x);
    commands.Store(index * 20 + 4, visible ? 1u : 0u);
    commands.Store(index * 20 + 8, command.y);
    commands.Store(index * 20 + 12, command.z);
    commands.Store(index * 20 + 16, command.w);
}
