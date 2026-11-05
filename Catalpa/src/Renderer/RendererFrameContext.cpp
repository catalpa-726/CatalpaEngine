#include "catpch.h"
#include "Renderer/RendererFrameContext.h"
#include "RHI/RHIDevice.h"
#include "Camera/CameraProjection.h"
#include "Shaders/ShaderMetadata.h"
#include <nlohmann/json.hpp>

namespace Catalpa {
    namespace {
        constexpr const char* PackingOperation = "BuildFrameUniformData";
        // 只有声明过的当前格式和有限线性清屏颜色才可进入GPU路径，不静默选择其他格式。
        bool ValidProfile(const RendererProfile& profile) noexcept
        {
            if (profile.depthFormat != PixelFormat::D32_Float) return false;
            for (size_t channel = 0; channel < 4; ++channel)
                if (!std::isfinite(profile.clearColor[channel]) || profile.clearColor[channel] < 0.0f
                    || (channel == 3 && profile.clearColor[channel] > 1.0f)) return false;
            return true;
        }
        bool FloatValue(double input, float& output) noexcept
        {
            if (!std::isfinite(input) || std::abs(input) > std::numeric_limits<float>::max()) return false;
            output = static_cast<float>(input);
            return std::isfinite(output) && (input == 0.0 || output != 0.0f);
        }
        bool Align(uint64_t value, uint64_t alignment, uint64_t& output) noexcept
        {
            if (alignment == 0 || (alignment & (alignment - 1)) != 0
                || value > std::numeric_limits<uint64_t>::max() - (alignment - 1)) return false;
            output = (value + alignment - 1) & ~(alignment - 1);
            return true;
        }
    }

