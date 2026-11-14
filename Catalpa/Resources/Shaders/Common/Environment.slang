import Common.Lighting;

[[vk::binding(13, 0)]] public TextureCube<float4> irradianceTexture;
[[vk::binding(14, 0)]] public TextureCube<float4> specularTexture;
[[vk::binding(15, 0)]] public Texture2D<float4> brdfTexture;
[[vk::binding(16, 0)]] public SamplerState environmentSampler;
[[vk::binding(17, 0)]] public TextureCube<float4> radianceTexture;

/**
 * 按环境贴图的朝向旋转世界方向，使天空和间接光使用相同的采样坐标。
 * @param direction 世界坐标中的环境采样方向，围绕世界 Y 轴旋转到贴图坐标。
 */
public float3 environmentDirection(float3 direction) {
    float angle = lightingData.countEnvironment.z;
    float sine = sin(angle), cosine = cos(angle);
    float3 local = float3(cosine * direction.x - sine * direction.z, direction.y,
                          sine * direction.x + cosine * direction.z);
    return local;
}
