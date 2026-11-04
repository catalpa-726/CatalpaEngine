import Common.Bindings;
import Common.Math;
import Common.Mesh;

// 独立Material块只含领域材质参数，Frame/View/Draw保持原职责。
struct MaterialParameters { float4 base_color; };
[[vk::binding(3, 0)]] ConstantBuffer<MaterialParameters> Material;

[shader("vertex")]
MeshVertexOutput vertexMain(MeshVertexInput input)
{
    return transformMeshVertex(input);
}
[shader("fragment")]
float4 fragmentMain(MeshVertexOutput input) : SV_Target0
{
    // normalDebug是明确配置，保留线性颜色/alpha，不借用透明度位传递诊断开关。
    if (frameData.options.x > 0.5)
        return float4(unitNormal(input.normal) * 0.5 + 0.5, drawData.color.a);
    return drawData.color * Material.base_color;
}