    bool ValidateFrameShaderInterface(const ShaderMetadata& metadata, std::string_view vertexEntry,
        std::string_view fragmentEntry, std::string& error)
    {
        try {
            if (!ValidateShaderMetadata(metadata, error)) return false;
            struct ExpectedMember {
                std::string_view name;
                uint32_t offset, vectors;
                ShaderScalarKind scalar = ShaderScalarKind::Float;
            };
            const auto checkNumber = [&](std::string_view path, std::string_view field, uint32_t actual, uint32_t expected) {
                if (actual == expected) return true;
                error = std::string(path) + "/" + std::string(field) + ": expected " + std::to_string(expected) + ", reflected " + std::to_string(actual);
                return false;
            };
            const auto checkCommon = [&](const ShaderBlockMember& member, std::string_view path, uint32_t offset, uint32_t bytes) {
                return checkNumber(path, "offset", member.offset, offset)
                    && checkNumber(path, "size", member.size, bytes)
                    && checkNumber(path, "paddedSize", member.paddedSize, bytes)
                    && checkNumber(path, "matrixStride", member.matrixStride, 0)
                    && checkNumber(path, "rowMajor", member.rowMajor ? 1u : 0u, 0);
            };
            const auto checkVectors = [&](const ShaderBlockMember& member, std::string_view path, uint32_t count, ShaderScalarKind scalar) {
                return checkNumber(path, "scalar", static_cast<uint32_t>(member.type.scalar), static_cast<uint32_t>(scalar))
                    && checkNumber(path, "scalarWidth", member.type.width, 32)
                    && checkNumber(path, "rows", member.type.rows, 1)
                    && checkNumber(path, "columns", member.type.columns, 4)
                    && checkNumber(path, "arrayCount", member.type.arrayCount, count)
                    && checkNumber(path, "arrayStride", member.arrayStride, count > 1 ? 16u : 0u)
                    && checkNumber(path, "nestedMemberCount", static_cast<uint32_t>(member.members.size()), 0);
            };
            const auto blockMatches = [&](ShaderStage stage, std::string_view entryName, uint32_t binding,
                uint32_t bytes, std::span<const ExpectedMember> expected) {
                const std::string path = "Frame shader ABI/" + std::string(entryName)
                    + (stage == ShaderStage::Vertex ? "(VS)" : "(FS)") + "/set0/binding" + std::to_string(binding);
                const auto* entry = FindShaderEntry(metadata, entryName, stage);
                if (!entry) { error = path + ": required entry is missing"; return false; }
                const auto found = std::find_if(entry->bindings.begin(), entry->bindings.end(), [&](const auto& value) { return value.set == 0 && value.binding == binding; });
                if (found == entry->bindings.end()) { error = path + ": required uniform block is missing"; return false; }
                if (!checkNumber(path, "resourceKind", static_cast<uint32_t>(found->kind), static_cast<uint32_t>(ShaderResourceKind::UniformBuffer))
                    || !checkNumber(path, "descriptorCount", found->count, 1)
                    || !checkNumber(path, "blockSize", found->blockSize, bytes)
                    || !checkNumber(path, "memberCount", static_cast<uint32_t>(found->members.size()), static_cast<uint32_t>(expected.size()))) return false;
                for (const auto& wanted : expected) {
                    const auto member = std::find_if(found->members.begin(), found->members.end(), [&](const auto& value) { return value.name == wanted.name; });
                    const std::string memberPath = path + "/" + std::string(wanted.name);
                    if (member == found->members.end()) { error = memberPath + ": required member is missing"; return false; }
                    const uint32_t memberBytes = wanted.vectors * 16;
                    if (!checkCommon(*member, memberPath, wanted.offset, memberBytes)) return false;
                    if (member->type.scalar != ShaderScalarKind::Structure) {
                        if (!checkVectors(*member, memberPath, wanted.vectors, wanted.scalar)) return false;
                        continue;
                    }
                    // Slang may retain a vector array as a struct containing exactly data[N].
                    // This is a second precise representation of the same host bytes, never a flattening rule.
                    if (wanted.vectors == 1) { error = memberPath + ": single float4 must not use an array wrapper"; return false; }
                    if (!checkNumber(memberPath, "scalarWidth", member->type.width, 0)
                        || !checkNumber(memberPath, "rows", member->type.rows, 1)
                        || !checkNumber(memberPath, "columns", member->type.columns, 1)
                        || !checkNumber(memberPath, "arrayCount", member->type.arrayCount, 1)
                        || !checkNumber(memberPath, "arrayStride", member->arrayStride, 0)
                        || !checkNumber(memberPath, "nestedMemberCount", static_cast<uint32_t>(member->members.size()), 1)) return false;
                    const auto& data = member->members.front();
                    if (data.name != "data") { error = memberPath + ": array wrapper must contain exactly the named data member"; return false; }
                    const std::string dataPath = memberPath + "/data";
                    if (!checkCommon(data, dataPath, 0, memberBytes) || !checkVectors(data, dataPath, wanted.vectors, wanted.scalar)) return false;
                }
                return true;
            };
            constexpr std::array<ExpectedMember, 2> frame{{
                {"time", static_cast<uint32_t>(offsetof(FrameGpuParameters, time)), 1},
                {"options", static_cast<uint32_t>(offsetof(FrameGpuParameters, options)), 1}}};
            constexpr std::array<ExpectedMember, 4> view{{
                {"viewProjectionColumns", static_cast<uint32_t>(offsetof(ViewGpuParameters, viewProjectionColumns)), 4},
                {"cameraPosition", static_cast<uint32_t>(offsetof(ViewGpuParameters, cameraPosition)), 1},
                {"inverseViewProjectionColumns", static_cast<uint32_t>(offsetof(ViewGpuParameters, inverseViewProjectionColumns)), 4},
                {"viewColumns", static_cast<uint32_t>(offsetof(ViewGpuParameters, viewColumns)), 4}}};
            constexpr std::array<ExpectedMember, 4> draw{{
                {"worldColumns", static_cast<uint32_t>(offsetof(DrawGpuParameters, worldColumns)), 4},
                {"normalColumns", static_cast<uint32_t>(offsetof(DrawGpuParameters, normalColumns)), 3},
                {"color", static_cast<uint32_t>(offsetof(DrawGpuParameters, color)), 1},
                {"ids", static_cast<uint32_t>(offsetof(DrawGpuParameters, ids)), 1, ShaderScalarKind::UnsignedInteger}}};
            if (!blockMatches(ShaderStage::Fragment, fragmentEntry, 0, static_cast<uint32_t>(sizeof(FrameGpuParameters)), frame)
                || !blockMatches(ShaderStage::Vertex, vertexEntry, 1, static_cast<uint32_t>(sizeof(ViewGpuParameters)), view)
                || !blockMatches(ShaderStage::Vertex, vertexEntry, 2, static_cast<uint32_t>(sizeof(DrawGpuParameters)), draw)
                || !blockMatches(ShaderStage::Fragment, fragmentEntry, 2, static_cast<uint32_t>(sizeof(DrawGpuParameters)), draw)) return false;
            error.clear();
            return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    RHIStatus ParseRendererProfile(std::string_view source, RendererProfile& output)
    {
        constexpr const char* operation = "ParseRendererProfile";
        if (source.empty() || source.size() > 65536) return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        try {
            using Json = nlohmann::json;
            std::set<std::string> keys;
            bool invalid = false;
            const auto document = Json::parse(source.begin(), source.end(), [&](int depth, Json::parse_event_t event, Json& value) {
                if (depth > 3) invalid = true;
                if (event == Json::parse_event_t::key && !keys.insert(value.get<std::string>()).second) invalid = true;
                return !invalid;
            }, false);
            if (invalid || !document.is_object() || document.size() != 6)
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            constexpr std::array<const char*, 6> required{ "schemaVersion", "profile", "depthFormat", "depthPrepass", "normalDebug", "clearColor" };
            for (const auto* field : required)
                if (!document.contains(field)) return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            if (!document["schemaVersion"].is_number_integer() || document["schemaVersion"] != 1
                || !document["profile"].is_string() || !document["depthFormat"].is_string()
                || document["depthFormat"] != "D32_Float" || !document["depthPrepass"].is_boolean()
                || !document["normalDebug"].is_boolean() || !document["clearColor"].is_array()
                || document["clearColor"].size() != 4)
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            const auto& id = document["profile"].get_ref<const std::string&>();
            if (id.empty() || id.size() > 64 || id[0] < 'a' || id[0] > 'z')
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            for (char c : id)
                if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '.' && c != '_' && c != '-')
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            RendererProfile candidate;
            candidate.depthPrepass = document["depthPrepass"].get<bool>();
            candidate.normalDebug = document["normalDebug"].get<bool>();
            for (size_t i = 0; i < 4; ++i) {
                if (!document["clearColor"][i].is_number() || !FloatValue(document["clearColor"][i].get<double>(), candidate.clearColor[i]))
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            }
            if (!ValidProfile(candidate)) return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            output = candidate;
            return {};
        } catch (const std::bad_alloc&) { return { .code = RHIErrorCode::OutOfMemory, .operation = operation }; }
        catch (const nlohmann::json::exception&) { return { .code = RHIErrorCode::InvalidDescription, .operation = operation }; }
    }

    RHIStatus BuildPresentationView(const RenderView& source, Extent2D extent, RenderView& output) noexcept
    {
        constexpr const char* operation = "BuildPresentationView";
        if (extent.IsEmpty()) return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        RenderView candidate = source;
        candidate.viewport.width = extent.width;
        candidate.viewport.height = extent.height;
        if (!BuildCameraProjection(candidate.lens, candidate.viewport, candidate.matrices.projection,
            candidate.matrices.inverseProjection).IsSuccess())
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        using DMat4 = glm::mat<4, 4, double, glm::packed_highp>;
        const auto multiply = [](const Math::Mat4& left, const Math::Mat4& right, Math::Mat4& result) {
            const DMat4 product = DMat4(left) * DMat4(right);
            Math::Mat4 value{0};
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!FloatValue(product[column][row], value[column][row])) return false;
            result = value;
            return true;
        };
        if (!multiply(candidate.matrices.projection, candidate.matrices.view, candidate.matrices.viewProjection)
            || !multiply(candidate.matrices.inverseView, candidate.matrices.inverseProjection, candidate.matrices.inverseViewProjection))
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        const DMat4 products[]{
            DMat4(candidate.matrices.viewProjection) * DMat4(candidate.matrices.inverseViewProjection),
            DMat4(candidate.matrices.inverseViewProjection) * DMat4(candidate.matrices.viewProjection)
        };
        for (const auto& product : products)
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!std::isfinite(product[column][row]) || std::abs(product[column][row] - (column == row ? 1.0 : 0.0)) > 2.0e-4)
                        return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        output = candidate;
        return {};
    }

    RHIStatus BuildFrameUniformData(const RenderFramePacket& packet, const RendererProfile& profile,
        uint64_t alignment, uint64_t maxRange, PackedFrameUniforms& output)
    {
        if (!ValidProfile(profile) || packet.views.empty()
            || maxRange < std::max(sizeof(DrawGpuParameters), sizeof(ViewGpuParameters))
            || packet.instances.size() >= UINT32_MAX || alignment == 0 || (alignment & (alignment - 1)) != 0)
            return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
        try {
            PackedFrameUniforms candidate;
            uint64_t viewStart = 0, viewStride = 0, drawStart = 0, stride = 0;
            if (!Align(sizeof(FrameGpuParameters), alignment, viewStart)
                || !Align(sizeof(ViewGpuParameters), alignment, viewStride)
                || !Align(sizeof(DrawGpuParameters), alignment, stride))
                return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
            const uint64_t viewCount = packet.views.size();
            if (viewCount - 1 > (std::numeric_limits<uint64_t>::max() - viewStart) / viewStride)
                return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
            uint64_t total = viewStart + (viewCount - 1) * viewStride;
            if (total > std::numeric_limits<uint64_t>::max() - sizeof(ViewGpuParameters))
                return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
            total += sizeof(ViewGpuParameters);
            if (!Align(total, alignment, drawStart))
                return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
            if (!packet.instances.empty()) {
                const uint64_t count = packet.instances.size();
                if (count - 1 > (std::numeric_limits<uint64_t>::max() - drawStart) / stride)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
                total = drawStart + (count - 1) * stride;
                if (total > std::numeric_limits<uint64_t>::max() - sizeof(DrawGpuParameters))
                    return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
                total += sizeof(DrawGpuParameters);
            }
            if (total > candidate.data.max_size() || packet.views.size() > candidate.viewOffsets.max_size()
                || packet.instances.size() > candidate.drawOffsets.max_size())
                return { .code = RHIErrorCode::OutOfMemory, .operation = PackingOperation };
            candidate.data.resize(static_cast<size_t>(total), std::byte{0});
            candidate.drawOffsets.reserve(packet.instances.size());
            candidate.viewOffsets.reserve(packet.views.size());
            FrameGpuParameters frame;
            const auto& time = packet.time;
            if (time.simulationSeconds < 0.0 || time.realDeltaSeconds < 0.0 || time.fixedDeltaSeconds <= 0.0
                || time.interpolationAlpha < 0.0 || time.interpolationAlpha > 1.0
                || !FloatValue(std::fmod(time.simulationSeconds, 4096.0), frame.time[0]) || !FloatValue(time.realDeltaSeconds, frame.time[1])
                || !FloatValue(time.fixedDeltaSeconds, frame.time[2]) || !FloatValue(time.interpolationAlpha, frame.time[3]))
                return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
            frame.options[0] = profile.normalDebug ? 1.0f : 0.0f;
            frame.options[1] = 0.1f;
            if (!FloatValue(std::floor(time.simulationSeconds / 4096.0), frame.options[2]))
                return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
            std::memcpy(candidate.data.data(), &frame, sizeof(frame));
            std::set<uint64_t> viewIds;
            for (size_t index = 0; index < packet.views.size(); ++index) {
                const auto& source = packet.views[index];
                if (source.viewport.width == 0 || source.viewport.height == 0 || !viewIds.insert(source.id).second)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
                ViewGpuParameters view;
                for (int column = 0; column < 4; ++column)
                    for (int row = 0; row < 4; ++row)
                        if (!FloatValue(source.matrices.viewProjection[column][row], view.viewProjectionColumns[column][row])
                            || !FloatValue(source.matrices.inverseViewProjection[column][row], view.inverseViewProjectionColumns[column][row])
                            || !FloatValue(source.matrices.view[column][row], view.viewColumns[column][row]))
                            return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
                for (int axis = 0; axis < 3; ++axis)
                    if (!FloatValue(source.matrices.position[axis], view.cameraPosition[axis]))
                        return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
                view.cameraPosition[3] = 1.0f;
                const uint64_t offset = viewStart + index * viewStride;
                candidate.viewOffsets.push_back(offset);
                std::memcpy(candidate.data.data() + offset, &view, sizeof(view));
            }
            for (size_t i = 0; i < packet.instances.size(); ++i) {
                const auto& instance = packet.instances[i];
                DrawGpuParameters draw;
                draw.ids[0] = static_cast<uint32_t>(i + 1);
                // double只用于派生符号，避免有限float世界矩阵的det乘法先溢出。
                draw.ids[1] = glm::determinant(glm::dmat3{ Math::Mat3{ instance.worldMatrix } }) < 0 ? 1u : 0u;
                for (int column = 0; column < 4; ++column)
                    for (int row = 0; row < 4; ++row)
                        if (!FloatValue(instance.worldMatrix[column][row], draw.worldColumns[column][row]))
                            return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
                for (int column = 0; column < 3; ++column)
                    for (int row = 0; row < 3; ++row)
                        if (!FloatValue(instance.normalMatrix[column][row], draw.normalColumns[column][row]))
                            return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
                for (int channel = 0; channel < 4; ++channel)
                    if (!FloatValue(instance.color[channel], draw.color[channel]) || draw.color[channel] < 0.0f
                        || (channel == 3 && draw.color[channel] > 1.0f))
                        return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
                const uint64_t offset = drawStart + i * stride;
                candidate.drawOffsets.push_back(offset);
                std::memcpy(candidate.data.data() + offset, &draw, sizeof(draw));
            }
            output = std::move(candidate);
            return {};
        } catch (const std::bad_alloc&) { return { .code = RHIErrorCode::OutOfMemory, .operation = PackingOperation }; }
    }

    RendererFrameContext::~RendererFrameContext()
    {
        for (auto& slot : m_Slots) m_Device.ReleaseBuffer(slot.buffer);
    }

    RHIStatus RendererFrameContext::Prepare(uint32_t slot, const RenderFramePacket& packet, const RendererProfile& profile, Extent2D targetExtent)
    {
        constexpr const char* operation = "RendererFrameContext::Prepare";
        if (slot >= m_Device.GetFrameCount()) return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        BufferHandle unpublished;
        try {
            if (m_Slots.empty()) m_Slots.resize(m_Device.GetFrameCount());
            RenderFramePacket presentation = packet;
            if (packet.views.empty()) {
                if (!m_ViewHistory.Stage(packet.sequence, packet.sceneIdentity, {}).IsSuccess())
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
                m_Slots[slot].views.clear();
                m_Slots[slot].visibility.clear();
                m_Slots[slot].packing = {};
                return {};
            }
            // 唯一主窗口按实际交换链尺寸重建；独立离屏上下文传入自身目标尺寸。
            if (packet.views.size() == 1)
                if (const auto status = BuildPresentationView(packet.views[0], targetExtent, presentation.views[0]); !status.IsSuccess()) return status;
            std::vector<VisibilityResult> visibility(presentation.views.size());
            for (size_t index = 0; index < presentation.views.size(); ++index)
                if (const auto status = VisibilitySystem::Build(presentation.views[index], presentation.instances, visibility[index]); !status.IsSuccess())
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            PackedFrameUniforms candidate;
            const auto& capabilities = m_Device.GetCapabilities();
            if (const auto status = BuildFrameUniformData(presentation, profile, capabilities.minUniformBufferOffsetAlignment,
                capabilities.maxUniformBufferRange, candidate); !status.IsSuccess()) return status;
            auto& frame = m_Slots[slot];
            BufferHandle buffer = frame.buffer;
            const bool replace = !buffer.HasValue() || frame.capacity < candidate.data.size();
            if (replace) {
                // 输出直接由try外候选接收：创建过程发布句柄后发生命名分配异常也可清理。
                if (const auto status = m_Device.CreateBuffer({ .size = candidate.data.size(), .usage = RHIBufferUsage::Uniform,
                    .memory = RHIMemoryDomain::Upload }, unpublished); !status.IsSuccess()) {
                    m_Device.ReleaseBuffer(std::exchange(unpublished, BufferHandle{}));
                    return status;
                }
                buffer = unpublished;
            }
            if (const auto status = m_Device.WriteBuffer(buffer, 0, candidate.data); !status.IsSuccess()) {
                m_Device.ReleaseBuffer(std::exchange(unpublished, BufferHandle{}));
                return status;
            }
            if (!m_ViewHistory.Stage(packet.sequence, packet.sceneIdentity, presentation.views).IsSuccess()) {
                m_Device.ReleaseBuffer(std::exchange(unpublished, BufferHandle{}));
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            }
            if (replace) {
                const auto previous = std::exchange(frame.buffer, std::exchange(unpublished, BufferHandle{}));
                frame.capacity = candidate.data.size();
                m_Device.ReleaseBuffer(previous);
            }
            frame.packing = std::move(candidate);
            frame.views = std::move(presentation.views);
            frame.visibility = std::move(visibility);
            return {};
        } catch (const std::bad_alloc&) {
            // WriteBuffer本身也可能在初始化范围扩容时抛出，尚未发布的候选owner须归还。
            m_Device.ReleaseBuffer(unpublished);
            m_ViewHistory.OnCancelled();
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
        }
    }

    BufferHandle RendererFrameContext::GetBuffer(uint32_t slot) const noexcept
    {
        return slot < m_Slots.size() ? m_Slots[slot].buffer : BufferHandle{};
    }
    const PackedFrameUniforms* RendererFrameContext::GetPacking(uint32_t slot) const noexcept
    {
        return slot < m_Slots.size() ? &m_Slots[slot].packing : nullptr;
    }
    const RenderView* RendererFrameContext::GetView(uint32_t slot, size_t index) const noexcept
    {
        return slot < m_Slots.size() && index < m_Slots[slot].views.size() ? &m_Slots[slot].views[index] : nullptr;
    }
    const VisibilityResult* RendererFrameContext::GetVisibility(uint32_t slot, size_t index) const noexcept
    {
        return slot < m_Slots.size() && index < m_Slots[slot].visibility.size() ? &m_Slots[slot].visibility[index] : nullptr;
    }
}
