import Common.Fullscreen;

[[vk::binding(0, 0)]] Texture2D<float4> sourceTexture;
[[vk::binding(1, 0)]] SamplerState sourceSampler;

[shader("vertex")]
FullscreenOutput vertexMain(uint index : SV_VulkanVertexID) {
    return fullscreenVertex(index);
}

[shader("fragment")]
float4 fragmentMain(FullscreenOutput input) : SV_Target0 {
    return sourceTexture.SampleLevel(sourceSampler, input.uv, 0);
}
