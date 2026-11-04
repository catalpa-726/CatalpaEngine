// 列向量约定的显式乘法；不依赖编译器默认matrix-major布局，也不重复Vulkan Y翻转。
public float4 multiplyColumns(float4 c0, float4 c1, float4 c2, float4 c3, float4 value)
{
    return c0 * value.x + c1 * value.y + c2 * value.z + c3 * value.w;
}
// 只单位化派生着色方向；先按最大分量缩放，避免平方溢出或下溢。
public float3 unitNormal(float3 value)
{
    float scale = max(abs(value.x), max(abs(value.y), abs(value.z)));
    if (!(scale > 0.0)) return float3(0.0);
    float3 scaled = value / scale;
    return scaled * rsqrt(dot(scaled, scaled));
}
// Normal语义按局部单位方向插值，不把作者向量长度作为权重；原始网格保持不变。
// inverse-transpose按同draw公共最大分量缩放，先避免乘加溢出/下溢；公共正缩放不改方向和插值权重。
public float3 transformNormalColumns(float4 c0, float4 c1, float4 c2, float3 value)
{
    float3 maxima = max(abs(c0.xyz), max(abs(c1.xyz), abs(c2.xyz)));
    float scale = max(maxima.x, max(maxima.y, maxima.z));
    if (!(scale > 0.0)) return float3(0.0);
    float3 local = unitNormal(value);
    return (c0.xyz / scale) * local.x + (c1.xyz / scale) * local.y + (c2.xyz / scale) * local.z;
}
