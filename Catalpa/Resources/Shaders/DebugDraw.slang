import Common.Bindings;
import Common.Math;

struct DebugVertexInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float4 color : COLOR;
};
struct DebugVertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR;
};

[shader("vertex")]
DebugVertexOutput vertexMain(DebugVertexInput input) {
    DebugVertexOutput output;
    output.position = multiplyColumns(viewData.viewProjectionColumns[0], viewData.viewProjectionColumns[1],
                                      viewData.viewProjectionColumns[2], viewData.viewProjectionColumns[3],
                                      float4(input.position, 1));
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(DebugVertexOutput input) : SV_Target0 {
    return input.color;
}
