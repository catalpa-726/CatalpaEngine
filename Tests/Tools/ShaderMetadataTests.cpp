#include "catpch.h"
#include "TestRunner.h"
#include "Shaders/ShaderLibrary.h"
#include "ShaderCompiler/ShaderCompileJob.h"
#include "ShaderCompiler/SlangCompiler.h"
#include "ShaderCompiler/SpirvReflection.h"
#include "AssetPipeline/Cookers/ShaderCooker.h"
#include "Commands/ToolCommandRunner.h"
#include "Assets/AssetContainerReader.h"
#include "RHI/RHIShaderDesc.h"
namespace Catalpa::Tests {
    namespace {
        // Minimal SPIR-V compute fixture: Shader capability, Logical/GLSL450, main local-size 1, empty function.
        // These CPU adapter tests do not execute or substitute for the SDK validator/cook integration acceptance.
        ShaderAsset ComputeFixture(std::string& error){
            ShaderAsset a;a.spirv={0x07230203,0x00010500,0,5,0,
                (2u<<16)|17u,1,(3u<<16)|14u,0,1,
                (5u<<16)|15u,5,3,0x6e69616d,0,
                (6u<<16)|16u,3,17,1,1,1,
                (2u<<16)|19u,1,(3u<<16)|33u,2,1,
                (5u<<16)|54u,1,3,0,2,(2u<<16)|248u,4,(1u<<16)|253u,(1u<<16)|56u};
            a.variant.sha256=std::string(64,'a');a.variant.inputFingerprint=std::string(64,'b');
            Tools::SpirvReflection::Reflect(a.spirv,a.metadata,error);return a;
        }
        std::vector<uint32_t> ArrayBlockFixture(bool specialized) {
            std::vector<uint32_t> words{0x07230203,0x00010500,0,17,0};
            const auto emit=[&](uint32_t opcode,std::initializer_list<uint32_t> operands){words.push_back((uint32_t(operands.size()+1)<<16)|opcode);words.insert(words.end(),operands);};
            const auto name=[&](uint32_t opcode,std::initializer_list<uint32_t> prefix,std::string_view text){
                const size_t count=(text.size()+1+3)/4;words.push_back((uint32_t(1+prefix.size()+count)<<16)|opcode);words.insert(words.end(),prefix);
                for(size_t i=0;i<count;++i){uint32_t word=0;for(size_t b=0;b<4&&i*4+b<text.size();++b)word|=uint32_t(uint8_t(text[i*4+b]))<<(b*8);words.push_back(word);}
            };
            emit(17,{1});emit(14,{0,1});emit(15,{5,3,0x6e69616d,0,12});emit(16,{3,17,1,1,1});
            name(5,{12},"viewData");name(6,{10,0},"viewProjectionColumns");
            emit(71,{10,2});emit(72,{10,0,35,0});emit(71,{9,6,16});emit(71,{12,34,0});emit(71,{12,33,1});
            if(specialized)emit(71,{8,1,0}); // Specialization ID zero must be distinguished from an ordinary constant.
            emit(19,{1});emit(33,{2,1});emit(22,{5,32});emit(23,{6,5,4});emit(21,{7,32,0});
            emit(specialized?50:43,{7,8,4});emit(28,{9,6,8});emit(30,{10,9});emit(32,{11,2,10});emit(59,{11,12,2});
            emit(32,{13,2,6});emit(43,{7,14,0});emit(54,{1,3,0,2});emit(248,{4});emit(65,{13,15,12,14,14});emit(61,{6,16,15});emit(253,{});emit(56,{});
            return words;
        }
        std::vector<uint32_t> MixedInterfaceFixture() {
            // Output block has Position builtin and one user location; its parent is marked BUILT_IN by Reflect.
            std::vector<uint32_t> words{0x07230203,0x00010500,0,18,0};
            const auto emit=[&](uint32_t opcode,std::initializer_list<uint32_t> operands){words.push_back((uint32_t(operands.size()+1)<<16)|opcode);words.insert(words.end(),operands);};
            emit(17,{1});emit(14,{0,1});emit(15,{0,3,0x6e69616d,0,9});
            emit(6,{7,1,0x6f6c6f63,0x72}); // OpMemberName "color"
            emit(71,{7,2});emit(72,{7,0,11,0});emit(72,{7,1,30,0});
            emit(19,{1});emit(33,{2,1});emit(22,{5,32});emit(23,{6,5,4});emit(30,{7,6,6});emit(32,{8,3,7});emit(59,{8,9,3});
            emit(43,{5,10,0});emit(44,{6,11,10,10,10,10});emit(21,{12,32,0});emit(43,{12,13,0});emit(43,{12,14,1});emit(32,{15,3,6});
            emit(54,{1,3,0,2});emit(248,{4});emit(65,{15,16,9,13});emit(62,{16,11});emit(65,{15,17,9,14});emit(62,{17,11});emit(253,{});emit(56,{});
            return words;
        }
        ShaderMetadata LayoutFixture(){
            ShaderMetadata metadata;metadata.bytecodeSha256=std::string(64,'c');
            ShaderEntryPointMetadata entry;entry.name="draw";entry.stage=ShaderStage::Vertex;
            ShaderBlockMember member;member.name="viewProjectionColumns";member.size=64;member.paddedSize=64;member.arrayStride=16;member.type.columns=4;member.type.arrayCount=4;
            ShaderBinding binding;binding.name="viewData";binding.binding=1;binding.blockSize=64;binding.members.push_back(member);entry.bindings.push_back(binding);
            entry.inputs.push_back({"position",0,{ShaderScalarKind::Float,32,1,3,1}});metadata.entries.push_back(entry);return metadata;
        }
    }
    void RunShaderMetadataTests(TestContext& test){
        std::string error;auto asset=ComputeFixture(error);
        test.Check(error.empty()&&asset.metadata.entries.size()==1,"final SPIR-V adapter discovers compute entry without hardcoded main/stage configuration");
        if(!error.empty()||asset.metadata.entries.size()!=1)return; // Preserve the adapter failure without indexing an absent fixture entry.
        test.Check(FindShaderEntry(asset.metadata,"main",ShaderStage::Compute)!=nullptr,"SPIR-V entry name/stage are preserved");
        test.Check(FindShaderEntry(asset.metadata,"main",ShaderStage::Vertex)==nullptr,"same name with different stage is not accepted");
        test.Check(ValidateShaderAsset(asset,error),"shader asset verifies header, metadata and actual SHA256");
        auto damaged=asset;damaged.spirv.back()^=1;
        test.Check(!ValidateShaderAsset(damaged,error),"changed final SPIR-V invalidates recorded hash");
        damaged=asset;damaged.variant.inputFingerprint="timestamp";test.Check(!ValidateShaderAsset(damaged,error),"incremental identity requires a real SHA256-shaped key");
        damaged=asset;damaged.metadata.targetEnvironment="vulkan1.0";test.Check(!ValidateShaderAsset(damaged,error),"unsupported target environment is rejected");
        damaged=asset;damaged.metadata.entries.push_back(damaged.metadata.entries[0]);test.Check(!ValidateShaderAsset(damaged,error),"duplicate entry/stage pairs are rejected");
        damaged=asset;auto sameName=damaged.metadata.entries[0];sameName.stage=ShaderStage::Vertex;damaged.metadata.entries.push_back(sameName);
        test.Check(!ValidateShaderMetadata(damaged.metadata,error),"Cross-stage duplicate names are rejected before name-only reflection APIs can conflate entries");
        ShaderMetadata reflectedArray;const auto ordinaryArray=ArrayBlockFixture(false);
        const bool arrayReflected=Tools::SpirvReflection::Reflect(ordinaryArray,reflectedArray,error);
        test.Check(arrayReflected,"SPIRV-Reflect UINT32_MAX absent-specialization sentinel permits a fixed float4 array");
        if(arrayReflected&&reflectedArray.entries.size()==1&&reflectedArray.entries[0].bindings.size()==1&&reflectedArray.entries[0].bindings[0].members.size()==1){
            const auto& block=reflectedArray.entries[0].bindings[0];const auto& member=block.members[0];
            test.Check(block.blockSize==64&&member.type.arrayCount==4&&member.type.rows==1&&member.type.columns==4&&member.arrayStride==16,"Real reflection preserves fixed array extent, vector shape and std140 stride");
        }else test.Check(false,"Array fixture exposes one active UBO with one named member");
        const auto keptArray=reflectedArray;
        test.Check(!Tools::SpirvReflection::Reflect(ArrayBlockFixture(true),reflectedArray,error)&&reflectedArray==keptArray,"Specialization ID zero is rejected and leaves previous reflected metadata intact");
        ShaderMetadata mixedInterface;const bool mixedReflected=Tools::SpirvReflection::Reflect(MixedInterfaceFixture(),mixedInterface,error);
        test.Check(mixedReflected&&mixedInterface.entries.size()==1&&mixedInterface.entries[0].outputs.size()==1
            &&mixedInterface.entries[0].outputs[0].location==0&&mixedInterface.entries[0].outputs[0].type.columns==4,
            "Builtin Position member does not hide an ordinary location sibling in the same interface block");
        auto metadata=LayoutFixture();test.Check(ValidateShaderMetadata(metadata,error),"array stride and vector columns preserve the actual ABI");
        auto invalid=metadata;invalid.entries[0].bindings[0].members[0].arrayStride=0;test.Check(!ValidateShaderMetadata(invalid,error),"array members require nonzero stride");
        invalid=metadata;invalid.entries[0].bindings[0].members[0].paddedSize=65;test.Check(!ValidateShaderMetadata(invalid,error),"buffer member cannot exceed reflected block size");
        invalid=metadata;invalid.entries[0].bindings[0].members.push_back(invalid.entries[0].bindings[0].members[0]);test.Check(!ValidateShaderMetadata(invalid,error),"duplicate or overlapping block members rejected");
        invalid=metadata;invalid.entries[0].bindings.push_back(invalid.entries[0].bindings[0]);test.Check(!ValidateShaderMetadata(invalid,error),"duplicate set/binding slot rejected");
        invalid=metadata;invalid.entries[0].inputs.push_back(invalid.entries[0].inputs[0]);test.Check(!ValidateShaderMetadata(invalid,error),"duplicate vertex locations rejected");
        invalid=metadata;invalid.entries[0].inputs[0].type.width=64;test.Check(!ValidateShaderMetadata(invalid,error),"unsupported 64-bit vertex input is not narrowed to float32");
        invalid=metadata;invalid.entries[0].pushConstantBytes=16;test.Check(!ValidateShaderMetadata(invalid,error),"push size cannot be declared without actual members");
        test.Check(Tools::SlangCompiler::CheckEntryReflection(R"({"entryPoints":[{"name":"main","stage":"compute"}]})",asset.metadata,error),"auxiliary Slang entry set matches final SPIR-V entry set");
        test.Check(!Tools::SlangCompiler::CheckEntryReflection(R"({"entryPoints":[{"name":"main","stage":"vertex"}]})",asset.metadata,error),"Slang/final SPIR-V stage disagreement rejects publication");
        test.Check(!Tools::SlangCompiler::CheckEntryReflection(R"({"entryPoints":[],"entryPoints":[]})",asset.metadata,error),"auxiliary reflection duplicate JSON keys rejected");
        std::vector<uint32_t> prepared{0xdeadbeef};test.Check(Tools::ShaderToolDetail::AddPositionInvariant(asset.spirv,prepared,error)&&prepared==asset.spirv,"non-vertex module remains identical before real validation");
        const auto previous=prepared;std::array<uint32_t,5> truncated{0x07230203,0x00010500,0,5,0};
        test.Check(!Tools::ShaderToolDetail::AddPositionInvariant(truncated,prepared,error)&&prepared==previous,"invariant preparation failure preserves output");
        std::vector<std::byte> payload;test.Check(ShaderLibrary::EncodePayload(asset,payload,error),"shader payload writes bounded little-endian schema");
        std::shared_ptr<const ShaderAsset> decoded;test.Check(ShaderLibrary::DecodePayload(payload,decoded,error)&&decoded&&decoded->spirv==asset.spirv&&decoded->metadata==asset.metadata&&decoded->variant==asset.variant,"shader payload round-trip preserves immutable code/interface/input key");
        const auto held=decoded;auto trailing=payload;trailing.push_back(std::byte{0});test.Check(!ShaderLibrary::DecodePayload(trailing,decoded,error)&&decoded==held,"trailing payload bytes fail while previous asset stays owned");
        auto cut=payload;if(!cut.empty())cut.pop_back();test.Check(!ShaderLibrary::DecodePayload(cut,decoded,error)&&decoded==held,"truncated payload fails without replacing previous asset");
        AssetId id;test.Check(ShaderLibrary::BuiltinId("Unlit.slang",id,error)&&id.ToString()=="5c35308e-1d0c-9d78-a968-b6dc14e5c915","builtin shader identity follows the frozen source-name namespace");
        auto saved=id;test.Check(!ShaderLibrary::BuiltinId("../Unlit.slang",id,error)&&id==saved,"invalid relative builtin path does not overwrite identity");
        AssetId other;test.Check(ShaderLibrary::BuiltinId("Other/Unlit.slang",other,error)&&other!=saved,"different source directories have different stable identities");
        std::vector<std::byte> cooked;test.Check(Tools::ShaderCooker::Cook(saved,asset,cooked,error),"shader cooker produces the shared asset container");
        AssetContainer container;test.Check(AssetContainerReader::Read(cooked,container,error)&&container.id==saved&&container.type.ToString()=="shader"&&container.version!=0&&container.payload==payload,"shader container exposes exact type, stable ID, content version and payload");
        if(!cooked.empty())cooked.back()^=std::byte{1};test.Check(!AssetContainerReader::Read(cooked,container,error),"corrupted cooked shader is rejected before decode");
        std::vector<std::string> invalidCommand{"--cook-shaders","--unknown","value"};const auto command=Tools::ToolCommandRunner::Run(invalidCommand);
        test.Check(command.exitCode==2&&!command.diagnostics.empty(),"tool argument errors return structured nonzero result without tools/devices");
        RHIShaderDesc noAsset;test.Check(!ValidateRHIShader(noAsset,error),"GPU shader creation requires an owned fixed asset version");
    }
}
