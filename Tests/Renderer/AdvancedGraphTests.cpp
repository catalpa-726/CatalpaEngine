#include "catpch.h"
#include "TestRunner.h"
#include "Support/FakeRHI.h"
#include "Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Renderer/RenderGraph/RenderGraphCompiler.h"

namespace Catalpa::Tests {
    namespace {

        RHITextureStateSnapshot TextureSnapshot(RHITextureUsage usage, bool initialized) {
            RHITextureStateSnapshot snapshot;
            snapshot.extent = {16, 16};
            snapshot.format = PixelFormat::RGBA8_UNorm;
            snapshot.usage = usage;
            snapshot.state.initialized = initialized;
            if (initialized) snapshot.state.access = {RHIPipelineStage::FragmentShader,
                RHIResourceAccess::SampledRead, RHITextureLayout::ShaderReadOnly};
            return snapshot;
        }

        RGIndexedDrawDesc FullscreenDraw(FakeDevice& device, RGTexture color) {
            RGIndexedDrawDesc draw;
            draw.color = color;
            draw.pipeline = {device.GetDeviceId(), {0, 1}};
            draw.vertexCount = 3;
            draw.viewport = {0, 0, 16, 16, 0, 1};
            draw.scissor.extent = {16, 16};
            return draw;
        }

    }

