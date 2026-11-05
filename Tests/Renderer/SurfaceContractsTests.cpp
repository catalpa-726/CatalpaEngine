#include "TestRunner.h"
#include "Support/FakeRHI.h"
#include "Renderer/Lighting/LightData.h"
#include "Renderer/RenderScene.h"
#include "Renderer/Visibility/VisibilitySystem.h"
#include "Renderer/RendererFrameContext.h"
#include "Renderer/Readback/ReadbackManager.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/DebugDraw.h"
#include "Geometry/PrimitiveMesh.h"

namespace Catalpa::Tests {
    namespace {
        // 为读回提供明确可控的完成证据和返回token，不模拟真实GPU执行。
        class ReadbackDevice final : public FakeDevice {
        public:
            uint32_t pixel = 2;
            size_t reads = 0, releases = 0;
            RHIStatus CreateBuffer(const RHIBufferDesc& desc, BufferHandle& output) override
            {
                if (desc.memory != RHIMemoryDomain::Readback || desc.size != 4)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = "ReadbackFixture/buffer" };
                RHIBufferStateSnapshot snapshot;
                snapshot.desc = desc;
                output = AddBuffer(snapshot);
                return {};
            }
            RHIStatus ReadBuffer(BufferHandle, uint64_t offset, std::span<std::byte> bytes, RHICompletionPoint completion) override
            {
                bool complete = false;
                if (offset != 0 || bytes.size() != sizeof(pixel) || !queue.IsComplete(completion, complete).IsSuccess() || !complete)
                    return { .code = RHIErrorCode::InvalidState, .operation = "ReadbackFixture/completion" };
                ++reads;
                std::memcpy(bytes.data(), &pixel, sizeof(pixel));
                return {};
            }
            void ReleaseBuffer(BufferHandle buffer) override { if (buffer.HasValue()) ++releases; }
        };

