import Common.Bindings;
import Common.Math;
import Common.Mesh;
import Common.Surface;
import Common.Lighting;

[shader("vertex")]
MeshVertexOutput vertexMain(MeshVertexInput input)
{
    return transformMeshVertex(input);
}
[shader("fragment")]
float4 fragmentMain(MeshVertexOutput input) : SV_Target0
{
    float4 color = surfaceBase(surfaceUV(input.uv), input.vertexColor);
    applySurfaceMask(color.a);
    /* normalDebug是明确配置，保留线性颜色/alpha，不借用透明度位传递诊断开关。 */
    if (frameData.options.x > 0.5)
        return float4(unitNormal(input.normal) * 0.5 + 0.5, color.a);
    return float4(applyDistanceFog(color.rgb, input.worldPosition, viewData.cameraPosition.xyz), color.a);
}
