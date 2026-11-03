struct TriangleDrawParameters
{
    float4 transform;
    float4 tint;
};

[[vk::push_constant]]
ConstantBuffer<TriangleDrawParameters> drawParameters;

[[vk::binding(0, 0)]]
Texture2D<float4> triangleTexture;

[[vk::binding(1, 0)]]
SamplerState triangleSampler;

struct VertexInput
{
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float3 color : COLOR0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position * drawParameters.transform.xy + drawParameters.transform.zw, 0.0, 1.0);
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    const float4 texel = triangleTexture.Sample(triangleSampler, input.uv);
    return texel * float4(input.color, 1.0) * drawParameters.tint;
}