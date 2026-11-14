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

[shader("fragment")]
float4 fragmentMain(FullscreenOutput input) : SV_Target0 {
    return tonemapPixel(sourceTexture.SampleLevel(sourceSampler, input.uv, 0).rgb, displayData.display.x);
}
