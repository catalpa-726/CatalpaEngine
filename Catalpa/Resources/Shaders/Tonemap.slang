import Common.Fullscreen;

[[vk::binding(0, 0)]] Texture2D<float4> sourceTexture;
[[vk::binding(1, 0)]] SamplerState sourceSampler;
struct DisplayParameters { float4 display; };
[[vk::push_constant]] ConstantBuffer<DisplayParameters> displayData;

[shader("vertex")]
FullscreenOutput vertexMain(uint index : SV_VulkanVertexID) { return fullscreenVertex(index); }

float toneChannel(float radiance, float inverseExposure)
{
    if (!(radiance > 0)) return 0;
    // 曝光用倒数比例表达，避免HDR*exp2(EV)或大值平方先溢出为Inf。
    float u = inverseExposure / radiance;
    if (u < 1.0)
        return saturate((2.51 + 0.03 * u) / (2.43 + 0.59 * u + 0.14 * u * u));
    float x = 1.0 / u;
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

[shader("fragment")]
float4 fragmentMain(FullscreenOutput input) : SV_Target0
{
    float3 hdr = sourceTexture.SampleLevel(sourceSampler, input.uv, 0).rgb;
    if (any(isnan(hdr)) || any(isinf(hdr))) return float4(1, 0, 1, 1); // 超预算或非法派生结果给出可见诊断。
    // ACES拟合只用于输出映射；输出仍线性，sRGB附件在UI线性合成后编码一次。
    float inverseExposure = exp2(-displayData.display.x);
    float3 mapped = float3(toneChannel(hdr.r, inverseExposure), toneChannel(hdr.g, inverseExposure),
        toneChannel(hdr.b, inverseExposure));
    return float4(mapped, 1);
}
