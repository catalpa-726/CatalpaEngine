struct TriangleDrawParameters
{
    float4 transform;
    float4 tint;
};

[[vk::push_constant]]
ConstantBuffer<TriangleDrawParameters> drawParameters;

struct VertexInput
{
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float3 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position * drawParameters.transform.xy + drawParameters.transform.zw, 0.0, 1.0);
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    return float4(input.color * drawParameters.tint.rgb, drawParameters.tint.a);
}