public struct FullscreenOutput
{
    public float4 position : SV_Position;
    [[vk::location(0)]] public float2 uv : TEXCOORD0;
};

// 三个全屏 pass 均以 firstVertex=0 绘制三个顶点。直接使用 VertexIndex，无需 BaseVertex。
public FullscreenOutput fullscreenVertex(uint index)
{
    FullscreenOutput result;
    float2 uv = float2((index << 1) & 2, index & 2);
    result.position = float4(uv * 2.0 - 1.0, 0, 1);
    result.uv = uv;
    return result;
}
