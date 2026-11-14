import Common.Bindings;
import Common.Math;
import Common.Lighting;
import Common.Environment;
import Common.Fullscreen;

[shader("vertex")]
FullscreenOutput vertexMain(uint index : SV_VulkanVertexID) {
    return fullscreenVertex(index);
}

/**
 * 正交相机共享天空射线方向，透视相机用逆视投影矩阵逐像素重建射线。
 * @param input 全屏三角形的插值结果，uv 给出当前像素的归一化纹理坐标。
 */
[shader("fragment")]
float4 fragmentMain(FullscreenOutput input) : SV_Target0 {
    if (lightingData.fogColorDensity.w > 0)
        return float4(lightingData.fogColorDensity.rgb, 1);
    float4 world =
        multiplyColumns(viewData.inverseViewProjectionColumns[0], viewData.inverseViewProjectionColumns[1],
                        viewData.inverseViewProjectionColumns[2], viewData.inverseViewProjectionColumns[3],
                        float4(input.uv * 2 - 1, 0.5, 1));
    bool orthographic = viewData.inverseViewProjectionColumns[2].w == 0;
    float3 direction = orthographic ? unitNormal(-float3(viewData.viewColumns[0].z, viewData.viewColumns[1].z,
                                                         viewData.viewColumns[2].z))
                                    : unitNormal(world.xyz - viewData.cameraPosition.xyz * world.w);
    return float4(radianceTexture.SampleLevel(environmentSampler, environmentDirection(direction), 0).rgb *
                      lightingData.countEnvironment.y,
                  1);
}
