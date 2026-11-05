#include "catpch.h"
#include "TestRunner.h"
#include "Renderer/RenderGraph/RenderGraphCompiler.h"

namespace Catalpa::Tests {
    namespace {
        // 构造独立的字节访问期望；不调用 Builder 推导函数来生成测试答案。
        RGResourceAccess Read(uint32_t resource, uint64_t offset, uint64_t size)
        {
            return { .resource = resource, .offset = offset, .size = size };
        }
        RGResourceAccess Write(uint32_t resource, uint64_t offset, uint64_t size)
        {
            return { .resource = resource, .offset = offset, .size = size,
                .mode = RGAccessMode::Write, .initialization = RGWriteInitialization::Initialize };
        }
        bool HasEdge(const RGCompiledSchedule& schedule, uint32_t before, uint32_t after, RGDependencyKind kind)
        {
            return std::find(schedule.dependencies.begin(), schedule.dependencies.end(),
                RGDependency{ .before = before, .after = after, .kind = kind }) != schedule.dependencies.end();
        }
    }

    // 同生产 Compile 使用的范围/依赖编译器；不创建窗口、GPU 或测试专用生产入口。
    void RunRenderGraphHazardTests(TestContext& test)
    {
        std::vector<RGResourceDeclaration> resources{ { .size = 64 } };
        std::vector<RGPassDeclaration> passes{
            { .accesses = { Write(0, 0, 64) } },
            { .accesses = { Read(0, 0, 64) }, .sideEffect = true },
            { .accesses = { Write(0, 0, 64) } }
        };
        std::vector<RGResourceAccess> outputs{ Read(0, 0, 64) };
        RGCompiledSchedule schedule;
        auto compile = [&] (std::span<const RGDependency> dependencies = {}) {
            return RenderGraphCompiler::BuildSchedule(resources, passes, dependencies, outputs, schedule);
        };
        test.Check(compile().IsSuccess(), "Initialized write/read/overwrite sequence compiles");
        test.Check(HasEdge(schedule, 0, 1, RGDependencyKind::RAW), "Read depends on the producing range version");
        test.Check(HasEdge(schedule, 1, 2, RGDependencyKind::WAR), "Overwrite waits for the earlier range reader");
        test.Check(HasEdge(schedule, 0, 2, RGDependencyKind::WAW), "Two overlapping writes preserve execution order");
        test.Check(schedule.passOrder == std::vector<uint32_t>{ 0, 1, 2 }, "Live hazard chain retains stable execution order");
        test.Check(schedule.versions.size() == 3 && schedule.versions[1].inputVersion == 1
            && schedule.versions[2].outputVersion == 2, "Logical write versions are independent of imported device revisions");

        passes = { { .accesses = { Write(0, 0, 64) } }, { .accesses = { Write(0, 0, 64) } } };
        test.Check(compile().IsSuccess() && schedule.passOrder == std::vector<uint32_t>{ 1 }
            && schedule.culledPassCount == 1, "Final full overwrite does not keep a dead predecessor through WAW");
        passes[0].sideEffect = true;
        test.Check(compile().IsSuccess() && schedule.passOrder == std::vector<uint32_t>{ 0, 1 },
            "Upload side effects survive even when the final output overwrites their current contents");
        passes[0].sideEffect = false;
        passes[1].accesses[0].mode = RGAccessMode::ReadWrite;
        passes[1].accesses[0].initialization = RGWriteInitialization::Preserve;
        test.Check(compile().IsSuccess() && schedule.passOrder == std::vector<uint32_t>{ 0, 1 },
            "Attachment Load retains its prior producer through RAW");

        // 最终输出的两半分别属于不同写版本，不能只记整个 buffer 的最后写者。
        passes = { { .accesses = { Write(0, 0, 64) } }, { .accesses = { Write(0, 16, 16) } },
            { .accesses = { Read(0, 0, 64) }, .sideEffect = true } };
        test.Check(compile().IsSuccess() && HasEdge(schedule, 0, 2, RGDependencyKind::RAW)
            && HasEdge(schedule, 1, 2, RGDependencyKind::RAW), "Partial overwrite keeps producers for all uncovered ranges");
        size_t readFragments = 0;
        for (const auto& version : schedule.versions) if (version.pass == 2) ++readFragments;
        test.Check(readFragments == 3, "Range splitting records left, overwritten middle and right versions");
        passes = { { .accesses = { Write(0, 0, 32) } }, { .accesses = { Write(0, 32, 32) } } };
        test.Check(compile().IsSuccess() && schedule.dependencies.empty()
            && schedule.passOrder == std::vector<uint32_t>{ 0, 1 }, "Adjacent independent ranges do not gain false hazards");
        outputs = { Read(0, 32, 32) };
        test.Check(compile().IsSuccess() && schedule.passOrder == std::vector<uint32_t>{ 1 },
            "Exporting one range can cull a producer for a different range");
        outputs = { Read(0, 0, 64) };
        const std::array reverseIndependent{ RGDependency{ .before = 1, .after = 0 } };
        test.Check(compile(reverseIndependent).IsSuccess() && schedule.passOrder == std::vector<uint32_t>{ 1, 0 },
            "Explicit dependency reorders otherwise independent ranges");
        const std::array cycle{ RGDependency{ .before = 0, .after = 1 }, RGDependency{ .before = 1, .after = 0 } };
        test.Check(!compile(cycle).IsSuccess() && schedule.passOrder.empty() && schedule.versions.empty(),
            "Cycles fail without leaving a previously valid schedule");
        outputs.clear();
        test.Check(!compile(cycle).IsSuccess(), "Dead-pass elimination does not conceal a dependency cycle");

        resources = { { .size = 64, .initialized = { { .offset = 0, .size = 64 } } },
            { .size = 1, .initialized = { { .offset = 0, .size = 1 } } } };
        passes = { { .accesses = { Read(0, 0, 64) }, .sideEffect = true },
            { .accesses = { Write(0, 0, 64) } }, { .accesses = { Write(0, 0, 64) } },
            { .accesses = { Read(1, 0, 1) }, .sideEffect = true } };
        outputs = { Read(0, 0, 64) };
        const std::array delayedRead{ RGDependency{ .before = 3, .after = 0 } };
        test.Check(compile(delayedRead).IsSuccess() && schedule.passOrder == std::vector<uint32_t>{ 3, 0, 2 }
            && schedule.culledPassCount == 1, "Culled intermediate writer does not erase transitive WAR/WAW ordering");

        resources = { { .size = 64 } };
        outputs.clear();
        passes = { { .accesses = { Read(0, 0, 1) } } };
        test.Check(!compile().IsSuccess(), "Even an unused pass cannot hide an uninitialized read");
        resources[0].initialized = { { .offset = 0, .size = 8 }, { .offset = 16, .size = 8 } };
        passes[0].accesses = { Read(0, 0, 24) };
        test.Check(!compile().IsSuccess(), "Imported initialization holes are not accepted as fully initialized");
        resources[0].initialized = { { .offset = 0, .size = 64 } };
        test.Check(compile().IsSuccess() && schedule.passOrder.empty(), "Unused valid read-only passes are culled");
        outputs = { Read(0, 0, 64) };
        passes.clear();
        test.Check(compile().IsSuccess() && schedule.passOrder.empty(), "Initialized imported output needs no fabricated producer");

        // Discard 与 Load/采样的初始化语义按整纹理验证，不假定绘制覆盖每个像素。
        resources = { { .kind = RGResourceKind::Texture, .size = 1, .initialized = { { .offset = 0, .size = 1 } } } };
        passes = { { .accesses = { Write(0, 0, 1) } }, { .accesses = { Read(0, 0, 1) }, .sideEffect = true } };
        passes[0].accesses[0].initialization = RGWriteInitialization::Discard;
        outputs = { Read(0, 0, 1) };
        test.Check(!compile().IsSuccess(), "StoreDontCare invalidates later texture sampling or attachment Load");
        passes.pop_back();
        test.Check(!compile().IsSuccess(), "Discarded texture cannot become a valid export");
        passes[0].accesses[0].initialization = RGWriteInitialization::Initialize;
        test.Check(compile().IsSuccess() && schedule.passOrder == std::vector<uint32_t>{ 0 },
            "Full clear initializes the exported texture");

        resources = { { .size = 64, .initialized = { { .offset = 0, .size = 64 } } } };
        outputs.clear();
        passes = { { .accesses = { Read(0, 0, 16), Write(0, 8, 16) }, .sideEffect = true } };
        test.Check(!compile().IsSuccess(), "Overlapping pass-internal read/write requires a single ReadWrite declaration");
        passes[0].accesses = { Read(0, 0, 16), Write(0, 16, 16) };
        test.Check(compile().IsSuccess(), "Non-overlapping same-buffer copy is permitted");
        passes[0].accesses = { Read(0, 63, 2) };
        test.Check(!compile().IsSuccess(), "Out-of-bounds range is rejected before version construction");
        passes[0].accesses = { Read(0, std::numeric_limits<uint64_t>::max(), 2) };
        test.Check(!compile().IsSuccess(), "Overflowing range is rejected without wrapping its end");
        passes[0].accesses = { Read(0, 0, 0) };
        test.Check(!compile().IsSuccess(), "Zero-size dependency ranges are rejected");
        passes[0].accesses = { Read(1, 0, 1) };
        test.Check(!compile().IsSuccess(), "Invalid resource directory index is rejected");
        passes[0].accesses = { Read(0, 0, 1) };
        const std::array invalidEdge{ RGDependency{ .before = 0, .after = 1 } };
        test.Check(!compile(invalidEdge).IsSuccess(), "Explicit dependency must reference existing passes");
        const std::array forgedHazard{ RGDependency{ .before = 0, .after = 0, .kind = RGDependencyKind::RAW } };
        test.Check(!compile(forgedHazard).IsSuccess(), "Callers cannot forge compiler-generated hazard edges");
        resources[0].kind = RGResourceKind::Texture;
        test.Check(compile().IsSuccess(), "Texture declarations support explicitly initialized mip/layer subresource index ranges");
        resources = { { .size = 64, .initialized = { { .offset = 63, .size = 2 } } } };
        test.Check(!compile().IsSuccess(), "Malformed imported initialized ranges are rejected");
    }
}
