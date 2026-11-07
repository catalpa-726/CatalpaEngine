struct Parameters { float4 transform; float4 options; };
[[vk::push_constant]] ConstantBuffer<Parameters> ui;
[[vk::binding(0,0)]] Texture2D<float4> uiTexture;
[[vk::binding(1,0)]] SamplerState uiSampler;
struct VertexInput { [[vk::location(0)]] float2 position:POSITION; [[vk::location(1)]] float2 uv:TEXCOORD0; [[vk::location(2)]] float4 color:COLOR0; };
struct VertexOutput { float4 position:SV_Position; [[vk::location(0)]] float2 uv:TEXCOORD0; [[vk::location(1)]] float4 color:COLOR0; };
[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput output; output.position=float4(input.position*ui.transform.xy+ui.transform.zw,0,1); output.uv=input.uv; output.color=input.color; return output;
}
float3 decodeSRGB(float3 c) { return float3(c.x<=0.04045?c.x/12.92:pow((c.x+0.055)/1.055,2.4),c.y<=0.04045?c.y/12.92:pow((c.y+0.055)/1.055,2.4),c.z<=0.04045?c.z/12.92:pow((c.z+0.055)/1.055,2.4)); }
[shader("fragment")]
float4 fragmentMain(VertexOutput input):SV_Target0 {
    float4 sample=uiTexture.Sample(uiSampler,input.uv);
    if(ui.options.x>0.5) sample.rgb=sample.a>0?decodeSRGB(saturate(sample.rgb/sample.a))*sample.a:float3(0);
    return sample*input.color; // 两项均为线性预乘；管线使用ONE/ONE_MINUS_SRC_ALPHA。
}
