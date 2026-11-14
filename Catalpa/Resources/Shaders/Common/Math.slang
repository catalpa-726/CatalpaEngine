/**
 * 显式计算矩阵与列向量的乘积，直接使用传入的列顺序。
 * @param c0 矩阵的第一列，与 value.x 相乘。
 * @param c1 矩阵的第二列，与 value.y 相乘。
 * @param c2 矩阵的第三列，与 value.z 相乘。
 * @param c3 矩阵的第四列，与 value.w 相乘。
 * @param value 待变换的四分量列向量。
 */
public float4 multiplyColumns(float4 c0, float4 c1, float4 c2, float4 c3, float4 value) {
    return c0 * value.x + c1 * value.y + c2 * value.z + c3 * value.w;
}
/**
 * 先按最大绝对分量缩放，再计算单位方向，以避免向量长度平方溢出或下溢。
 * @param value 待单位化的三维方向，零向量返回零。
 */
public float3 unitNormal(float3 value) {
    float scale = max(abs(value.x), max(abs(value.y), abs(value.z)));
    if (!(scale > 0.0))
        return float3(0.0);
    float3 scaled = value / scale;
    return scaled * rsqrt(dot(scaled, scaled));
}
/**
 * 用逆转置矩阵变换局部单位法线。三个矩阵列采用同一正比例缩放，避免原始法线长度影响插值权重。
 * @param c0 逆转置矩阵的第一列，仅使用 xyz 分量。
 * @param c1 逆转置矩阵的第二列，仅使用 xyz 分量。
 * @param c2 逆转置矩阵的第三列，仅使用 xyz 分量。
 * @param value 网格顶点的局部法线，先转为单位方向再变换。
 */
public float3 transformNormalColumns(float4 c0, float4 c1, float4 c2, float3 value) {
    float3 maxima = max(abs(c0.xyz), max(abs(c1.xyz), abs(c2.xyz)));
    float scale = max(maxima.x, max(maxima.y, maxima.z));
    if (!(scale > 0.0))
        return float3(0.0);
    float3 local = unitNormal(value);
    return (c0.xyz / scale) * local.x + (c1.xyz / scale) * local.y + (c2.xyz / scale) * local.z;
}
