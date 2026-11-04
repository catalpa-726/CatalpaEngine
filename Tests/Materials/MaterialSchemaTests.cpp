#include "catpch.h"
#include "TestRunner.h"
#include "Materials/MaterialValidation.h"
#include "Images/ImageValidation.h"
#include "Shaders/ShaderLibrary.h"
#include "Assets/BuiltinAssetIds.h"

namespace Catalpa::Tests {
    namespace {
        // 只为CPU结构校验制造明确的metadata替身；该哈希不是实际SPIR-V验证证据。
        ShaderMetadata FixtureMetadata() {
            ShaderMetadata metadata;metadata.bytecodeSha256=std::string(64,'0');
            ShaderEntryPointMetadata vertex;vertex.name="vertexMain";vertex.stage=ShaderStage::Vertex;
            vertex.inputs.push_back({"position",0,{ShaderScalarKind::Float,32,1,3,1}});
            ShaderEntryPointMetadata fragment;fragment.name="fragmentMain";fragment.stage=ShaderStage::Fragment;
            ShaderBinding block;block.name="Material";block.binding=3;block.blockSize=16;
            block.members.push_back({"base_color",0,16,16,0,0,false,{ShaderScalarKind::Float,32,1,4,1},{}});
            fragment.bindings.push_back(std::move(block));
            metadata.entries={std::move(vertex),std::move(fragment)};return metadata;
        }
        // 限量读取仓库fixture：仅只读消费，失败不能退回内联假成功。
        bool ReadFixture(std::string& output) {
            const auto path=std::filesystem::path(__FILE__).parent_path().parent_path()/"Fixtures"/"MaterialTypeMismatch.material.json";
            std::ifstream input(path,std::ios::binary);if(!input)return false;
            input.seekg(0,std::ios::end);const auto size=input.tellg();
            if(size<=0||size>1024*1024)return false;
            std::string candidate(static_cast<size_t>(size),'\0');input.seekg(0);
            if(!input.read(candidate.data(),static_cast<std::streamsize>(candidate.size())))return false;
            output.swap(candidate);return true;
        }
    }
    // 检查定义/实例/共享版本/受限覆盖与最终shader ABI；不创建RHI或GPU对象。
    void RunMaterialSchemaTests(TestContext& test) {
        std::string error;AssetId shader;
        test.Check(ShaderLibrary::BuiltinId("Unlit.slang",shader,error),"Builtin material uses the shared shader identity protocol");
        std::shared_ptr<const MaterialDefinition> definition;
        test.Check(CreateBuiltinMaterialDefinition(BuiltinMaterialKind::Unlit,shader,definition,error),"Builtin unlit definition is validated by the public domain contract");
        if(!definition)return;
        AssetVersionRef<MaterialDefinition> fixed;
        test.Check(AssetVersionRef<MaterialDefinition>::TryCreate(BuiltinAssetIds::DefaultUnlitDefinition,1,definition,fixed,error),"Definition reference pins its immutable value and explicit version");
        std::shared_ptr<const MaterialInstance> first;
        test.Check(CreateMaterialInstance(fixed,{},{},1,first,error),"Instance materializes every validated default");
        if(!first)return;
        const auto original=first;
        std::array changes{MaterialParameterOverride{"base_color",std::array<float,4>{0.2f,0.5f,0.8f,1}}};
        MaterialStateOverrides states;states.cull=MaterialCullMode::Back;
        test.Check(UpdateMaterialInstance(*first,changes,states,first,error),"Allowed parameter and cull updates publish a new instance revision");
        test.Check(first!=original&&first->revision==2&&original->revision==1
            &&first->definition.GetShared()==original->definition.GetShared(),"Immutable instances share a fixed definition while old frame versions remain alive");
        const auto* before=FindMaterialParameter(*original,"base_color");
        const auto* after=FindMaterialParameter(*first,"base_color");
        test.Check(before&&after&&std::get<std::array<float,4>>(*before)[0]==1
            &&std::get<std::array<float,4>>(*after)[0]==0.2f,"Publishing a new instance never edits a retained frame value");
        const auto retained=first;
        changes[0].value=0.5f;
        test.Check(!UpdateMaterialInstance(*first,changes,{},first,error)&&first==retained,"Type mismatch preserves the last published instance");
        changes[0].value=std::array<float,4>{-1,1,1,1};
        test.Check(!UpdateMaterialInstance(*first,changes,{},first,error)&&first==retained,"Out-of-range parameter cannot replace a published value");
        changes[0].parameterId="undeclared";
        test.Check(!UpdateMaterialInstance(*first,changes,{},first,error),"Unknown parameter IDs cannot be added by instances");
        changes[0]={"base_color",std::array<float,4>{1,1,1,1}};
        const std::array duplicate{changes[0],changes[0]};
        test.Check(!UpdateMaterialInstance(*first,duplicate,{},first,error),"Duplicate override IDs are rejected");
        states={};states.depthWrite=false;
        test.Check(!UpdateMaterialInstance(*first,{},states,first,error),"Instances cannot change undeclared render-state policy");
        auto exhausted=*first;exhausted.revision=std::numeric_limits<uint64_t>::max();
        test.Check(!UpdateMaterialInstance(exhausted,{},{},first,error)&&first==retained,"Revision exhaustion is diagnosed without wrapping");
        auto altered=*definition;altered.parameters.push_back(altered.parameters[0]);
        test.Check(!ValidateMaterialDefinition(altered,error),"Duplicate schema parameter IDs are rejected");
        altered=*definition;altered.parameters[0].defaultValue=std::array<float,4>{std::numeric_limits<float>::infinity(),1,1,1};
        test.Check(!ValidateMaterialDefinition(altered,error),"Nonfinite material defaults are rejected");
        altered=*definition;altered.parameters[0].allowOverride=false;
        auto readonly=std::make_shared<const MaterialDefinition>(altered);AssetVersionRef<MaterialDefinition> readonlyRef;
        test.Check(AssetVersionRef<MaterialDefinition>::TryCreate(fixed.GetId(),2,readonly,readonlyRef,error),"Changed definition has its own fixed version");
        test.Check(!CreateMaterialInstance(readonlyRef,changes,{},1,first,error)&&first==retained,"Fixed schema fields cannot be overridden even with a valid value");
        const std::array attributes{VertexAttribute{0,VertexFormat::Float3,0,VertexSemantic::Position}};
        const VertexLayout layout{12,attributes};auto metadata=FixtureMetadata();
        test.Check(ValidateMaterialShaderInterface(*definition,metadata,layout,error),"CPU interface fixture matches float4 Material at binding 3");
        auto badMetadata=metadata;badMetadata.entries[1].bindings[0].members[0].type.scalar=ShaderScalarKind::UnsignedInteger;
        test.Check(!ValidateMaterialShaderInterface(*definition,badMetadata,layout,error),"Shader scalar-kind mismatch is rejected despite equal byte size");
        badMetadata=metadata;badMetadata.entries[0].inputs[0].location=1;
        test.Check(!ValidateMaterialShaderInterface(*definition,badMetadata,layout,error),"Missing shader vertex location is diagnosed");
        badMetadata=metadata;badMetadata.entries[1].bindings[0].members[0].offset=4;
        test.Check(!ValidateMaterialShaderInterface(*definition,badMetadata,layout,error),"Reflected member bounds cannot exceed the actual uniform block");
        std::shared_ptr<const MaterialDefinition> pbr;
        test.Check(CreateBuiltinMaterialDefinition(BuiltinMaterialKind::Pbr,shader,pbr,error),"PBR domain defaults exist independently of GPU lighting support");
        test.Check(pbr&&!ValidateMaterialShaderInterface(*pbr,metadata,layout,error),"Unlit shader cannot falsely validate a PBR parameter block");
        const std::string instanceJson=R"({"schemaVersion":1,"kind":"instance","definitionId":")"+fixed.GetId().ToString()
            +R"(","definitionVersion":1,"revision":7,"parameters":{"base_color":[0.25,0.5,0.75,1]}})";
        test.Check(ParseMaterialInstance(instanceJson,fixed,first,error)&&first->revision==7,"Instance JSON resolves an exact fixed definition before publication");
        const auto parsed=first;
        test.Check(!ParseMaterialInstance(instanceJson,readonlyRef,first,error)&&first==parsed,"Wrong dependency version preserves the published parsed instance");
        test.Check(!ParseMaterialInstance(R"({"schemaVersion":2})",fixed,first,error)&&first==parsed,"Unknown material schema version cannot replace old state");
        std::string fixture;const bool read=ReadFixture(fixture);
        test.Check(read,"The explicit malformed material fixture is present and readable");
        if(read)test.Check(!ParseMaterialDefinition(fixture,definition,error)&&definition==fixed.GetShared(),"Malformed fixture fails the real shared material parser without replacing output");
        MaterialParameter texture;texture.id="normal_map";texture.type=MaterialParameterType::Texture2D;
        texture.textureSemantic=MaterialTextureSemantic::Normal;texture.defaultValue=AssetHandle<ImageAsset>{};
        test.Check(!ValidateMaterialValue(texture,texture.defaultValue,error),"Missing texture without an explicit fallback policy is rejected");
        texture.allowTextureFallback=true;
        test.Check(ValidateMaterialValue(texture,texture.defaultValue,error),"Explicit texture fallback is a domain policy, not a missing-file success");
        ImageAsset image;image.image.format=PixelFormat::RGBA8_UNorm;image.image.width=1;image.image.height=1;
        image.image.semantic=ImageSemantic::Normal;image.image.pixels.resize(4);image.image.subresources.push_back({0,0,0,1,1,1,0,4,4,4});
        test.Check(ValidateMaterialTexture(texture,image,error),"Resolved normal texture must carry normal semantics");
        image.image.semantic=ImageSemantic::Color;
        test.Check(!ValidateMaterialTexture(texture,image,error),"Texture dependencies cannot silently reinterpret color as normals");
        fixed={};definition.reset();readonly.reset();readonlyRef={};
        test.Check(original->definition.HasValue()&&ValidateMaterialInstance(*original,error),"Retained old instance keeps its definition alive after external owners release");
    }
}
