public struct FullscreenOutput {
    public float4 position : SV_Position;
    [[vk::location(0)]] public float2 uv : TEXCOORD0;
};

/**
 * 按顶点索引生成覆盖整个视口的单个三角形。
 * @param index 从零起绘的顶点索引，有效值为 0、1、2。
 */
public FullscreenOutput fullscreenVertex(uint index) {
    FullscreenOutput result;
    float2 uv = float2((index << 1) & 2, index & 2);
    result.position = float4(uv * 2.0 - 1.0, 0, 1);
    result.uv = uv;
    return result;
}
