import Common.Bindings;
import Common.Math;
import Common.Mesh;
import Common.Surface;
import Common.Pbr;
import Common.Lighting;
import Common.Environment;

[shader("vertex")]
MeshVertexOutput vertexMain(MeshVertexInput input) { return transformMeshVertex(input); }

[shader("fragment")]
float4 fragmentMain(MeshVertexOutput input, bool frontFace : SV_IsFrontFace) : SV_Target0
{
    float2 uv = surfaceUV(input.uv);
    float4 base = surfaceBase(uv, input.vertexColor);
    applySurfaceMask(base.a);
    float3 normal = surfaceNormal(input, uv, frontFace);
    if (frameData.options.x > 0.5) return float4(normal * 0.5 + 0.5, base.a);
    float3 view = unitNormal(viewData.cameraPosition.xyz - input.worldPosition);
    float metallic = Material.emissive_metallic.w;
    float roughness = Material.roughness_normal_occlusion_cutoff.x;
    if (surfaceFlag(4u))
    {
        float4 packed = metallicRoughness_texture.Sample(metallicRoughnessSampler, uv);
        roughness *= packed.g; metallic *= packed.b;
    }
    float3 emissive = Material.emissive_metallic.rgb;
    if (surfaceFlag(8u)) emissive *= emissive_texture.Sample(emissiveSampler, uv).rgb;
    float3 result = emissive;
    for (uint i = 0; i < tileLightCount(input.position.xy); ++i)
    {
        float3 lightDirection;
        LightData light = lightingData.lights[tileLightIndex(input.position.xy,i)];
        float3 incident = lightIncident(light, input.worldPosition, lightDirection);
        if (light.typeInnerCosShadow.z > 0.5) incident *= shadowVisibility(input.worldPosition);
        result += evaluatePbr(base.rgb, metallic, roughness, normal, view, lightDirection, incident);
    }
    float ndotv = saturate(dot(normal, view));
    float3 f0 = lerp(float3(0.04), base.rgb, metallic);
    float3 fresnel = fresnelSchlick(ndotv, f0);
    float3 irradiance = irradianceTexture.SampleLevel(environmentSampler, environmentDirection(normal), 0).rgb;
    float3 reflected = reflect(-view, normal);
    float3 prefiltered = specularTexture.SampleLevel(environmentSampler, environmentDirection(reflected),
        roughness * lightingData.shadowParameters.w).rgb;
    uint lutWidth, lutHeight;
    brdfTexture.GetDimensions(lutWidth, lutHeight);
    float2 halfTexel = 0.5 / float2(lutWidth, lutHeight);
    /* LUT坐标夹在像素中心，确保边界不依赖共享采样器的寻址模式。 */
    float2 lutUV = clamp(float2(ndotv, roughness), halfTexel, 1.0 - halfTexel);
    float2 brdf = brdfTexture.SampleLevel(environmentSampler, lutUV, 0).rg;
    float occlusion = surfaceFlag(16u) ? lerp(1.0, occlusion_texture.Sample(occlusionSampler, uv).r,
        Material.roughness_normal_occlusion_cutoff.z) : 1.0;
    result += ((1.0 - fresnel) * (1.0 - metallic) * base.rgb * irradiance / PBR_PI +
        prefiltered * (f0 * brdf.x + brdf.y)) * lightingData.countEnvironment.y * occlusion;
    return float4(applyDistanceFog(result, input.worldPosition, viewData.cameraPosition.xyz), base.a);
}
