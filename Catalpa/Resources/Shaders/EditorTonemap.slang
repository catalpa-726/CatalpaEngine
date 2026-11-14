import Common.Fullscreen;
import Common.Display;

[[vk::binding(0, 0)]] Texture2D<float4> sourceTexture;
[[vk::binding(1, 0)]] SamplerState sourceSampler;
struct DisplayParameters {
    float4 display;
};
[[vk::push_constant]] ConstantBuffer<DisplayParameters> displayData;

[shader("vertex")]
FullscreenOutput vertexMain(uint index : SV_VulkanVertexID) {
    return fullscreenVertex(index);
}

[[vk::binding(2, 0)]] Texture2D<uint> pickingIds;

/**
 * 读取当前 HDR 帧的拾取结果，用标识的最高位判断像素处最前方的可见表面是否已被选中。
 * @param pixel 待查询的整数像素坐标，可以位于图像边界外。
 * @param lastPixel 拾取纹理最后一个有效像素的坐标，用作逐轴钳位上限。
 */
bool selectedAt(int2 pixel, int2 lastPixel) {
    uint id = pickingIds.Load(int3(clamp(pixel, int2(0), lastPixel), 0));
    return (id & 0x80000000u) != 0;
}

/**
 * 对 HDR 场景做色调映射，并以四邻域在选中表面的外侧绘制轮廓。
 * @param input 全屏三角形的插值结果，uv 用于采样场景，position.xy 用于查询拾取纹理。
 */
[shader("fragment")]
float4 fragmentMain(FullscreenOutput input) : SV_Target0 {
    uint w, h;
    pickingIds.GetDimensions(w, h);
    int2 last = int2(w, h) - 1;
    int2 p = int2(input.position.xy);
    float4 scene =
        tonemapPixel(sourceTexture.SampleLevel(sourceSampler, input.uv, 0).rgb, displayData.display.x);
    if (!selectedAt(p, last) && (selectedAt(p + int2(1, 0), last) || selectedAt(p - int2(1, 0), last) ||
                                 selectedAt(p + int2(0, 1), last) || selectedAt(p - int2(0, 1), last)))
        return float4(0.12, 0.85, 0.46, 1.0);
    return scene;
}
