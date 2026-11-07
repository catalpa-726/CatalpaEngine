#include "catpch.h"
#include "ShaderCompiler/SpirvReflection.h"
#include "Assets/AssetContainerReader.h"
#include <spirv_reflect.h>
namespace Catalpa::Tools {
    namespace {
        bool Failure(std::string& e,std::string_view s) { e.assign(s);return false; }
        bool ArrayCount(const SpvReflectArrayTraits& array,uint32_t& output,std::string& error) {
            // Locked SPIRV-Reflect ParseType uses UINT32_MAX for an ordinary constant dimension.
            // A specialization ID of zero is valid; zero is not the absent-ID sentinel.
            if(array.dims_count>1)return Failure(error,"multidimensional shader arrays need a newer metadata schema");
            uint32_t count=1;
            for(uint32_t i=0;i<array.dims_count;++i){
                if (array.dims[i] == SPV_REFLECT_ARRAY_DIM_RUNTIME) {
                    output = 0;
                    return true;
                }
                if (array.spec_constant_op_ids[i] != UINT32_MAX || array.dims[i] > 1048576)
                    return Failure(error,"specialized/oversized arrays are not supported by shader metadata");
                count=array.dims[i];
            }
            output=count;return true;
        }
        bool Shape(const SpvReflectTypeDescription* t,const SpvReflectNumericTraits& n,const SpvReflectArrayTraits& a,ShaderTypeShape& out,std::string& error) {
            if(!t)return Failure(error,"reflection lacks type description");
            ShaderTypeShape s;
            if(t->type_flags&SPV_REFLECT_TYPE_FLAG_STRUCT){s.scalar=ShaderScalarKind::Structure;s.width=0;}
            else if(t->type_flags&SPV_REFLECT_TYPE_FLAG_FLOAT)s.scalar=ShaderScalarKind::Float;
            else if(t->type_flags&SPV_REFLECT_TYPE_FLAG_INT)s.scalar=n.scalar.signedness?ShaderScalarKind::SignedInteger:ShaderScalarKind::UnsignedInteger;
            else if(t->type_flags&SPV_REFLECT_TYPE_FLAG_BOOL)s.scalar=ShaderScalarKind::Boolean;
            else return Failure(error,"unsupported reflected scalar type");
            if(s.scalar!=ShaderScalarKind::Structure)s.width=s.scalar==ShaderScalarKind::Boolean?32:n.scalar.width;
            if(n.matrix.column_count){s.columns=n.matrix.column_count;s.rows=n.matrix.row_count;}
            else if(n.vector.component_count)s.columns=n.vector.component_count;
            if(!ArrayCount(a,s.arrayCount,error))return false;
            out=s;return true;
        }
        bool Members(const SpvReflectBlockVariable& block, std::vector<ShaderBlockMember>& output,
                     size_t depth, size_t& budget, std::string& error) {
            if (depth > 16 || block.member_count > budget)
                return Failure(error, "reflection member/depth budget exceeded");
            budget -= block.member_count;

            for (uint32_t i = 0; i < block.member_count; ++i) {
                const auto& m = block.members[i];
                if (!m.name || !*m.name)
                    return Failure(error, "SPIR-V block member names must be preserved");

                // 当前 Reflect 只向成员的 array 字段复制定长数组信息；运行时数组要从类型读取。
                // 只认显式 OpTypeRuntimeArray，不能把尺寸为零的普通成员猜成可变长数组。
                const bool runtimeArray = m.type_description &&
                                          m.type_description->op == SpvOpTypeRuntimeArray;
                const auto& array = runtimeArray ? m.type_description->traits.array : m.array;
                if (runtimeArray &&
                    (array.dims_count != 1 || array.dims[0] != SPV_REFLECT_ARRAY_DIM_RUNTIME))
                    return Failure(error, "unsupported runtime shader array dimensions");

                ShaderBlockMember v;
                v.name = m.name;
                v.offset = m.offset;
                v.size = m.size;
                v.paddedSize = m.padded_size;
                v.arrayStride = array.stride;
                v.matrixStride = m.numeric.matrix.stride;
                v.rowMajor = (m.decoration_flags & SPV_REFLECT_DECORATION_ROW_MAJOR) != 0;
                if (!Shape(m.type_description, m.numeric, array, v.type, error) ||
                    !Members(m, v.members, depth + 1, budget, error))
                    return false;
                if ((v.type.arrayCount == 0) != runtimeArray)
                    return Failure(error, "shader array length does not match its SPIR-V type");

                // Reflect 对结构体数组会给出单个元素的尺寸。schema3 的尾数组没有固定总尺寸；
                // 保留元素成员和 stride，交给布局校验检查元素范围、重叠和合法的末尾位置。
                if (runtimeArray) {
                    v.size = 0;
                    v.paddedSize = 0;
                }
                output.push_back(std::move(v));
            }
            std::sort(output.begin(), output.end(), [](const auto& a, const auto& b) {
                return a.offset < b.offset;
            });
            return true;
        }

