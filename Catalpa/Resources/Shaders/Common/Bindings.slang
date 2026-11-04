// Frame/View/Draw ABI：float4列拥有固定16字节对齐，CPU见RendererFrameContext.h。
public struct FrameParameters
{
    public float4 time;    // simulation, realDelta, fixedDelta, interpolationAlpha，秒。
    public float4 options; // x=normalDebug，其余0。
};
public struct ViewParameters
{
    public float4 viewProjectionColumns[4];
    public float4 cameraPosition;
};
public struct DrawParameters
{
    public float4 worldColumns[4];
    public float4 normalColumns[3];
    public float4 color;
};
[[vk::binding(0, 0)]] public ConstantBuffer<FrameParameters> frameData;
[[vk::binding(1, 0)]] public ConstantBuffer<ViewParameters> viewData;
[[vk::binding(2, 0)]] public ConstantBuffer<DrawParameters> drawData;
