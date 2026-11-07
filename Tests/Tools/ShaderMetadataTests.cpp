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
        enum class StorageLayout { RuntimeWords, PrefixTail, StructureTail, FixedWord };

        // 直接反射真实 OpTypeRuntimeArray，而非手工设置 metadata 绕过第三方适配层。
        // SSBO 使用 StorageBuffer + Block，main 通过 AccessChain/Load 引用实际资源。
        std::vector<uint32_t> StorageBlockFixture(StorageLayout layout) {
            std::vector<uint32_t> words{0x07230203, 0x00010500, 0, 17, 0};
            const auto emit = [&](uint32_t opcode, std::initializer_list<uint32_t> operands) {
                words.push_back((uint32_t(operands.size() + 1) << 16) | opcode);
                words.insert(words.end(), operands);
            };
            const auto name = [&](uint32_t opcode, std::initializer_list<uint32_t> prefix,
                                  std::string_view text) {
                const size_t count = (text.size() + 1 + 3) / 4;
                words.push_back((uint32_t(1 + prefix.size() + count) << 16) | opcode);
                words.insert(words.end(), prefix);
                for (size_t index = 0; index < count; ++index) {
                    uint32_t word = 0;
                    for (size_t byte = 0; byte < 4 && index * 4 + byte < text.size(); ++byte)
                        word |= uint32_t(uint8_t(text[index * 4 + byte])) << (byte * 8);
                    words.push_back(word);
                }
            };
            const bool runtime = layout != StorageLayout::FixedWord;
            const bool prefix = layout == StorageLayout::PrefixTail;
            const bool structure = layout == StorageLayout::StructureTail;
            emit(17, {1});                       // Shader
            emit(14, {0, 1});                    // Logical, GLSL450
            emit(15, {5, 3, 0x6e69616d, 0, 11}); // Compute main; SSBO interface
            emit(16, {3, 17, 1, 1, 1});          // LocalSize 1,1,1
            name(5, {11}, "storageData");
            if (prefix) {
                name(6, {9, 0}, "count");
                name(6, {9, 1}, "values");
            } else
                name(6, {9, 0}, runtime ? "values" : "value");
            if (structure) {
                name(6, {7, 0}, "joint");
                name(6, {7, 1}, "weight");
            }
            emit(71, {9, 2}); // Block
            emit(72, {9, 0, 35, 0});
            if (prefix)
                emit(72, {9, 1, 35, 16});
            emit(71, {11, 34, 0});
            emit(71, {11, 33, 3});
            if (runtime)
                emit(71, {8, 6, structure ? 8u : 4u}); // ArrayStride
            if (structure) {
                emit(72, {7, 0, 35, 0});
                emit(72, {7, 1, 35, 4});
            }
            emit(19, {1});
            emit(33, {2, 1});
            emit(21, {5, 32, 0});
            emit(22, {6, 32});
            if (structure)
                emit(30, {7, 5, 6});
            if (runtime)
                emit(29, {8, structure ? 7u : 5u});
            if (prefix)
                emit(30, {9, 5, 8});
            else
                emit(30, {9, runtime ? 8u : 5u});
            emit(32, {10, 12, 9});
            emit(59, {10, 11, 12});
            emit(32, {12, 12, 5});
            emit(43, {5, 13, 0});
            emit(43, {5, 16, 1});
            emit(54, {1, 3, 0, 2});
            emit(248, {4});
            if (structure)
                emit(65, {12, 14, 11, 13, 13, 13});
            else if (prefix)
                emit(65, {12, 14, 11, 16, 13});
            else if (runtime)
                emit(65, {12, 14, 11, 13, 13});
            else
                emit(65, {12, 14, 11, 13});
            emit(61, {5, 15, 14});
            emit(253, {});
            emit(56, {});
            return words;
        }
        void RunBindingDiagnosticTests(TestContext& test, const ShaderMetadata& runtimeMetadata) {
            std::string error;
            auto candidate = runtimeMetadata;
            auto& bindings = candidate.entries[0].bindings;
            bindings[0].name = "commands_uint";
            bindings[0].binding = 2;

            // 同一槽的 uint/int 存储视图仍然冲突；不能为兼容编译器输出而静默合并布局。
            auto signedView = bindings[0];
            signedView.name = "commands_int";
            signedView.members[0].type.scalar = ShaderScalarKind::SignedInteger;
            bindings.push_back(signedView);
            test.Check(!ValidateShaderMetadata(candidate, error) &&
                           error.find("duplicate shader binding slot") != std::string::npos &&
                           error.find("entry=main") != std::string::npos &&
                           error.find("name=commands_int") != std::string::npos &&
                           error.find("set=0, binding=2, count=1") != std::string::npos &&
                           error.find("kind=" + std::to_string(uint32_t(ShaderResourceKind::StorageBuffer))) !=
                               std::string::npos &&
                           error.find("existing resource=commands_uint") != std::string::npos,
                       "Conflicting typed SSBO views report the entry, slot and both resource names");

            bindings[1].binding = 3;
            test.Check(ValidateShaderMetadata(candidate, error) && error.empty(),
                       "Different typed buffers remain valid when assigned distinct bindings");
            bindings[1].binding = 2;
            bindings[1].set = 1;
            test.Check(ValidateShaderMetadata(candidate, error) && error.empty(),
                       "Binding numbers are scoped by descriptor set, not globally deduplicated");

            // 非法字段与重复槽分别诊断，保留具体字段及原始数值，便于定位 Cook 错误。
            const auto invalidField = [&](const char* field, auto change) {
                auto invalid = runtimeMetadata;
                auto& binding = invalid.entries[0].bindings[0];
                binding.name = "commands";
                binding.binding = 2;
                change(binding);
                const bool accepted = ValidateShaderMetadata(invalid, error);
                test.Check(!accepted &&
                               error.find("invalid shader binding field " + std::string(field) + ":") !=
                                   std::string::npos &&
                               error.find("entry=main") != std::string::npos &&
                               error.find("name=") != std::string::npos &&
                               error.find("set=" + std::to_string(binding.set)) != std::string::npos &&
                               error.find("binding=" + std::to_string(binding.binding)) != std::string::npos &&
                               error.find("count=" + std::to_string(binding.count)) != std::string::npos &&
                               error.find("kind=" + std::to_string(uint32_t(binding.kind))) != std::string::npos,
                           "Invalid binding fields report their cause and complete binding coordinates");
            };
            invalidField("name", [](auto& binding) { binding.name.clear(); });
            invalidField("set", [](auto& binding) { binding.set = 32; });
            invalidField("binding", [](auto& binding) { binding.binding = 65536; });
            invalidField("count", [](auto& binding) { binding.count = 0; });
            invalidField("count", [](auto& binding) { binding.count = 65537; });
            invalidField("kind", [](auto& binding) { binding.kind = ShaderResourceKind(UINT32_MAX); });
        }

        void RunStorageBufferReflectionTests(TestContext& test, const ShaderAsset& asset) {
            std::string error;
            ShaderMetadata runtimeMetadata, structureMetadata;
            for (const auto layout : {StorageLayout::RuntimeWords, StorageLayout::PrefixTail,
                                      StorageLayout::StructureTail, StorageLayout::FixedWord}) {
                ShaderMetadata reflected;
                const bool accepted =
                    Tools::SpirvReflection::Reflect(StorageBlockFixture(layout), reflected, error);
                test.Check(accepted,
                           "Real storage-buffer reflection accepts fixed and runtime-array layouts");
                if (!accepted || reflected.entries.size() != 1 || reflected.entries[0].bindings.size() != 1) {
                    test.Check(false, "Storage fixture exposes one active named SSBO");
                    continue;
                }
                const auto& block = reflected.entries[0].bindings[0];
                const bool prefix = layout == StorageLayout::PrefixTail;
                const bool runtime = layout != StorageLayout::FixedWord;
                if (block.members.size() != (prefix ? 2u : 1u)) {
                    test.Check(false, "Storage reflection preserves the declared prefix and tail members");
                    continue;
                }
                const auto& member = block.members.back();
                test.Check(block.kind == ShaderResourceKind::StorageBuffer && block.binding == 3 &&
                               block.blockSize == (prefix    ? 16u
                                                   : runtime ? 0u
                                                             : 4u),
                           "SSBO block size records only the fixed prefix, never an invented runtime length");
                if (runtime) {
                    test.Check(member.type.arrayCount == 0 && member.size == 0 && member.paddedSize == 0 &&
                                   member.arrayStride == (layout == StorageLayout::StructureTail ? 8u : 4u) &&
                                   member.offset == (prefix ? 16u : 0u),
                               "Runtime-array shape and stride come from reflected type traits");
                    if (layout == StorageLayout::StructureTail) {
                        test.Check(
                            member.type.scalar == ShaderScalarKind::Structure && member.members.size() == 2 &&
                                member.members[0].name == "joint" && member.members[0].offset == 0 &&
                                member.members[1].name == "weight" && member.members[1].offset == 4,
                            "A runtime structure array retains its element members within ArrayStride");
                        structureMetadata = reflected;
                    } else if (layout == StorageLayout::RuntimeWords)
                        runtimeMetadata = reflected;
                } else
                    test.Check(member.type.arrayCount == 1 && member.size == 4 && member.paddedSize == 4,
                               "Fixed SSBO members retain their finite reflected extent");
            }
            if (!runtimeMetadata.entries.empty()) {
                RunBindingDiagnosticTests(test, runtimeMetadata);
                const auto invalidRuntime = [&](auto change, const char* description) {
                    auto candidate = runtimeMetadata;
                    change(candidate);
                    test.Check(!ValidateShaderMetadata(candidate, error), description);
                };
                for (uint32_t schema : {1u, 2u})
                    invalidRuntime(
                        [&](auto& value) {
                            value.schemaVersion = schema;
                        },
                        "Runtime storage arrays cannot enter legacy metadata schemas");
                invalidRuntime(
                    [](auto& value) {
                        value.entries[0].bindings[0].kind = ShaderResourceKind::UniformBuffer;
                    },
                    "Runtime arrays cannot be reinterpreted as UBO members");
                invalidRuntime(
                    [](auto& value) {
                        auto& entry = value.entries[0];
                        entry.pushConstantMembers = entry.bindings[0].members;
                        entry.pushConstantBytes = 4;
                        entry.bindings.clear();
                    },
                    "Push constants cannot contain a runtime-sized tail");
                invalidRuntime(
                    [](auto& value) {
                        auto& entry = value.entries[0];
                        entry.inputs.push_back({"runtimeInput", 0, entry.bindings[0].members[0].type});
                    },
                    "Shader interface variables cannot expose runtime arrays");
                invalidRuntime(
                    [](auto& value) {
                        value.entries[0].bindings[0].count = 0;
                    },
                    "Runtime buffer contents do not enable unbounded descriptor arrays");
                for (uint32_t stride : {0u, 3u})
                    invalidRuntime(
                        [&](auto& value) {
                            value.entries[0].bindings[0].members[0].arrayStride = stride;
                        },
                        "Runtime scalar array stride must contain a complete element");
                invalidRuntime(
                    [](auto& value) {
                        auto& tail = value.entries[0].bindings[0].members[0];
                        tail.size = tail.paddedSize = 4;
                    },
                    "Canonical runtime metadata cannot pretend to have one fixed element");
                invalidRuntime(
                    [](auto& value) {
                        auto& member = value.entries[0].bindings[0].members[0];
                        member.type.arrayCount = 1;
                        member.arrayStride = 0;
                    },
                    "A zero-sized ordinary member is not silently accepted as a runtime array");
                invalidRuntime(
                    [](auto& value) {
                        value.entries[0].bindings[0].members[0].offset = 4;
                    },
                    "Runtime tail offset cannot exceed the recorded static prefix");
                invalidRuntime(
                    [](auto& value) {
                        auto& block = value.entries[0].bindings[0];
                        ShaderBlockMember after;
                        after.name = "after";
                        after.offset = 4;
                        after.size = after.paddedSize = 4;
                        block.members.push_back(after);
                        block.blockSize = 8;
                    },
                    "A runtime tail must remain the final block member");
                invalidRuntime(
                    [](auto& value) {
                        auto& block = value.entries[0].bindings[0];
                        auto second = block.members[0];
                        second.name = "secondTail";
                        second.offset = 4;
                        block.members.push_back(second);
                        block.blockSize = 4;
                    },
                    "One storage block cannot contain two runtime arrays");
                invalidRuntime(
                    [](auto& value) {
                        auto& block = value.entries[0].bindings[0];
                        ShaderBlockMember prefix;
                        prefix.name = "prefix";
                        prefix.size = prefix.paddedSize = 4;
                        block.members[0].offset = 2;
                        block.members.insert(block.members.begin(), prefix);
                        block.blockSize = 4;
                    },
                    "Runtime tail cannot overlap a preceding fixed member");
                auto badStride = StorageBlockFixture(StorageLayout::RuntimeWords);
                for (size_t offset = 5; offset < badStride.size();) {
                    const uint32_t count = badStride[offset] >> 16;
                    if ((badStride[offset] & 65535u) == 71 && count == 4 && badStride[offset + 1] == 8 &&
                        badStride[offset + 2] == 6)
                        badStride[offset + 3] = 0;
                    offset += count;
                }
                auto retained = runtimeMetadata;
                test.Check(
                    !Tools::SpirvReflection::Reflect(badStride, retained, error) &&
                        retained == runtimeMetadata,
                    "Malformed reflected runtime stride rejects publication and preserves prior metadata");
                auto runtimeAsset = asset;
                runtimeAsset.spirv = StorageBlockFixture(StorageLayout::RuntimeWords);
                runtimeAsset.metadata = runtimeMetadata;
                std::vector<std::byte> runtimePayload;
                std::shared_ptr<const ShaderAsset> runtimeDecoded;
                test.Check(
                    ShaderLibrary::EncodePayload(runtimeAsset, runtimePayload, error) &&
                        ShaderLibrary::DecodePayload(runtimePayload, runtimeDecoded, error) &&
                        runtimeDecoded && runtimeDecoded->metadata == runtimeMetadata,
                    "Schema3 shader payload roundtrip preserves the runtime tail without a fake length");
            }
            if (!structureMetadata.entries.empty()) {
                auto tooSmall = structureMetadata;
                tooSmall.entries[0].bindings[0].members[0].arrayStride = 4;
                test.Check(!ValidateShaderMetadata(tooSmall, error),
                           "Runtime structure elements cannot extend beyond ArrayStride");
            }
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
        // 即使字节码本身和哈希有效，Cook/运行时也必须在提交给驱动前拒绝未启用的能力。
        auto needsDrawParameters = asset;
        needsDrawParameters.spirv.insert(needsDrawParameters.spirv.begin() + 5, { (2u << 16) | 17u, 4427u });
        const bool hashed = ComputeAssetContentHash(std::as_bytes(std::span(needsDrawParameters.spirv)),
            needsDrawParameters.metadata.bytecodeSha256, error);
        test.Check(hashed && !ValidateShaderAsset(needsDrawParameters, error)
            && error.find("DrawParameters") != std::string::npos, "Reject unsupported DrawParameters before native shader creation");
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
        RunStorageBufferReflectionTests(test, asset);
        auto metadata=LayoutFixture();test.Check(ValidateShaderMetadata(metadata,error),"array stride and vector columns preserve the actual ABI");
        auto cubeMetadata = metadata;
        ShaderBinding cubeBinding;
        cubeBinding.binding = 13; cubeBinding.kind = ShaderResourceKind::SampledImage;
        cubeBinding.name = "irradianceTexture"; cubeBinding.imageDimension = ShaderImageDimension::Cube;
        cubeMetadata.entries.front().bindings.push_back(cubeBinding);
        test.Check(ValidateShaderMetadata(cubeMetadata, error), "Schema2 explicitly represents a Cube sampled image");
        cubeMetadata.schemaVersion = 1;
        test.Check(!ValidateShaderMetadata(cubeMetadata, error), "Legacy schema cannot silently reinterpret 2D images as Cube");
        cubeMetadata.schemaVersion = 2;
        cubeMetadata.entries.front().bindings.back().imageDimension = ShaderImageDimension::None;
        test.Check(!ValidateShaderMetadata(cubeMetadata, error), "Image resources require a concrete recorded dimension");
        cubeMetadata = metadata;
        cubeMetadata.entries.front().bindings.front().imageDimension = ShaderImageDimension::Cube;
        test.Check(!ValidateShaderMetadata(cubeMetadata, error), "A uniform buffer cannot carry an image dimension");
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
        test.Check(!payload.empty() && payload.front() == std::byte{2}, "New shader payloads identify the explicit image-dimension protocol");
        auto legacyAsset = asset;
        legacyAsset.metadata.schemaVersion = 1;
        std::vector<std::byte> legacyPayload;
        const bool legacyEncoded = ShaderLibrary::EncodePayload(legacyAsset, legacyPayload, error);
        // 无descriptor的compute夹具在两个payload协议中只有首个版本号不同。
        if (legacyEncoded && !legacyPayload.empty()) legacyPayload.front() = std::byte{1};
        std::shared_ptr<const ShaderAsset> legacyDecoded;
        test.Check(legacyEncoded && ShaderLibrary::DecodePayload(legacyPayload, legacyDecoded, error) &&
            legacyDecoded && legacyDecoded->metadata.schemaVersion == 1,
            "Legacy shader payload1 remains readable without inventing Cube dimensions");
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
