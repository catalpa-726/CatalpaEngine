import Common.Math;

public static const float PBR_PI = 3.14159265358979323846;

public float3 fresnelSchlick(float cosine, float3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - saturate(cosine), 5.0);
}

public float distributionGGX(float ndoth, float roughness)
{
    // 可见法线分布下限仅用于有限像素着色，不改作者roughness或源数据。
    float alpha = max(roughness * roughness, 0.002025);
    float a2 = alpha * alpha;
    float denominator = ndoth * ndoth * (a2 - 1.0) + 1.0;
    return a2 / max(PBR_PI * denominator * denominator, 1e-15);
}

public float smithVisibility(float ndotv, float ndotl, float roughness)
{
    float alpha = max(roughness * roughness, 0.002025);
    float a2 = alpha * alpha;
    float gv = ndotl * sqrt(ndotv * ndotv * (1.0 - a2) + a2);
    float gl = ndotv * sqrt(ndotl * ndotl * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-7);
}

public float3 evaluatePbr(float3 base, float metallic, float roughness, float3 normal,
    float3 viewDirection, float3 lightDirection, float3 incident)
{
    float ndotl = saturate(dot(normal, lightDirection));
    float ndotv = max(saturate(dot(normal, viewDirection)), 1e-5);
    float3 halfVector = unitNormal(viewDirection + lightDirection);
    float3 fresnel = fresnelSchlick(dot(viewDirection, halfVector), lerp(float3(0.04), base, metallic));
    float distribution = distributionGGX(saturate(dot(normal, halfVector)), roughness);
    float visibility = smithVisibility(ndotv, ndotl, roughness);
    float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * base / PBR_PI;
    return (diffuse + distribution * visibility * fresnel) * incident * ndotl;
}
