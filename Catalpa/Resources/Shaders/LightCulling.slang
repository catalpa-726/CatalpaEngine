[[vk::binding(0,0)]] ByteAddressBuffer lightRects;
[[vk::binding(1,0)]] RWByteAddressBuffer tileLights;
struct Parameters {uint columns;uint rows;uint count;uint unused;};
[[vk::push_constant]] ConstantBuffer<Parameters> p;
[shader("compute")]
[numthreads(64,1,1)]
void computeMain(uint3 id:SV_DispatchThreadID) {
    uint tile=id.x;if(tile>=p.columns*p.rows)return;
    // 头部和列表统一使用 uint 标量视图，避免常量地址的 Store4 生成同槽向量别名。
    if(tile==0) {
        tileLights.Store(0,p.columns);
        tileLights.Store(4,p.rows);
        tileLights.Store(8,p.count);
        tileLights.Store(12,0u);
    }
    float2 minimum=float2(tile%p.columns,tile/p.columns)*16;
    float2 maximum=minimum+16;
    uint base=16+tile*33*4,count=0;
    // 每tile容量32等于当前正式灯光容量；仍写完整unused槽，提供确定初始化证据。
    for(uint slot=0;slot<32;++slot)tileLights.Store(base+4+slot*4,0u);
    for(uint light=0;light<min(p.count,32u);++light) {
        float4 rectangle=asfloat(lightRects.Load4(light*16));
        if(all(rectangle.xy<=maximum)&&all(rectangle.zw>=minimum))tileLights.Store(base+4+(count++)*4,light);
    }
    tileLights.Store(base,count);
}
