#include "TestRunner.h"
#include "Support/FakeRHI.h"
#include "Renderer/Resources/GeometryGpuCache.h"
#include "Renderer/Resources/TextureGpuCache.h"
#include "Renderer/Resources/MaterialGpuCache.h"
#include "Renderer/Resources/PipelineLibrary.h"
#include "Renderer/RendererFrameContext.h"
#include "Renderer/Uploads/UploadScheduler.h"
#include "Geometry/PrimitiveMesh.h"
#include "Materials/MaterialValidation.h"
#include "Assets/AssetContainerReader.h"
#include "Core/PlatformServices.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Shaders/ShaderLibrary.h"

namespace Catalpa::Tests {
    namespace {
        // 仅计数逻辑owner/失败路径，不伪造GPU执行、completion或原生内存释放证明。
        class CacheDevice final : public FakeDevice {
        public:
            uint32_t createdBuffers=0,releasedBuffers=0,createdTextures=0,releasedTextures=0,createdPipelines=0,releasedPipelines=0;
            RHIStatus CreateBuffer(const RHIBufferDesc& desc,BufferHandle& out) override {
                RHIBufferStateSnapshot state;state.desc=desc;out=AddBuffer(state);++createdBuffers;return {};
            }
            RHIStatus WriteBuffer(BufferHandle,uint64_t,std::span<const std::byte>) override {return {};}
            void ReleaseBuffer(BufferHandle value) override {if(value.HasValue()) ++releasedBuffers;}
            RHIFormatSupport GetFormatSupport(PixelFormat) const override {return {.sampledImage=true,.linearFiltering=true,.transferDestination=true};}
            RHIStatus CreateTexture(const RHITextureDesc&,TextureHandle& out) override {out={GetDeviceId(),{createdTextures++,1}};return {};}
            RHIStatus CreateTextureView(TextureHandle texture,const RHITextureViewDesc&,TextureViewHandle& out) override {out={texture.device,{texture.slot.index,texture.slot.generation}};return {};}
            void ReleaseTexture(TextureHandle value) override {if(value.HasValue()) ++releasedTextures;}
            RHIStatus CreateGraphicsPipeline(const RHIGraphicsPipelineDesc&,GraphicsPipelineHandle& out) override {out={GetDeviceId(),{createdPipelines++,1}};return {};}
            void ReleaseGraphicsPipeline(GraphicsPipelineHandle value) override {if(value.HasValue()) ++releasedPipelines;}
        };
    }
    void RunGpuCacheTests(TestContext& test)
    {
        CacheDevice device;UploadScheduler uploads(device);std::string error;
        MeshData cube;MeshResourceRef mesh;
        test.Check(CreateCubeMesh(1.0f,cube).IsSuccess()&&CreateMeshResource(cube,mesh).IsSuccess(),"Cache fixture uses validated geometry");
        if(!mesh) return;
        const auto id=AssetId::FromWords(0x1234,1);
        auto data=std::make_shared<const MeshAsset>(MeshAsset{mesh,{"default"}});
        AssetVersionRef<MeshAsset> version;
        test.Check(AssetVersionRef<MeshAsset>::TryCreate(id,1,data,version,error),"Geometry cache gets fixed version");
        GeometryGpuCache geometry(device,uploads);
        std::shared_ptr<GeometryGpuResource> a,b;
        test.Check(geometry.Acquire(version,a).IsSuccess()&&geometry.Acquire(version,b).IsSuccess()&&a==b,"Same geometry version has one GPU owner");
        test.Check(device.createdBuffers==4,"One vertex/index upload has exactly two destination and two staging buffers");
        a.reset();b.reset();geometry.Prune();
        test.Check(geometry.Size()==1&&device.releasedBuffers==0,"Unreferenced pending upload keeps target owners alive");
        AssetVersionRef<MeshAsset> collision;
        AssetVersionRef<MeshAsset>::TryCreate(id,1,std::make_shared<const MeshAsset>(*data),collision,error);
        test.Check(!geometry.Acquire(collision,a).IsSuccess(),"Conflicting payload for one identity/version is rejected");
        uploads.Shutdown();geometry.Clear();
        test.Check(device.releasedBuffers==4,"Cancellation shutdown releases staging before cached destinations");

        ImageAsset image;image.image.width=1;image.image.height=1;image.image.format=PixelFormat::RGBA8_UNorm;
        image.image.pixels.assign(4,std::byte{255});image.image.subresources.push_back({0,0,0,1,1,1,0,4,4,4});
        AssetVersionRef<ImageAsset> imageVersion;
        AssetVersionRef<ImageAsset>::TryCreate(AssetId::FromWords(0x1234,2),1,std::make_shared<const ImageAsset>(image),imageVersion,error);
        TextureGpuCache textures(device,uploads);
        std::shared_ptr<TextureGpuResource> textureA,textureB;
        test.Check(textures.Acquire(imageVersion,textureA).IsSuccess()&&textures.Acquire(imageVersion,textureB).IsSuccess()&&textureA==textureB,"Image version reuses exactly one texture/view");
        test.Check(device.createdTextures==1,"Shared image does not create duplicate GPU textures");
        uploads.Shutdown();textures.Clear();textureA.reset();
        test.Check(device.releasedTextures==0,"A retained texture lease survives cache invalidation");
        textureB.reset();test.Check(device.releasedTextures==1,"Last texture owner retires through RHI release");

        AssetId shaderId;ShaderLibrary::BuiltinId("Unlit.slang",shaderId,error);
        std::shared_ptr<const MaterialInstance> instance;
        test.Check(CreateBuiltinMaterialInstance(BuiltinMaterialKind::Unlit,AssetId::FromWords(0x1234,3),1,shaderId,instance,error),"Unlit material factory");
        if(!instance) return;
        AssetVersionRef<MaterialInstance> materialVersion;
        AssetVersionRef<MaterialInstance>::TryCreate(AssetId::FromWords(0x1234,4),1,instance,materialVersion,error);
        MaterialGpuCache materials(device);
        std::shared_ptr<MaterialGpuResource> materialA,materialB;
        const auto before=device.createdBuffers;
        test.Check(materials.Acquire(materialVersion,materialA).IsSuccess()&&materials.Acquire(materialVersion,materialB).IsSuccess()&&materialA==materialB,"Fixed material version reuses immutable parameter buffer");
        test.Check(device.createdBuffers==before+1,"Material cache owns only Material UBO, not Frame/View/Draw");
        materials.Clear();materialA.reset();materialB.reset();

        // 使用本次构建真正产出的最终容器/metadata，CPU替身不执行shader。
        std::filesystem::path root;
        test.Check(GetExecutableDirectory(root,error),error);
        VirtualFileSystem files;
        if(!files.MountReadOnly("shaders",root/"Resources"/"Shaders",0,error)) {test.Check(false,error);return;}
        std::vector<AssetRecord> catalog;
        if(!ShaderLibrary::ReadBuiltinCatalog(root/"Resources"/"Shaders","shaders",catalog,error)) {test.Check(false,error);return;}
        // 统一从当前manifest读取真实产物；不执行SDK，也不改动生成文件。
        const auto readShader=[&](std::string_view source,AssetVersionRef<ShaderAsset>& output) {
            AssetId shaderId;
            if(!ShaderLibrary::BuiltinId(source,shaderId,error)) return false;
            const auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto& r){return r.id==shaderId;});
            if(found==catalog.end()) {error="Current shader manifest is missing "+std::string(source);return false;}
            std::vector<std::byte> bytes;AssetContainer container;std::shared_ptr<const ShaderAsset> shader;
            if(!files.ReadFile(found->locator.path,16*1024*1024,bytes,error)||!AssetContainerReader::Read(bytes,container,error)
                ||!ShaderLibrary::DecodePayload(container.payload,shader,error)) return false;
            if(container.id!=found->id||container.version!=found->version) {error="Shader identity differs from current manifest";return false;}
            return AssetVersionRef<ShaderAsset>::TryCreate(container.id,container.version,shader,output,error);
        };
        AssetVersionRef<ShaderAsset> shaderVersion;
        if(!readShader("ImGui.slang",shaderVersion)) {test.Check(false,error);return;}
        const std::array<VertexAttribute,3> attributes{{{0,VertexFormat::Float2,0},{1,VertexFormat::Float2,8},{2,VertexFormat::Float4,16}}};
        const std::array<RHIBindingLayoutEntry,2> bindings{{{0,RHIBindingType::SampledTexture,RHIShaderVisibility::Fragment},{1,RHIBindingType::Sampler,RHIShaderVisibility::Fragment}}};
        RHIGraphicsPipelineDesc desc;
        desc.vertex={shaderVersion,RHIShaderStage::Vertex,"vertexMain"};desc.fragment={shaderVersion,RHIShaderStage::Fragment,"fragmentMain"};
        desc.colorFormat=PixelFormat::RGBA8_SRGB;desc.vertexLayout={32,attributes};
        desc.pushConstants={16,RHIShaderVisibility::Vertex};desc.bindingLayout={bindings};
        test.Check(ValidateRHIPipelineShaderInterface(desc,error),"Real cooked UI metadata agrees with the host pipeline ABI");
        PipelineLibrary pipelines(device);std::shared_ptr<PipelineResource> pipelineA,pipelineB;
        test.Check(pipelines.Acquire(desc,pipelineA).IsSuccess()&&pipelines.Acquire(desc,pipelineB).IsSuccess()&&pipelineA==pipelineB,"Complete pipeline key reuses native owner");
        test.Check(device.createdPipelines==1,"Repeated descriptor does not create a second pipeline");
        desc.colorFormat=PixelFormat::BGRA8_SRGB;
        std::shared_ptr<PipelineResource> other;
        test.Check(pipelines.Acquire(desc,other).IsSuccess()&&other!=pipelineA,"Attachment format participates in the pipeline key");
        desc.pushConstants.size=32;
        test.Check(!pipelines.Acquire(desc,other).IsSuccess(),"Incorrect reflected ABI never reaches backend pipeline creation");
        pipelines.Clear();pipelineA.reset();pipelineB.reset();other.reset();
        test.Check(device.createdPipelines==device.releasedPipelines,"All pipeline logical owners release through existing RHI");

        AssetVersionRef<ShaderAsset> unlit,depth;
        if(!readShader("Unlit.slang",unlit)||!readShader("Depth.slang",depth)) {test.Check(false,error);return;}
        const auto validateFrame=[&](const ShaderMetadata& metadata) {
            return ValidateFrameShaderInterface(metadata,"vertexMain","fragmentMain",error);
        };
        const bool frameMatches=validateFrame(unlit.Get()->metadata);
        test.Check(frameMatches,"Actual cooked Unlit uses the same Frame/View/Draw ABI validator as Renderer: "+error);
        if(!frameMatches) return;

        // 只变更测试副本；包装与直接数组的内存ABI相同，原metadata保持真实SPIR-V结构。
        auto flattened=unlit.Get()->metadata;
        for(auto& entry:flattened.entries) for(auto& binding:entry.bindings) for(auto& member:binding.members)
            if((member.name=="viewProjectionColumns"||member.name=="worldColumns"||member.name=="normalColumns")
                &&member.type.scalar==ShaderScalarKind::Structure&&member.members.size()==1) {
                auto leaf=member.members.front();leaf.name=member.name;leaf.offset=member.offset;member=std::move(leaf);
            }
        test.Check(validateFrame(flattened),"Equivalent direct float4 arrays retain the complete host byte layout");
        const auto viewColumns=[](ShaderMetadata& metadata)->ShaderBlockMember* {
            for(auto& entry:metadata.entries) if(entry.stage==ShaderStage::Vertex&&entry.name=="vertexMain")
                for(auto& binding:entry.bindings) if(binding.set==0&&binding.binding==1)
                    for(auto& member:binding.members) if(member.name=="viewProjectionColumns") return &member;
            return nullptr;
        };
        auto fixture=unlit.Get()->metadata;
        auto* columns=viewColumns(fixture);
        if(!columns) {test.Check(false,"Actual Unlit exposes View columns");return;}
        columns->offset+=16;
        test.Check(!validateFrame(fixture),"Changed outer member offset cannot pass Frame/View/Draw ABI");
        fixture=unlit.Get()->metadata;columns=viewColumns(fixture);
        auto* leaf=columns->type.scalar==ShaderScalarKind::Structure?&columns->members.front():columns;
        leaf->arrayStride=32;
        test.Check(!validateFrame(fixture),"Changed matrix-column stride is rejected");
        fixture=unlit.Get()->metadata;columns=viewColumns(fixture);
        leaf=columns->type.scalar==ShaderScalarKind::Structure?&columns->members.front():columns;
        leaf->type.scalar=ShaderScalarKind::SignedInteger;
        test.Check(!validateFrame(fixture),"Same-sized integer columns cannot replace float columns");
        fixture=unlit.Get()->metadata;columns=viewColumns(fixture);
        leaf=columns->type.scalar==ShaderScalarKind::Structure?&columns->members.front():columns;
        leaf->matrixStride=16;
        test.Check(!validateFrame(fixture),"A matrix decoration cannot reinterpret a float4 array");
        fixture=unlit.Get()->metadata;columns=viewColumns(fixture);
        leaf=columns->type.scalar==ShaderScalarKind::Structure?&columns->members.front():columns;
        leaf->rowMajor=true;
        test.Check(!validateFrame(fixture),"Row-major metadata cannot reinterpret explicit host columns");
        fixture=unlit.Get()->metadata;columns=viewColumns(fixture);
        if(columns->type.scalar==ShaderScalarKind::Structure) {
            columns->members.front().offset=16;
            test.Check(!validateFrame(fixture),"Array wrapper data must start at byte zero");
            fixture=unlit.Get()->metadata;columns=viewColumns(fixture);
            columns->members.front().name="other";
            test.Check(!validateFrame(fixture),"Unknown structure wrappers are not silently accepted");
        }

        // 真实forward及prepass组合还必须通过RHI跨stage/raw metadata检查。
        const std::array<RHIBindingLayoutEntry,4> sceneBindings{{
            {0,RHIBindingType::UniformBuffer,RHIShaderVisibility::Fragment,sizeof(FrameGpuParameters)},
            {1,RHIBindingType::UniformBuffer,RHIShaderVisibility::Vertex,sizeof(ViewGpuParameters)},
            {2,RHIBindingType::UniformBuffer,RHIShaderVisibility::VertexFragment,sizeof(DrawGpuParameters)},
            {3,RHIBindingType::UniformBuffer,RHIShaderVisibility::Fragment,16}}};
        RHIGraphicsPipelineDesc scenePipeline;
        scenePipeline.vertex={unlit,RHIShaderStage::Vertex,"vertexMain"};scenePipeline.fragment={unlit,RHIShaderStage::Fragment,"fragmentMain"};
        // 与Renderer一致：当前Unlit只消费位置/法线，网格中的UV不属于该pipeline输入。
        std::vector<VertexAttribute> sceneInputs;
        for(const auto& attribute:mesh->GetData().attributes) if(attribute.location<2) sceneInputs.push_back(attribute);
        scenePipeline.vertexLayout={mesh->GetData().vertexStride,sceneInputs};scenePipeline.bindingLayout={sceneBindings};
        scenePipeline.colorFormat=PixelFormat::RGBA8_SRGB;scenePipeline.depthFormat=PixelFormat::D32_Float;
        const bool forwardMatches=ValidateRHIPipelineShaderInterface(scenePipeline,error);
        test.Check(forwardMatches,"Actual cooked forward descriptor/stage ABI: "+error);
        const std::array<RHIBindingLayoutEntry,2> depthBindings{{
            {1,RHIBindingType::UniformBuffer,RHIShaderVisibility::Vertex,sizeof(ViewGpuParameters)},
            {2,RHIBindingType::UniformBuffer,RHIShaderVisibility::Vertex,sizeof(DrawGpuParameters)}}};
        scenePipeline.fragment={depth,RHIShaderStage::Fragment,"fragmentMain"};scenePipeline.colorFormat=PixelFormat::Unknown;scenePipeline.bindingLayout={depthBindings};
        const bool depthMatches=ValidateRHIPipelineShaderInterface(scenePipeline,error);
        test.Check(depthMatches,"Actual Unlit vertex and Depth fragment preserve prepass descriptor/stage ABI: "+error);
    }
}
