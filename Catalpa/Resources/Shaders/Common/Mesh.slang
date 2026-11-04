import Common.Bindings;
import Common.Math;

public struct MeshVertexInput
{
    [[vk::location(0)]] public float3 position : POSITION;
    [[vk::location(1)]] public float3 normal : NORMAL;
};
public struct MeshVertexOutput
{
    public float4 position : SV_Position;
    [[vk::location(0)]] public float3 normal : NORMAL;
};
// 世界矩阵与viewProjection已在CPU按统一坐标契约建立；GPU这里只消费显式列。
public MeshVertexOutput transformMeshVertex(MeshVertexInput input)
{
    MeshVertexOutput output;
    float4 worldPosition = multiplyColumns(drawData.worldColumns[0], drawData.worldColumns[1],
        drawData.worldColumns[2], drawData.worldColumns[3], float4(input.position, 1.0));
    output.position = multiplyColumns(viewData.viewProjectionColumns[0], viewData.viewProjectionColumns[1],
        viewData.viewProjectionColumns[2], viewData.viewProjectionColumns[3], worldPosition);
    output.normal = transformNormalColumns(drawData.normalColumns[0], drawData.normalColumns[1],
        drawData.normalColumns[2], input.normal);
    return output;
}
