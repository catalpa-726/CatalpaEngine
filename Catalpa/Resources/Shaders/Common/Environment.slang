import Common.Lighting;

[[vk::binding(13, 0)]] public TextureCube<float4> irradianceTexture;
[[vk::binding(14, 0)]] public TextureCube<float4> specularTexture;
[[vk::binding(15, 0)]] public Texture2D<float4> brdfTexture;
[[vk::binding(16, 0)]] public SamplerState environmentSampler;
[[vk::binding(17, 0)]] public TextureCube<float4> radianceTexture;

public float3 environmentDirection(float3 direction)
{
    float angle = lightingData.countEnvironment.z;
    float sine = sin(angle), cosine = cos(angle);
    float3 local = float3(cosine * direction.x - sine * direction.z, direction.y,
        sine * direction.x + cosine * direction.z);
    return local;
}