    // 使用生产Builder/Compiler的CPU状态替身，只证明图契约，不冒称真实GPU执行。
    void RunAdvancedGraphTests(TestContext& test) {
        FakeDevice device;
        const auto attachmentUsage = RHITextureUsage::ColorAttachment | RHITextureUsage::Sampled;
        const auto colorHandle = device.AddTexture(TextureSnapshot(attachmentUsage, false));
        RenderGraphBuilder offscreen(device);
        RGTexture color;
        test.Check(offscreen.ImportTexture(colorHandle, color).IsSuccess() &&
            offscreen.AddClearPass("Offscreen clear", color, {0, 0, 0, 1}).IsSuccess() &&
            offscreen.ExportTexture(color).IsSuccess(), "Offscreen graph declares a real sampled export without Present");
        RenderGraphPlan plan;
        test.Check(RenderGraphCompiler::Compile(offscreen, plan).IsSuccess() && plan.IsValid() &&
            plan.GetSchedule().passOrder.size() == 1, "Offscreen clear survives dead-pass elimination");
        FakeCommandList commands;
        const bool recorded = plan.Record(commands).IsSuccess();
        test.Check(recorded && commands.textureUpdates.size() == 1 && commands.textureUpdates.front().after.initialized &&
            commands.textureUpdates.front().after.access.layout == RHITextureLayout::ShaderReadOnly,
            "Export records a sampled-layout transition and initialized output without a fabricated swapchain");

        const auto sourceA = device.AddTexture(TextureSnapshot(RHITextureUsage::Sampled, true));
        const auto sourceB = device.AddTexture(TextureSnapshot(RHITextureUsage::Sampled, true));
        RenderGraphBuilder fullscreen(device);
        RGTexture target, textureA, textureB;
        test.Check(fullscreen.ImportTexture(colorHandle, target).IsSuccess() &&
            fullscreen.ImportTexture(sourceA, textureA).IsSuccess() && fullscreen.ImportTexture(sourceB, textureB).IsSuccess(),
            "Fullscreen resources import without vertex or index buffers");
        auto draw = FullscreenDraw(device, target);
        const SamplerHandle sharedSampler{device.GetDeviceId(), {1, 1}};
        draw.sampledTextures = {{textureA, sharedSampler, 4, 9}, {textureB, sharedSampler, 5, 9}};
        test.Check(fullscreen.AddDrawPass("Fullscreen triangle", draw).IsSuccess() &&
            fullscreen.ExportTexture(target).IsSuccess() && RenderGraphCompiler::Compile(fullscreen, plan).IsSuccess(),
            "Vertexless draw accepts two textures sharing the same explicit sampler binding and handle");
        test.Check(plan.GetSchedule().passOrder.size() == 1, "Fullscreen draw remains a live exported producer");

        RenderGraphBuilder conflicting(device);
        RGTexture conflictTarget, conflictA, conflictB;
        const bool imported = conflicting.ImportTexture(colorHandle, conflictTarget).IsSuccess() &&
            conflicting.ImportTexture(sourceA, conflictA).IsSuccess() && conflicting.ImportTexture(sourceB, conflictB).IsSuccess();
        auto badDraw = FullscreenDraw(device, conflictTarget);
        badDraw.sampledTextures = {{conflictA, sharedSampler, 4, 9},
            {conflictB, SamplerHandle{device.GetDeviceId(), {2, 1}}, 5, 9}};
        test.Check(imported && !conflicting.AddDrawPass("Conflicting samplers", badDraw).IsSuccess(),
            "Shared sampler binding rejects different sampler handles instead of silently choosing one");

        // Texture逻辑范围按layer*mips+mip索引；mip0就绪不等于整条mip链可采样。
        std::vector<RGResourceDeclaration> resources{{.kind = RGResourceKind::Texture, .size = 3,
            .initialized = {{.offset = 0, .size = 1}}}};
        std::vector<RGPassDeclaration> passes{{.accesses = {{.resource = 0, .offset = 1, .size = 1}}, .sideEffect = true}};
        RGCompiledSchedule schedule;
        test.Check(!RenderGraphCompiler::BuildSchedule(resources, passes, {}, {}, schedule).IsSuccess(),
            "Reading an uninitialized mip fails even when mip0 is initialized");
        passes = {{.accesses = {{.resource = 0, .offset = 1, .size = 1, .mode = RGAccessMode::Write,
            .initialization = RGWriteInitialization::Initialize}}}};
        const std::array mipOutput{RGResourceAccess{.resource = 0, .offset = 1, .size = 1}};
        test.Check(RenderGraphCompiler::BuildSchedule(resources, passes, {}, mipOutput, schedule).IsSuccess(),
            "A fully uploaded mip can be exported independently of other mips");
        const std::array allMips{RGResourceAccess{.resource = 0, .size = 3}};
        test.Check(!RenderGraphCompiler::BuildSchedule(resources, passes, {}, allMips, schedule).IsSuccess(),
            "Partial mip initialization cannot fabricate a fully sampleable chain");

        auto pickSnapshot = TextureSnapshot(RHITextureUsage::TransferSource, true);
        pickSnapshot.format = PixelFormat::R32_UInt;
        const auto pickHandle = device.AddTexture(pickSnapshot);
        RHIBufferStateSnapshot readback;
        readback.desc.size = 4;
        readback.desc.memory = RHIMemoryDomain::Readback;
        readback.desc.usage = RHIBufferUsage::TransferDestination;
        const auto readbackHandle = device.AddBuffer(readback);
        RenderGraphBuilder picking(device);
        RGTexture pickTexture;
        RGBuffer pickBuffer;
        RHITextureBufferCopy copy;
        copy.extent = {1, 1};
        test.Check(picking.ImportTexture(pickHandle, pickTexture).IsSuccess() &&
            picking.ImportBuffer(readbackHandle, pickBuffer).IsSuccess() &&
            picking.AddTextureReadbackPass("Pick pixel", pickTexture, pickBuffer, copy).IsSuccess() &&
            RenderGraphCompiler::Compile(picking, plan).IsSuccess(),
            "Host readback is an explicit live side effect without Present or a texture export");
        FakeCommandList readCommands;
        readCommands.allowReadback = true;
        test.Check(plan.Record(readCommands).IsSuccess() && readCommands.bufferUpdates.size() == 1 &&
            readCommands.bufferUpdates.front().after.access.stage == RHIPipelineStage::Host &&
            readCommands.bufferUpdates.front().after.access.access == RHIResourceAccess::HostRead &&
            RangeContains(readCommands.bufferUpdates.front().after.initialized, 0, 4) &&
            std::find(readCommands.events.begin(), readCommands.events.end(), "CopyTextureToBuffer") != readCommands.events.end(),
            "Readback records the copy and final HostRead visibility before the consumer waits for completion");
    }

}
