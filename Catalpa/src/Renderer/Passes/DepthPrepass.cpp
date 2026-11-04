#include "catpch.h"
#include "Renderer/Passes/DepthPrepass.h"
#include "Renderer/RenderGraph/RenderGraph.h"

namespace Catalpa {
    namespace {
        // Khronos unified1 grammar：opcode与decoration固定值，不依赖上游头或修改生成SPIR-V原件。
        constexpr uint32_t SpirvMagic = 0x07230203;
        constexpr uint16_t OpEntryPoint = 15, OpTypeFloat = 22, OpTypeVector = 23, OpTypeStruct = 30,
            OpTypePointer = 32, OpTypeFunction = 33, OpFunction = 54, OpFunctionEnd = 56, OpVariable = 59,
            OpDecorate = 71, OpMemberDecorate = 72;
        constexpr uint32_t VertexExecutionModel = 0, OutputStorageClass = 3,
            BlockDecoration = 2, BuiltInDecoration = 11, InvariantDecoration = 18, PositionBuiltIn = 0;
        struct SpirvType {
            uint16_t opcode = 0;
            uint32_t componentOrPointee = 0;
            uint32_t countOrStorage = 0;
            std::vector<uint32_t> members;
        };
        struct SpirvVariable {
            uint32_t type = 0, storage = 0;
        };
        struct SpirvPosition {
            uint32_t target = 0, member = 0;
            bool memberDecoration = false;
            constexpr bool operator==(const SpirvPosition&) const = default;
        };
        bool AnnotationOpcode(uint16_t opcode) noexcept
        {
            // Core annotations plus已定义的DecorateId/DecorateString/MemberDecorateString。
            return (opcode >= 71 && opcode <= 75) || opcode == 332 || opcode == 5632 || opcode == 5633;
        }
    }

