/* Frame/View/Draw ABI：float4列拥有固定16字节对齐，CPU见RendererFrameContext.h。 */
public struct FrameParameters {
    public float4 time; /* simulation模4096秒, realDelta, fixedDelta, interpolationAlpha。 */
    public float4 options; /* x=normalDebug,y=Blend拾取alpha阈值,z=4096秒epoch,w保留。 */
};
public struct ViewParameters {
    public float4 viewProjectionColumns[4];
    public float4 cameraPosition;
    public float4 inverseViewProjectionColumns[4];
    public float4 viewColumns[4];
};
public struct DrawParameters {
    public float4 worldColumns[4];
    public float4 normalColumns[3];
    public float4 color;
    public uint4 ids; /* x为一基绘制token(0背景)，y bit0为镜像；不把EntityId截断为uint。 */
};
[[vk::binding(0, 0)]] public ConstantBuffer<FrameParameters> frameData;
[[vk::binding(1, 0)]] public ConstantBuffer<ViewParameters> viewData;
[[vk::binding(2, 0)]] public ConstantBuffer<DrawParameters> drawData;
