#include "catpch.h"
#include "Renderer/RendererFrameContext.h"
#include "RHI/RHIDevice.h"
#include "Camera/CameraProjection.h"
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
        if (!ValidProfile(profile) || packet.views.size() != 1 || packet.views[0].viewport.width == 0 || packet.views[0].viewport.height == 0
            || maxRange < sizeof(DrawGpuParameters) || alignment == 0 || (alignment & (alignment - 1)) != 0)
            return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
        try {
            PackedFrameUniforms candidate;
            uint64_t drawStart = 0, stride = 0;
            if (!Align(sizeof(FrameGpuParameters), alignment, candidate.viewOffset)
                || candidate.viewOffset > std::numeric_limits<uint64_t>::max() - sizeof(ViewGpuParameters)
                || !Align(candidate.viewOffset + sizeof(ViewGpuParameters), alignment, drawStart)
                || !Align(sizeof(DrawGpuParameters), alignment, stride))
                return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
            uint64_t total = candidate.viewOffset + sizeof(ViewGpuParameters);
            if (!packet.instances.empty()) {
                const uint64_t count = packet.instances.size();
                if (count - 1 > (std::numeric_limits<uint64_t>::max() - drawStart) / stride)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
                total = drawStart + (count - 1) * stride;
                if (total > std::numeric_limits<uint64_t>::max() - sizeof(DrawGpuParameters))
                    return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
                total += sizeof(DrawGpuParameters);
            }
            if (total > candidate.data.max_size()) return { .code = RHIErrorCode::OutOfMemory, .operation = PackingOperation };
            candidate.data.resize(static_cast<size_t>(total), std::byte{0});
            candidate.drawOffsets.reserve(packet.instances.size());
            FrameGpuParameters frame;
            const auto& time = packet.time;
            if (time.simulationSeconds < 0.0 || time.realDeltaSeconds < 0.0 || time.fixedDeltaSeconds <= 0.0
                || time.interpolationAlpha < 0.0 || time.interpolationAlpha > 1.0
                || !FloatValue(time.simulationSeconds, frame.time[0]) || !FloatValue(time.realDeltaSeconds, frame.time[1])
                || !FloatValue(time.fixedDeltaSeconds, frame.time[2]) || !FloatValue(time.interpolationAlpha, frame.time[3]))
                return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
            frame.options[0] = profile.normalDebug ? 1.0f : 0.0f;
            std::memcpy(candidate.data.data(), &frame, sizeof(frame));
            ViewGpuParameters view;
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!FloatValue(packet.views[0].matrices.viewProjection[column][row], view.viewProjectionColumns[column][row]))
                        return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
            for (int axis = 0; axis < 3; ++axis)
                if (!FloatValue(packet.views[0].matrices.position[axis], view.cameraPosition[axis]))
                    return { .code = RHIErrorCode::InvalidDescription, .operation = PackingOperation };
            view.cameraPosition[3] = 1.0f;
            std::memcpy(candidate.data.data() + candidate.viewOffset, &view, sizeof(view));
            for (size_t i = 0; i < packet.instances.size(); ++i) {
                const auto& instance = packet.instances[i];
                DrawGpuParameters draw;
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
            if (packet.views.size() != 1) return { .code = RHIErrorCode::Unsupported, .operation = operation };
            if (const auto status = BuildPresentationView(packet.views[0], targetExtent, presentation.views[0]); !status.IsSuccess()) return status;
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
            if (replace) {
                const auto previous = std::exchange(frame.buffer, std::exchange(unpublished, BufferHandle{}));
                frame.capacity = candidate.data.size();
                m_Device.ReleaseBuffer(previous);
            }
            frame.packing = std::move(candidate);
            return {};
        } catch (const std::bad_alloc&) {
            // WriteBuffer本身也可能在初始化范围扩容时抛出，尚未发布的候选owner须归还。
            m_Device.ReleaseBuffer(unpublished);
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
}