    RHIStatus DepthPrepass::PrepareInvariantVertexShader(std::span<const uint32_t> source, std::vector<uint32_t>& output)
    {
        constexpr const char* operation = "DepthPrepass::PrepareInvariantVertexShader";
        const RHIStatus invalid{ .code = RHIErrorCode::InvalidDescription, .operation = operation };
        // 只接受当前SPIR-V 1.x原生字序；schema必须为0，不把任意字节串扫描成装饰。
        if (source.size() < 5 || source[0] != SpirvMagic || source[3] <= 1 || source[4] != 0
            || (source[1] & 0xff0000ffu) != 0 || ((source[1] >> 16) & 0xffu) != 1
            || ((source[1] >> 8) & 0xffu) > 6) return invalid;
        try {
            const auto idValid = [&](uint32_t id) { return id != 0 && id < source[3]; };
            std::map<uint32_t, SpirvType> types;
            std::map<uint32_t, SpirvVariable> variables;
            std::set<uint32_t> definitions, interfaces, blockStructs, functions;
            std::vector<SpirvPosition> positions, invariants;
            uint32_t entryFunction = 0;
            size_t annotationEnd = 0, firstType = 0;
            bool inFunction = false, functionSection = false, annotationsClosed = false;
            for (size_t offset = 5; offset < source.size();) {
                const uint32_t word = source[offset];
                const auto count = static_cast<uint16_t>(word >> 16);
                const auto opcode = static_cast<uint16_t>(word & 0xffffu);
                if (count == 0 || count > source.size() - offset) return invalid;
                const auto operands = source.subspan(offset, count);
                const bool annotation = AnnotationOpcode(opcode);
                if (annotation) {
                    if (annotationsClosed || firstType != 0 || inFunction) return invalid;
                    annotationEnd = offset + count;
                } else if (annotationEnd != 0) annotationsClosed = true;
                if (opcode == OpEntryPoint) {
                    // 单个Vertex入口；多入口或非Vertex不猜测应约束哪一套输出。
                    if (count < 4 || entryFunction != 0 || operands[1] != VertexExecutionModel || !idValid(operands[2])
                        || annotationEnd != 0 || firstType != 0 || inFunction) return invalid;
                    entryFunction = operands[2];
                    bool terminated = false;
                    size_t interfaceStart = 0;
                    for (size_t index = 3; index < count && !terminated; ++index) {
                        for (uint32_t byte = 0; byte < 4; ++byte) {
                            if (((operands[index] >> (byte * 8)) & 0xffu) == 0) {
                                // literal string末word的NUL后只允许padding；空入口名拒绝。
                                if ((index == 3 && byte == 0) || (byte < 3 && (operands[index] >> ((byte + 1) * 8)) != 0)) return invalid;
                                terminated = true;
                                interfaceStart = index + 1;
                                break;
                            }
                        }
                    }
                    if (!terminated) return invalid;
                    for (size_t index = interfaceStart; index < count; ++index)
                        if (!idValid(operands[index]) || !interfaces.insert(operands[index]).second) return invalid;
                } else if (opcode >= 19 && opcode <= 39) {
                    if (inFunction || functionSection || count < 2 || !idValid(operands[1]) || !definitions.insert(operands[1]).second) return invalid;
                    if (firstType == 0) firstType = offset;
                    // ForwardPointer不属于本步已支持的Slang VS类型形式，不做不完整推断。
                    if (opcode == 39) return invalid;
                    SpirvType type; type.opcode = opcode;
                    if ((opcode == 19 || opcode == 20) && count != 2) return invalid;
                    if (opcode == OpTypeFloat) {
                        if (count != 3) return invalid;
                        type.countOrStorage = operands[2];
                    } else if (opcode == OpTypeVector || opcode == OpTypePointer) {
                        if (count != 4) return invalid;
                        if (opcode == OpTypeVector) { type.componentOrPointee = operands[2]; type.countOrStorage = operands[3]; }
                        else { type.countOrStorage = operands[2]; type.componentOrPointee = operands[3]; }
                        if (!idValid(type.componentOrPointee)) return invalid;
                    } else if (opcode == OpTypeStruct) {
                        for (size_t member = 2; member < count; ++member) {
                            if (!idValid(operands[member])) return invalid;
                            type.members.push_back(operands[member]);
                        }
                    } else if (opcode == OpTypeFunction) {
                        if (count < 3) return invalid;
                        type.componentOrPointee = operands[2];
                        type.countOrStorage = count - 3;
                        for (size_t index = 2; index < count; ++index) if (!idValid(operands[index])) return invalid;
                    }
                    types.emplace(operands[1], std::move(type));
                } else if (opcode == OpVariable) {
                    if ((count != 4 && count != 5) || !idValid(operands[1]) || !idValid(operands[2])
                        || !definitions.insert(operands[2]).second || (count == 5 && !idValid(operands[4]))) return invalid;
                    const auto pointer = types.find(operands[1]);
                    if (pointer == types.end() || pointer->second.opcode != OpTypePointer
                        || pointer->second.countOrStorage != operands[3]) return invalid;
                    // Function变量只在函数内，其他StorageClass只在全局类型/变量节。
                    if ((operands[3] == 7) != inFunction || (!inFunction && functionSection)) return invalid;
                    variables.emplace(operands[2], SpirvVariable{ operands[1], operands[3] });
                } else if (opcode == OpFunction) {
                    if (count != 5 || inFunction || firstType == 0 || !idValid(operands[1]) || !idValid(operands[2])
                        || !idValid(operands[4]) || !definitions.insert(operands[2]).second) return invalid;
                    const auto functionType = types.find(operands[4]);
                    const auto returnType = types.find(operands[1]);
                    if (returnType == types.end() || functionType == types.end() || functionType->second.opcode != OpTypeFunction
                        || functionType->second.componentOrPointee != operands[1]
                        || (operands[2] == entryFunction && (returnType->second.opcode != 19 || functionType->second.countOrStorage != 0))) return invalid;
                    functions.insert(operands[2]);
                    functionSection = true;
                    inFunction = true;
                } else if (opcode == OpFunctionEnd) {
                    if (count != 1 || !inFunction) return invalid;
                    inFunction = false;
                } else if (opcode == OpDecorate) {
                    if (count < 3 || !idValid(operands[1])) return invalid;
                    if (operands[2] == BuiltInDecoration) {
                        if (count != 4) return invalid;
                        if (operands[3] == PositionBuiltIn) positions.push_back({operands[1],0,false});
                    } else if (operands[2] == InvariantDecoration) {
                        if (count != 3) return invalid;
                        invariants.push_back({operands[1],0,false});
                    } else if (operands[2] == BlockDecoration) {
                        if (count != 3 || !blockStructs.insert(operands[1]).second) return invalid;
                    }
                } else if (opcode == OpMemberDecorate) {
                    if (count < 4 || !idValid(operands[1])) return invalid;
                    if (operands[3] == BuiltInDecoration) {
                        if (count != 5) return invalid;
                        if (operands[4] == PositionBuiltIn) positions.push_back({operands[1],operands[2],true});
                    } else if (operands[3] == InvariantDecoration) {
                        if (count != 4) return invalid;
                        invariants.push_back({operands[1],operands[2],true});
                    }
                } else if (opcode == 332 || opcode == 5632 || opcode == 5633) {
                    const size_t decorationIndex = opcode == 5633 ? 3 : 2;
                    if (count < decorationIndex + 2 || !idValid(operands[1])) return invalid;
                    if (operands[decorationIndex] == BuiltInDecoration || operands[decorationIndex] == InvariantDecoration
                        || operands[decorationIndex] == BlockDecoration) return invalid;
                    if (opcode == 332) {
                        for (size_t index = 3; index < count; ++index) if (!idValid(operands[index])) return invalid;
                    } else {
                        bool terminated = false;
                        for (size_t index = decorationIndex + 1; index < count; ++index)
                            for (uint32_t byte = 0; byte < 4; ++byte)
                                if (((operands[index] >> (byte * 8)) & 0xffu) == 0) {
                                    if (index + 1 != count || (byte < 3 && (operands[index] >> ((byte + 1) * 8)) != 0)) return invalid;
                                    terminated = true;
                                    break;
                                }
                        if (!terminated) return invalid;
                    }
                } else if (opcode >= 73 && opcode <= 75) {
                    // 本步不解析group间接BuiltIn/Invariant，防把歧义装饰误当作已受检输出。
                    return invalid;
                }
                offset += count;
            }
            if (inFunction || entryFunction == 0 || !functions.contains(entryFunction) || firstType == 0
                || annotationEnd == 0 || annotationEnd > firstType || positions.size() != 1) return invalid;
            const auto position = positions.front();
            uint32_t valueType = 0;
            if (!position.memberDecoration) {
                const auto variable = variables.find(position.target);
                if (variable == variables.end() || variable->second.storage != OutputStorageClass || !interfaces.contains(position.target)) return invalid;
                const auto pointer = types.find(variable->second.type);
                if (pointer == types.end() || pointer->second.opcode != OpTypePointer || pointer->second.countOrStorage != OutputStorageClass) return invalid;
                valueType = pointer->second.componentOrPointee;
            } else {
                const auto structure = types.find(position.target);
                if (structure == types.end() || structure->second.opcode != OpTypeStruct || !blockStructs.contains(position.target)
                    || position.member >= structure->second.members.size()) return invalid;
                size_t matchingOutputs = 0;
                for (const auto& [id, variable] : variables) {
                    if (!interfaces.contains(id)) continue;
                    const auto pointer = types.find(variable.type);
                    if (pointer != types.end() && pointer->second.opcode == OpTypePointer
                        && pointer->second.componentOrPointee == position.target) {
                        if (variable.storage != OutputStorageClass || pointer->second.countOrStorage != OutputStorageClass) return invalid;
                        ++matchingOutputs;
                    }
                }
                if (matchingOutputs != 1) return invalid;
                valueType = structure->second.members[position.member];
            }
            const auto vector = types.find(valueType);
            if (vector == types.end() || vector->second.opcode != OpTypeVector || vector->second.countOrStorage != 4) return invalid;
            const auto scalar = types.find(vector->second.componentOrPointee);
            if (scalar == types.end() || scalar->second.opcode != OpTypeFloat || scalar->second.countOrStorage != 32) return invalid;
            size_t invariantCount = 0;
            for (const auto& invariant : invariants) if (invariant == position) ++invariantCount;
            if (invariantCount > 1) return invalid;
            std::vector<uint32_t> candidate;
            const size_t extra = position.memberDecoration ? 4 : 3;
            if (source.size() > candidate.max_size() - extra) return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
            candidate.assign(source.begin(), source.end());
            if (invariantCount == 0) {
                std::array<uint32_t, 4> decoration{};
                decoration[0] = (static_cast<uint32_t>(extra) << 16) | (position.memberDecoration ? OpMemberDecorate : OpDecorate);
                decoration[1] = position.target;
                if (position.memberDecoration) { decoration[2] = position.member; decoration[3] = InvariantDecoration; }
                else decoration[2] = InvariantDecoration;
                candidate.insert(candidate.begin() + static_cast<std::ptrdiff_t>(annotationEnd), decoration.begin(), decoration.begin() + extra);
            }
            output = std::move(candidate);
            return {};
        } catch (const std::bad_alloc&) { return { .code = RHIErrorCode::OutOfMemory, .operation = operation }; }
    }

    RHIStatus DepthPrepass::Add(RenderGraph& graph, std::span<const RGIndexedDrawDesc> draws, float clearDepth)
    {
        constexpr const char* operation = "DepthPrepass::Add";
        if (!std::isfinite(clearDepth) || clearDepth < 0.0f || clearDepth > 1.0f)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        // 先检查批量目标/范围一致性，避免把不同视图的draw误当一段共享深度。
        for (const auto& draw : draws)
            if (!draw.depth.HasValue() || draw.depth != draws.front().depth
                || draw.indexCount == 0 || draw.instanceCount == 0)
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        bool first = true;
        for (const auto& source : draws) {
            auto draw = source;
            draw.color = {};
            draw.depthLoadOp = first ? RHILoadOp::Clear : RHILoadOp::Load;
            draw.depthStoreOp = RHIStoreOp::Store;
            draw.clearDepth = clearDepth;
            draw.depthReadOnly = false;
            if (const auto status = graph.AddDrawPass("Depth prepass", draw); !status.IsSuccess()) return status;
            first = false;
        }
        return {};
    }
}
