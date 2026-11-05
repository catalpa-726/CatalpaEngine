import Common.Bindings;
import Common.Mesh;
import Common.Math;

// 固定80字节GPU传输；领域参数ID由CPU MaterialSurface adapter验证后编码。
public struct MaterialParameters
{
    public float4 base_color;
    public float4 emissive_metallic;
    public float4 roughness_normal_occlusion_cutoff;
    public float4 uv_scale_offset;
    public float4 time_effect;
};
[[vk::binding(3, 0)]] public ConstantBuffer<MaterialParameters> Material;
[[vk::binding(4, 0)]] public Texture2D<float4> baseColor_texture;
[[vk::binding(5, 0)]] public Texture2D<float4> normal_texture;
[[vk::binding(6, 0)]] public Texture2D<float4> metallicRoughness_texture;
[[vk::binding(7, 0)]] public Texture2D<float4> emissive_texture;
[[vk::binding(8, 0)]] public Texture2D<float4> occlusion_texture;
[[vk::binding(18, 0)]] public SamplerState baseColorSampler;
[[vk::binding(19, 0)]] public SamplerState normalSampler;
[[vk::binding(20, 0)]] public SamplerState metallicRoughnessSampler;
[[vk::binding(21, 0)]] public SamplerState emissiveSampler;
[[vk::binding(22, 0)]] public SamplerState occlusionSampler;

public uint surfaceFlags() { return uint(Material.time_effect.w); }
public bool surfaceFlag(uint flag) { return (surfaceFlags() & flag) != 0; }

public float2 surfaceUV(float2 uv)
{
    if (surfaceFlag(128u)) uv.y = 1.0 - uv.y;
    // 每4096模拟秒的整数周期，UV在±0.5内作正弦摆动；Repeat/Clamp/Mirror均跨周期连续。
    float2 oscillation = 0.5 * sin(frac((frameData.time.x / 4096.0) * Material.time_effect.xy) * 6.28318530718);
    return uv * Material.uv_scale_offset.xy + Material.uv_scale_offset.zw +
        oscillation;
}

public float4 surfaceBase(float2 uv, float4 vertexColor)
{
    float4 color = drawData.color * Material.base_color * vertexColor;
    if (surfaceFlag(1u)) color *= baseColor_texture.Sample(baseColorSampler, uv);
    if (Material.time_effect.z > 0)
    {
        float wave = sin(frac((frameData.time.x / 4096.0) * Material.time_effect.z) * 6.28318530718);
        // PBR脉冲只衰减反射率，范围[.5,1]；Unlit输出可用[.5,1.5]的亮度变化。
        color.rgb *= surfaceFlag(256u) ? 1.0 + 0.5 * wave : 0.75 + 0.25 * wave;
    }
    return color;
}

public void applySurfaceMask(float alpha)
{
    if (surfaceFlag(32u)) clip(alpha - Material.roughness_normal_occlusion_cutoff.w);
}

public float3 surfaceNormal(MeshVertexOutput input, float2 uv, bool frontFace)
{
    float3 normal = unitNormal(input.normal);
    float3 tangent = unitNormal(input.tangent.xyz - normal * dot(normal, input.tangent.xyz));
    float3 bitangent = cross(normal, tangent) * input.tangent.w;
    // 作者UV缩放的镜像改变纹理坐标基方向；uv_flip_v仅转换来源行序，不重复翻转法线通道。
    tangent *= Material.uv_scale_offset.x < 0 ? -1.0 : 1.0;
    bitangent *= Material.uv_scale_offset.y < 0 ? -1.0 : 1.0;
    if (surfaceFlag(2u))
    {
        float3 sampled = normal_texture.Sample(normalSampler, uv).xyz * 2.0 - 1.0;
        sampled.xy *= Material.roughness_normal_occlusion_cutoff.y;
        normal = unitNormal(tangent * sampled.x + bitangent * sampled.y + normal * sampled.z);
    }
    // 双面背面整体翻转着色法线；深度与Mask使用同一UV和alpha规则。
    return frontFace ? normal : -normal;
}
