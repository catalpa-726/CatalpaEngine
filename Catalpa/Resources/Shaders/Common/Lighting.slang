import Common.Math;

public struct LightData
{
    public float4 positionRange;
    public float4 directionOuterCos;
    public float4 colorIntensity;
    public float4 typeInnerCosShadow;
};
public struct LightingParameters
{
    public float4 countEnvironment;
    public float4 shadowParameters;
    public float4 shadowViewProjection[4];
    public LightData lights[32];
    public float4 fogColorDensity; // RGB为线性雾色，w为启用后的每米消光系数，0禁用。
};
[[vk::binding(10, 0)]] public ConstantBuffer<LightingParameters> lightingData;
[[vk::binding(11, 0)]] public Texture2D<float> shadowTexture;
[[vk::binding(12, 0)]] public SamplerComparisonState shadowSampler;

public float3 applyDistanceFog(float3 color, float3 worldPosition, float3 cameraPosition)
{
    float density = lightingData.fogColorDensity.w;
    if (!(density > 0)) return color; // 避免零密度与无穷远距离相乘产生NaN。
    // 先作半差再乘密度，避免大距离溢出后再与极小密度相乘而错误变成全雾。
    float3 offset = (worldPosition * 0.5 - cameraPosition * 0.5) * density;
    float scale = max(abs(offset.x), max(abs(offset.y), abs(offset.z)));
    if (isinf(scale)) return lightingData.fogColorDensity.rgb;
    float opticalDepth = scale > 0 ? (2.0 * scale) * length(offset / scale) : 0;
    float transmission = exp(-opticalDepth);
    return color * transmission + lightingData.fogColorDensity.rgb * (1.0 - transmission);
}

public float shadowVisibility(float3 worldPosition)
{
    if (lightingData.shadowParameters.x < 0.5) return 1.0;
    float4 projected = multiplyColumns(lightingData.shadowViewProjection[0], lightingData.shadowViewProjection[1],
        lightingData.shadowViewProjection[2], lightingData.shadowViewProjection[3], float4(worldPosition, 1));
    float3 ndc = projected.xyz / projected.w;
    float2 uv = ndc.xy * 0.5 + 0.5;
    if (any(uv < 0) || any(uv > 1) || ndc.z < 0 || ndc.z > 1) return 1.0;
    float result = 0;
    // 固定3x3 PCF；bias在CPU由世界米转换为光源ZO深度，不掩盖投影符号错误。
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            result += shadowTexture.SampleCmpLevelZero(shadowSampler,
                uv + float2(x, y) * lightingData.shadowParameters.z, ndc.z - lightingData.shadowParameters.y);
    return result / 9.0;
}

public float3 lightIncident(LightData light, float3 position, out float3 direction)
{
    if (light.typeInnerCosShadow.x < 0.5)
    {
        direction = -unitNormal(light.directionOuterCos.xyz);
        return light.colorIntensity.rgb * light.colorIntensity.a; // lux。
    }
    float3 offset = light.positionRange.xyz - position;
    float maximumDistance = max(abs(offset.x), max(abs(offset.y), abs(offset.z)));
    if (maximumDistance > light.positionRange.w)
    {
        direction = float3(0);
        return float3(0);
    }
    direction = unitNormal(offset);
    float actualDistance = maximumDistance > 0 ? maximumDistance * length(offset / maximumDistance) : 0;
    float normalizedDistance = actualDistance / light.positionRange.w;
    float cutoff = saturate(1.0 - pow(normalizedDistance, 4.0));
    float inverseDistance = 1.0 / max(actualDistance, 0.001);
    float attenuation = cutoff * cutoff;
    if (light.typeInnerCosShadow.x > 1.5)
    {
        float cone = dot(unitNormal(light.directionOuterCos.xyz), -direction);
        float inner = light.typeInnerCosShadow.y, outer = light.directionOuterCos.w;
        float spot = inner > outer ? saturate((cone - outer) / (inner - outer)) : step(outer, cone);
        attenuation *= spot * spot;
    }
    // 先乘受检强度再两次除距离，避免1/r²先下溢而丢掉仍可表示的低照度。
    return ((light.colorIntensity.rgb * light.colorIntensity.a) * inverseDistance) * inverseDistance * attenuation;
}
