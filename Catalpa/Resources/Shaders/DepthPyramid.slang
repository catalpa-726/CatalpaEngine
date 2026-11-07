[[vk::binding(0,0)]] Texture2D<float> sceneDepth;
[[vk::binding(1,0)]] SamplerState depthSampler;
[[vk::binding(2,0)]] RWByteAddressBuffer pyramid;
struct Parameters {uint sourceOffset;uint destinationOffset;uint sourceWidth;uint sourceHeight;uint width;uint height;uint level;uint reversedZ;};
[[vk::push_constant]] ConstantBuffer<Parameters> p;
[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 id:SV_DispatchThreadID) {
    if(id.x>=p.width||id.y>=p.height)return;
    float value=p.reversedZ!=0?1.0:0.0;
    if(p.level==0) value=sceneDepth.SampleLevel(depthSampler,(float2(id.xy)+0.5)/float2(p.width,p.height),0);
    else {
        for(uint y=0;y<2;++y) for(uint x=0;x<2;++x) {
            uint2 source=id.xy*2+uint2(x,y);
            if(source.x>=p.sourceWidth||source.y>=p.sourceHeight)continue;
            float sample=asfloat(pyramid.Load(p.sourceOffset+(source.y*p.sourceWidth+source.x)*4));
            value=p.reversedZ!=0?min(value,sample):max(value,sample);
        }
    }
    pyramid.Store(p.destinationOffset+(id.y*p.width+id.x)*4,asuint(value));
}