        bool StorageBlockSize(const std::vector<ShaderBlockMember>& members, uint32_t& output,
                              std::string& error) {
            // Reflect 将所有 StorageBuffer 的块尺寸置零，包括有固定前缀或完全定长的块。
            // 重建最小静态范围；尾数组只贡献起始偏移，不虚构运行时元素数量。
            uint64_t size = 0;
            for (const auto& member : members)
                size = std::max(size, uint64_t(member.offset) + member.paddedSize);
            if (size > UINT32_MAX)
                return Failure(error, "reflected storage buffer size overflows metadata");
            output = uint32_t(size);
            return true;
        }
        bool Interface(const SpvReflectInterfaceVariable& x,std::vector<ShaderInterfaceVariable>& output,std::string& error) {
            // Upstream propagates BUILT_IN to a parent containing any builtin member; keep ordinary siblings.
            if(x.member_count){for(uint32_t i=0;i<x.member_count;++i)if(!Interface(x.members[i],output,error))return false;return true;}
            if(x.decoration_flags&SPV_REFLECT_DECORATION_BUILT_IN)return true;
            if(x.component!=0&&x.component!=UINT32_MAX)return Failure(error,"component-decorated interface requires unsupported packing");
            ShaderInterfaceVariable v;v.location=x.location;v.name=x.name&&*x.name?x.name:"location"+std::to_string(x.location);
            if(!Shape(x.type_description,x.numeric,x.array,v.type,error))return false;output.push_back(std::move(v));return true;
        }
    }
    bool SpirvReflection::Reflect(std::span<const uint32_t> words,ShaderMetadata& output,std::string& error) {
        try {
            if(words.size()<5||words.size()>4*1024*1024)return Failure(error,"invalid reflected SPIR-V byte length");
            // ParseType's array sentinel is also absent for an undecorated SpecConstantOp.
            // Schema1 has no specialization-map contract, so never turn such defaults into fixed layout facts.
            for(size_t offset=5;offset<words.size();){const uint32_t count=words[offset]>>16,opcode=words[offset]&65535u;
                if(count==0||count>words.size()-offset)return Failure(error,"truncated SPIR-V before reflection");
                if(opcode>=SpvOpSpecConstantTrue&&opcode<=SpvOpSpecConstantOp)return Failure(error,"shader schema1 does not support specialization constants");
                offset+=count;
            }
            SpvReflectShaderModule module{};
            if(spvReflectCreateShaderModule(words.size_bytes(),words.data(),&module)!=SPV_REFLECT_RESULT_SUCCESS)return Failure(error,"SPIRV-Reflect rejected validated module");
            struct Owner{SpvReflectShaderModule* m;~Owner(){spvReflectDestroyShaderModule(m);}} owner{&module};
            ShaderMetadata candidate;candidate.spirvVersion=words[1];
            if(!ComputeAssetContentHash(std::as_bytes(words),candidate.bytecodeSha256,error))return false;
            if(module.entry_point_count==0||module.entry_point_count>64)return Failure(error,"unsupported reflected entry count");
            size_t budget=4096;std::set<std::string> entryNames;
            for(uint32_t i=0;i<module.entry_point_count;++i){const auto& source=module.entry_points[i];ShaderEntryPointMetadata entry;
                if(!source.name||!*source.name)return Failure(error,"unnamed shader entry");entry.name=source.name;
                if(!entryNames.insert(entry.name).second)return Failure(error,"schema1 requires unique entry names across stages for unambiguous reflection");
                switch(source.shader_stage){case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT:entry.stage=ShaderStage::Vertex;break;case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT:entry.stage=ShaderStage::Fragment;break;case SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT:entry.stage=ShaderStage::Compute;break;default:return Failure(error,"unsupported shader stage in schema1");}
                uint32_t count=0;
                if(spvReflectEnumerateEntryPointDescriptorBindings(&module,source.name,&count,nullptr)!=SPV_REFLECT_RESULT_SUCCESS||count>256)return Failure(error,"cannot enumerate shader bindings");
                std::vector<SpvReflectDescriptorBinding*> bindings(count);
                if(spvReflectEnumerateEntryPointDescriptorBindings(&module,source.name,&count,bindings.data())!=SPV_REFLECT_RESULT_SUCCESS)return Failure(error,"cannot read shader bindings");
                for(const auto* b:bindings){ShaderBinding v;v.set=b->set;v.binding=b->binding;v.count=b->count;
                    uint32_t descriptorCount=0;
                    if(!b->type_description||!ArrayCount(b->type_description->traits.array,descriptorCount,error)||descriptorCount!=b->count)
                        return Failure(error,"unsupported or inconsistent descriptor array dimensions");
                    if(!b->name||!*b->name)return Failure(error,"SPIR-V resource names must be preserved");v.name=b->name;
                    switch(b->descriptor_type){
                    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:v.kind=ShaderResourceKind::UniformBuffer;break;
                    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:v.kind=ShaderResourceKind::StorageBuffer;break;
                    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:v.kind=ShaderResourceKind::SampledImage;break;
                    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:v.kind=ShaderResourceKind::StorageImage;break;
                    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:v.kind=ShaderResourceKind::Sampler;break;
                    case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:v.kind=ShaderResourceKind::CombinedImageSampler;break;
                    default:return Failure(error,"unsupported reflected resource kind");}
                    if (v.kind == ShaderResourceKind::UniformBuffer || v.kind == ShaderResourceKind::StorageBuffer) {
                        v.blockSize = b->block.padded_size;
                        if (!Members(b->block, v.members, 0, budget, error))
                            return false;
                        if (v.kind == ShaderResourceKind::StorageBuffer &&
                            !StorageBlockSize(v.members, v.blockSize, error))
                            return false;
                    }
                    else if(v.kind!=ShaderResourceKind::Sampler) {
                        if (b->image.arrayed || b->image.ms || (b->image.dim != SpvDim2D && b->image.dim != SpvDimCube))
                            return Failure(error, "Schema2 supports non-arrayed, non-MS 2D or Cube images only");
                        v.imageDimension = b->image.dim == SpvDimCube ? ShaderImageDimension::Cube : ShaderImageDimension::Texture2D;
                    }
                    entry.bindings.push_back(std::move(v));
                }
                std::sort(entry.bindings.begin(),entry.bindings.end(),[](const auto&a,const auto&b){return std::pair(a.set,a.binding)<std::pair(b.set,b.binding);});
                for(uint32_t n=0;n<source.input_variable_count;++n)if(!Interface(*source.input_variables[n],entry.inputs,error))return false;
                for(uint32_t n=0;n<source.output_variable_count;++n)if(!Interface(*source.output_variables[n],entry.outputs,error))return false;
                auto sortIO=[](auto& v){std::sort(v.begin(),v.end(),[](const auto&a,const auto&b){return a.location<b.location;});};sortIO(entry.inputs);sortIO(entry.outputs);
                count=0;if(spvReflectEnumerateEntryPointPushConstantBlocks(&module,source.name,&count,nullptr)!=SPV_REFLECT_RESULT_SUCCESS||count>1)return Failure(error,"unsupported push constant block count");
                if(count){SpvReflectBlockVariable* block=nullptr;if(spvReflectEnumerateEntryPointPushConstantBlocks(&module,source.name,&count,&block)!=SPV_REFLECT_RESULT_SUCCESS||!block||block->offset!=0)return Failure(error,"push constants must start at offset zero");entry.pushConstantBytes=block->padded_size;if(!Members(*block,entry.pushConstantMembers,0,budget,error))return false;}
                candidate.entries.push_back(std::move(entry));
            }
            std::sort(candidate.entries.begin(),candidate.entries.end(),[](const auto&a,const auto&b){return std::pair(a.stage,a.name)<std::pair(b.stage,b.name);});
            if(!ValidateShaderMetadata(candidate,error))return false;output=std::move(candidate);error.clear();return true;
        }catch(const std::exception& e){return Failure(error,e.what());}
    }
}
