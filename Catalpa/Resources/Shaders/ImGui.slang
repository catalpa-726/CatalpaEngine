// position 为 UI 逻辑坐标，uv 为归一化纹理坐标；color.rgb 为 sRGB，alpha 为线性覆盖率。
struct ImGuiParameters { float4 transform; };
[[vk::push_constant]] ConstantBuffer<ImGuiParameters> ui;
[[vk::binding(0, 0)]] Texture2D<float4> fontTexture;
[[vk::binding(1, 0)]] SamplerState fontSampler;

struct VertexInput
{
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
};
struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR0;
};
[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position * ui.transform.xy + ui.transform.zw, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float srgbToLinear(float value)
{
    return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    // 字体图集使用线性 UNorm 覆盖率；UI RGB 只解码一次。
    // sRGB 附件在线性空间混合，并在写入时编码。
    float4 texel = fontTexture.Sample(fontSampler, input.uv);
    float3 linearColor = float3(srgbToLinear(input.color.r), srgbToLinear(input.color.g), srgbToLinear(input.color.b));
    return float4(linearColor * texel.rgb, input.color.a * texel.a);
}