        bool SetAlpha(RenderMeshInstance& instance, MaterialAlphaMode alpha, uint64_t identity)
        {
            std::string error;
            MaterialDefinition definition;
            definition.alphaMode = alpha;
            AssetVersionRef<MaterialDefinition> fixed;
            if (!AssetVersionRef<MaterialDefinition>::TryCreate(AssetId::FromWords(201, identity), 1,
                std::make_shared<const MaterialDefinition>(definition), fixed, error)) return false;
            MaterialInstance material;
            material.definition = fixed;
            AssetVersionRef<MaterialInstance> value;
            if (!AssetVersionRef<MaterialInstance>::TryCreate(AssetId::FromWords(202, identity), 1,
                std::make_shared<const MaterialInstance>(material), value, error)) return false;
            instance.materials = { value };
            return true;
        }
    }

    void RunSurfaceContractsTests(TestContext& test)
    {
        MeshData mesh;
        MeshResourceRef resource;
        test.Check(CreateCubeMesh(1, mesh).IsSuccess() && CreateMeshResource(mesh, resource).IsSuccess(),
            "Surface fixture owns a validated immutable metre cube");
        if (!resource) return;
        RenderFramePacket packet;
        for (uint32_t index = 0; index < 4; ++index) {
            Math::Mat4 world{ 1 };
            world[3].z = index == 0 ? -3.0f : index == 1 ? -7.0f : -5.0f;
            RenderMeshInstance instance;
            test.Check(BuildRenderMeshInstance({ 7, index, 1 }, resource, world, { 1, 1, 1, 1 }, instance).IsSuccess(),
                "Surface fixture derives independent conservative world bounds");
            test.Check(SetAlpha(instance, index == 0 ? MaterialAlphaMode::Mask : MaterialAlphaMode::Blend, index + 1),
                "Visibility fixture freezes its own material alpha classification");
            packet.instances.push_back(std::move(instance));
        }
        RenderView view;
        view.id = 5;
        test.Check(BuildPresentationView(view, { 640, 480 }, view).IsSuccess(), "Visibility fixture uses an actual ZO projection");
        VisibilityResult visible;
        test.Check(VisibilitySystem::Build(view, packet.instances, visible).IsSuccess()
            && visible.masked.size() == 1 && visible.opaque.empty() && visible.transparent.size() == 3,
            "Mask remains a depth-writing class while Blend enters the transparent list");
        if (visible.transparent.size() == 3)
            test.Check(visible.transparent[0].instanceIndex == 1 && visible.transparent[1].instanceIndex == 2
                && visible.transparent[2].instanceIndex == 3,
                "Blend sorts back to front and equal depths use stable instance identity");

        auto missingMesh = packet.instances;
        missingMesh.back().mesh.reset();
        const auto previousIndices = visible.instanceIndices;
        test.Check(!VisibilitySystem::Build(view, missingMesh, visible).IsSuccess()
            && visible.instanceIndices == previousIndices,
            "Missing geometry rejects the whole candidate and preserves the previous visible set");

        RenderLight light;
        light.parameters.castShadows = true;
        light.parameters.shadowBiasMeters = 0.01f;
        light.direction = { 0, -1, 0 };
        packet.lights.push_back(light);
        DirectionalShadowView shadow;
        test.Check(BuildDirectionalShadowView(packet, 1024, shadow).IsSuccess() && shadow.enabled
            && shadow.lightIndex == 0 && shadow.normalizedBias > 0, "Directional shadow derives one finite metre-to-ZO bias and stable light index");
        bool covered = true;
        for (const auto& instance : packet.instances)
            for (uint32_t corner = 0; corner < 8; ++corner) {
                const auto& bounds = instance.worldBounds;
                const Math::Vec4 position{ (corner & 1) ? bounds.maximum.x : bounds.minimum.x,
                    (corner & 2) ? bounds.maximum.y : bounds.minimum.y,
                    (corner & 4) ? bounds.maximum.z : bounds.minimum.z, 1 };
                const auto clip = shadow.viewProjection * position;
                covered &= std::abs(clip.x) <= 1.0001f && std::abs(clip.y) <= 1.0001f && clip.z >= 0 && clip.z <= 1;
            }
        test.Check(covered, "Shadow ZO projection conservatively contains every source bounds corner");
        const auto previousShadow = shadow;
        packet.lights[0].parameters.shadowBiasMeters = std::numeric_limits<float>::max();
        test.Check(!BuildDirectionalShadowView(packet, 1024, shadow).IsSuccess()
            && shadow.viewProjection == previousShadow.viewProjection && shadow.normalizedBias == previousShadow.normalizedBias,
            "A finite world bias that exceeds the light depth span cannot overflow or replace the previous shadow view");
        packet.lights[0].parameters.shadowBiasMeters = 0.01f;
        PackedLightingData lights;
        test.Check(BuildLightingData(packet, shadow, 1024, 5, lights).IsSuccess()
            && lights.countEnvironment[0] == 1 && lights.countEnvironment[1] == 0
            && lights.shadowParameters[3] == 4 && lights.lights[0].typeInnerCosShadow[2] == 1,
            "Lighting ABI exposes explicit count, disabled missing IBL, shadow ownership and specular mip range");
        const auto preserved = lights;
        packet.lights.resize(MaximumRenderLights + 1);
        test.Check(!BuildLightingData(packet, shadow, 1024, 5, lights).IsSuccess()
            && std::memcmp(&lights, &preserved, sizeof(lights)) == 0, "Light budget overflow cannot publish a truncated lighting block");

        DebugDraw debug;
        test.Check(debug.AddBounds(packet.instances[0].worldBounds, { 0, 1, 0, 1 }).IsSuccess()
            && debug.GetVertices().size() == 24, "Bounds overlay encodes exactly twelve independent line segments");
        const auto count = debug.GetVertices().size();
        test.Check(!debug.AddLine({ {}, {}, { -1, 1, 1, 1 } }).IsSuccess()
            && debug.GetVertices().size() == count, "Invalid debug color preserves previously accepted lines");

        ReadbackDevice device;
        RHITextureStateSnapshot texture;
        texture.extent = { 4, 4 };
        texture.format = PixelFormat::R32_UInt;
        texture.usage = RHITextureUsage::TransferSource;
        texture.state.initialized = true;
        const auto source = device.AddTexture(texture);
        ReadbackManager readback(device);
        auto lease = std::make_shared<int>(1);
        const std::array<RenderInstanceId, 2> ids{{ { 7, 10, 2 }, { 7, 11, 9 } }};
        const PickingRequest request{ 1, 5, 7, 44, 2, 1, 2 };
        uint64_t ticket = 0;
        test.Check(readback.Request(request, source, lease, ids, ticket).IsSuccess(), "Picking freezes displayed-frame identity table before queue submission");
        PickingResult result;
        test.Check(!readback.Poll(ticket, result) && device.reads == 0, "Unsubmitted readback remains pending without host memory access");
        RenderGraph graph(device);
        RenderGraphPlan plan;
        test.Check(readback.AddPasses(graph).IsSuccess() && graph.Compile(plan).IsSuccess(),
            "Readback is a real graph side effect without a fabricated present target");
        readback.OnCancelled();
        RenderGraph retry(device);
        RenderGraphPlan retried;
        test.Check(!readback.Poll(ticket, result) && readback.AddPasses(retry).IsSuccess() && retry.Compile(retried).IsSuccess(),
            "Cancelled recording can retry the same pixel payload and ticket without claiming queue completion");
        device.queue.submitted = 1;
        readback.OnSubmitted({ device.GetDeviceId(), device.queue.id, 1 });
        test.Check(!readback.Poll(ticket, result) && device.reads == 0, "Queue acceptance is insufficient to read GPU destination bytes");
        device.queue.completed = 1;
        test.Check(readback.Poll(ticket, result) && result.status == PickingResultStatus::Ready && result.hit == ids[1]
            && result.request.frameSequence == 44 && device.reads == 1 && device.releases == 1,
            "Completed pixel token resolves through retained full scene/index/generation identity");
        bool recycled = true;
        for (uint32_t index = 0; index < 96; ++index) {
            recycled &= readback.Request(request, source, lease, ids, ticket).IsSuccess();
            readback.Cancel(ticket);
        }
        test.Check(recycled && lease.use_count() == 1,
            "Repeated cancelled clicks release ticket quota and source leases without requiring Poll");
        test.Check(readback.Request(request, source, lease, ids, ticket).IsSuccess(), "Closed-surface fixture can allocate a fresh ticket");
        readback.CancelSurface(request.surfaceId);
        test.Check(lease.use_count() == 1 && !readback.Poll(ticket, result), "Closing a preview invalidates every associated pending ticket and lease");
    }
}
