#include "catpch.h"
#include "TestRunner.h"
#include "RHI/RHICompletionSet.h"

namespace Catalpa::Tests {

    // 纯 CPU 完成要求集合；不初始化设备，不把合并成功当成 GPU 已完成。
    void RunCompletionTests(TestContext& test)
    {
        const RHICompletionPoint first{ .device = { 11 }, .queue = { 1 }, .sequence = 3 };
        RHICompletionSet completions;
        test.Check(completions.GetPoints().empty(), "A new completion set has no last-use requirements");
        test.Check(!completions.TryInclude(first) && completions.GetPoints().empty(),
            "An unreserved set rejects a new queue without allocating or changing state");
        test.Check(completions.Reserve(3).IsSuccess(), "Queue identity storage can be reserved before submission");
        const bool inserted = completions.TryInclude(first);
        test.Check(inserted && completions.GetPoints().size() == 1 && completions.GetPoints()[0] == first,
            "An accepted submission can be recorded in reserved storage");
        if (!inserted || completions.GetPoints().size() != 1) return;
        const auto* storage = completions.GetPoints().data();

        auto later = first;
        later.sequence = 9;
        test.Check(completions.TryInclude(later) && completions.GetPoints().size() == 1
            && completions.GetPoints()[0].sequence == 9,
            "The same device and queue merge to the latest whole-submission sequence");
        test.Check(completions.TryInclude(first) && completions.GetPoints()[0].sequence == 9,
            "An older last-use point cannot move completion requirements backwards");
        test.Check(completions.TryInclude(later) && completions.GetPoints().size() == 1,
            "Repeated last-use points do not create duplicate queue requirements");

        auto otherQueue = first;
        otherQueue.queue = { 2 };
        otherQueue.sequence = 2;
        test.Check(completions.TryInclude(otherQueue) && completions.GetPoints().size() == 2
            && completions.GetPoints()[0].sequence == 9 && completions.GetPoints()[1] == otherQueue,
            "Independent queue identities retain independent sequence requirements");
        auto nextDevice = first;
        nextDevice.device = { 12 };
        nextDevice.sequence = 1;
        test.Check(completions.TryInclude(nextDevice) && completions.GetPoints().size() == 3
            && completions.GetPoints()[2] == nextDevice,
            "A new device epoch cannot satisfy or replace the previous epoch requirement");
        test.Check(completions.GetPoints().data() == storage,
            "Recording the reserved queue identities and merging sequences does not move storage");

        const std::array<RHICompletionPoint, 3> expected{ later, otherQueue, nextDevice };
        const auto unchanged = [&]() {
            return std::ranges::equal(completions.GetPoints(), expected)
                && completions.GetPoints().data() == storage;
        };
        auto invalid = later;
        invalid.sequence = 0;
        test.Check(!completions.TryInclude(invalid) && unchanged(),
            "Zero sequence is rejected without disturbing existing last-use requirements");
        invalid = later; invalid.device = {};
        test.Check(!completions.TryInclude(invalid) && unchanged(),
            "Missing device epoch is rejected without changing the collection");
        invalid = later; invalid.queue = {};
        test.Check(!completions.TryInclude(invalid) && unchanged(),
            "Missing queue identity is rejected without changing the collection");
        test.Check(completions.Reserve(std::numeric_limits<size_t>::max()).code == RHIErrorCode::OutOfMemory
            && unchanged(), "Impossible reservation reports failure and preserves existing requirements");

        // 合并只比较序列，不进行加法；最大值既不回绕，也不会生成下一个提交编号。
        later.sequence = std::numeric_limits<uint64_t>::max();
        test.Check(completions.TryInclude(later) && completions.GetPoints()[0] == later,
            "The maximum sequence remains representable without arithmetic overflow");
        test.Check(completions.TryInclude(first) && completions.GetPoints()[0] == later,
            "A maximum requirement is not lost when an earlier sequence is included");

        completions.Clear();
        test.Check(completions.GetPoints().empty(), "Clear removes CPU requirements without claiming GPU completion");
        test.Check(completions.TryInclude(nextDevice) && completions.TryInclude(otherQueue)
            && completions.TryInclude(first) && completions.GetPoints().data() == storage,
            "Clearing preserves the reserved capacity for later frame bookkeeping");

        // 不同输入顺序仍得到同一个 per-queue 最大值；集合顺序本身不用于提交排序。
        RHICompletionSet reverse;
        test.Check(reverse.Reserve(1).IsSuccess() && reverse.TryInclude(later) && reverse.TryInclude(first)
            && reverse.GetPoints().size() == 1 && reverse.GetPoints()[0] == later,
            "Last-use merging is independent of chronological insertion order");
    }

}
