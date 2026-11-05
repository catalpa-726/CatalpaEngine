import Common.Bindings;
import Common.Math;

public struct MeshVertexInput
{
    [[vk::location(0)]] public float3 position : POSITION;
    [[vk::location(1)]] public float3 normal : NORMAL;
    [[vk::location(2)]] public float4 tangent : TANGENT;
    [[vk::location(3)]] public float2 uv : TEXCOORD0;
    [[vk::location(4)]] public float4 vertexColor : COLOR0;
};
public struct MeshVertexOutput
{
    public float4 position : SV_Position;
    [[vk::location(0)]] public float3 normal : NORMAL;
    [[vk::location(1)]] public float3 worldPosition : TEXCOORD0;
    [[vk::location(2)]] public float4 tangent : TEXCOORD1;
    [[vk::location(3)]] public float2 uv : TEXCOORD2;
    [[vk::location(4)]] public float4 vertexColor : COLOR0;
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
    output.worldPosition = worldPosition.xyz;
    float3 maxima = max(abs(drawData.worldColumns[0].xyz),
        max(abs(drawData.worldColumns[1].xyz), abs(drawData.worldColumns[2].xyz)));
    float scale = max(maxima.x, max(maxima.y, maxima.z));
    float3 tangent = unitNormal(input.tangent.xyz);
    float3 worldTangent = (drawData.worldColumns[0].xyz / scale) * tangent.x +
        (drawData.worldColumns[1].xyz / scale) * tangent.y + (drawData.worldColumns[2].xyz / scale) * tangent.z;
    // 镜像世界变换改变TBN手性；法线仍用逆转置，不把非均匀缩放当旋转。
    // 镜像符号由CPU double determinant统一提供，避免GPU float cross/dot先溢出。
    float determinantSign = (drawData.ids.y & 1u) != 0 ? -1.0 : 1.0;
    output.tangent = float4(worldTangent, input.tangent.w * determinantSign);
    output.uv = input.uv;
    output.vertexColor = input.vertexColor;
    return output;
}
