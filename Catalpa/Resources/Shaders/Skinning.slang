/* 顶点64字节与权重32字节均按显式偏移读写，避免float3的std430布局差异。 */
[[vk::binding(0, 0)]] ByteAddressBuffer sourceVertices;
[[vk::binding(1, 0)]] ByteAddressBuffer skinWeights;
[[vk::binding(2, 0)]] ByteAddressBuffer skinPalette;
[[vk::binding(3, 0)]] RWByteAddressBuffer outputVertices;
[[vk::binding(4, 0)]] RWByteAddressBuffer diagnostics;
struct Parameters {
    uint vertexCount;
    uint jointCount;
    uint unused0;
    uint unused1;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;

/**
 * 按四个关节的权重混合矩阵并变换顶点。诊断位 0 表示位置非法，位 1 表示着色基底回退。
 * @param dispatchThreadID x 为当前顶点的索引，同时定位源顶点、蒙皮权重和输出顶点。
 */
[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint vertex = dispatchThreadID.x;
    if (vertex >= parameters.vertexCount)
        return;
    uint address = vertex * 64;
    uint4 joints = skinWeights.Load4(vertex * 32);
    float4 weights = asfloat(skinWeights.Load4(vertex * 32 + 16));
    weights /= max(dot(weights, float4(1)), 1e-8); /* CPU已验证非负且和接近1；不改源权重。 */
    float4 c0 = 0, c1 = 0, c2 = 0, c3 = 0;
    for (uint j = 0; j < 4; ++j) {
        uint bone = min(joints[j], parameters.jointCount - 1);
        c0 += asfloat(skinPalette.Load4(bone * 64)) * weights[j];
        c1 += asfloat(skinPalette.Load4(bone * 64 + 16)) * weights[j];
        c2 += asfloat(skinPalette.Load4(bone * 64 + 32)) * weights[j];
        c3 += asfloat(skinPalette.Load4(bone * 64 + 48)) * weights[j];
    }
    float3 position = asfloat(sourceVertices.Load3(address));
    float3 normal = asfloat(sourceVertices.Load3(address + 12));
    float4 tangent = asfloat(sourceVertices.Load4(address + 24));
    float3 transformed = c0.xyz * position.x + c1.xyz * position.y + c2.xyz * position.z + c3.xyz;
    float3 a = c0.xyz, b = c1.xyz, c = c2.xyz;
    float scale = max(max(max(abs(a.x), abs(a.y)), abs(a.z)),
                      max(max(max(abs(b.x), abs(b.y)), abs(b.z)), max(max(abs(c.x), abs(c.y)), abs(c.z))));
    float divisor = scale > 0 ? scale : 1;
    float3 an = a / divisor, bn = b / divisor, cn = c / divisor;
    float determinant = dot(an, cross(bn, cn));
    float3 n = normal;
    float3 t = tangent.xyz;
    bool positionValid = !any(isnan(transformed)) && !any(isinf(transformed));
    bool basisValid = scale > 0 && !isinf(scale) && !isnan(scale) && abs(determinant) > 1e-12;
    if (basisValid) {
        n = normalize((cross(bn, cn) * normal.x + cross(cn, an) * normal.y + cross(an, bn) * normal.z) /
                      determinant);
        if (any(isnan(n)) || any(isinf(n))) {
            uint previous;
            diagnostics.InterlockedOr(0, 2, previous);
            n = normal;
        }
        t = an * t.x + bn * t.y + cn * t.z;
        t -= n * dot(n, t);
        if (dot(t, t) <= 1e-20 || any(isnan(t)) || any(isinf(t))) {
            uint previous;
            diagnostics.InterlockedOr(0, 2, previous);
            float3 axis = abs(n.x) < abs(n.y) ? (abs(n.x) < abs(n.z) ? float3(1, 0, 0) : float3(0, 0, 1))
                                              : (abs(n.y) < abs(n.z) ? float3(0, 1, 0) : float3(0, 0, 1));
            t = cross(axis, n);
        }
        t = normalize(t);
    }
    /* 混合矩阵的基底奇异不代表变形位置无效；保留有限的位置，单独报告着色基底回退。 */
    if (!basisValid) {
        uint previous;
        diagnostics.InterlockedOr(0, 2, previous);
        n = normal;
        t = tangent.xyz;
        determinant = 1;
    }
    if (!positionValid) {
        uint previous;
        diagnostics.InterlockedOr(0, 1, previous);
        transformed = position;
    }
    outputVertices.Store3(address, asuint(transformed));
    outputVertices.Store3(address + 12, asuint(n));
    outputVertices.Store4(address + 24, asuint(float4(t, tangent.w * (determinant < 0 ? -1 : 1))));
    outputVertices.Store2(address + 40, sourceVertices.Load2(address + 40));
    outputVertices.Store4(address + 48, sourceVertices.Load4(address + 48));
}
