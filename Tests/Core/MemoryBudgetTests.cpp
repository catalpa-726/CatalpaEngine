#include "catpch.h"

#include "TestRunner.h"
#include "Core/Memory/MemoryBudget.h"
#include "Core/Jobs/JobSystem.h"

namespace Catalpa::Tests {
	void RunMemoryBudgetTests(TestContext& context) {
		MemoryBudget budget(100);
		MemoryReservation first, second;
		context.Check(budget.TryReserve(60, first) && first.Bytes() == 60, "Budget charges an actual reservation");
		context.Check(!budget.TryReserve(41, second) && second.Bytes() == 0 && budget.GetStats().used == 60,
		    "Over-limit request is rejected without modifying output");
		context.Check(budget.TryReserve(40, second) && budget.GetStats().peak == 100, "Exact limit is accepted");
		context.Check(!first.Shrink(61) && first.Bytes() == 60, "Reservation cannot grow by shrinking");
		context.Check(first.Shrink(20) && budget.GetStats().used == 60, "Decode releases unused transient allowance");
		MemoryReservation moved(std::move(first));
		context.Check(
		    first.Bytes() == 0 && moved.Bytes() == 20 && budget.GetStats().used == 60, "Move transfers one charge");
		second = {};
		moved = {};
		context.Check(budget.GetStats().used == 0 && budget.GetStats().rejected == 1,
		    "Last reservation release returns all bytes");
		context.Check(
		    !budget.TryReserve(std::numeric_limits<size_t>::max(), first), "Reservation rejects overflowing size");
		MemoryReservation surviving;
		{
			MemoryBudget owner(12);
			context.Check(owner.TryReserve(12, surviving), "Reservation may outlive budget facade");
		}
		context.Check(surviving.Shrink(0), "Detached reservation keeps its accounting owner alive");
		std::string error;
		JobSystem jobs;
		context.Check(!jobs.Start({0, 4}, error), "Scheduler requires explicit positive worker count");
		if (!jobs.Start({2, 4}, error)) {
			context.Check(false, error);
			return;
		}
		MemoryBudget concurrent(64);
		std::atomic_uint count{0};
		std::atomic_bool release{false};
		std::array<JobHandle, 2> handles;
		for (auto& handle : handles)
			context.Check(jobs.Submit(
			                  [&](const CancellationToken& token) {
				                  MemoryReservation bytes;
				                  if (concurrent.TryReserve(32, bytes))
					                  count.fetch_add(1, std::memory_order_release);
				                  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
				                  while (!release.load(std::memory_order_acquire) && !token.IsCancellationRequested() &&
				                      std::chrono::steady_clock::now() < deadline)
					                  std::this_thread::yield();
			                  },
			                  handle, error),
			    "Submit concurrent accounting through the single jobs scheduler");
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (count.load(std::memory_order_acquire) != 2 && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		context.Check(count == 2 && concurrent.GetStats().used == 64,
		    "Concurrent reservations reach, but do not exceed, the limit");
		context.Check(!concurrent.TryReserve(1, first), "Concurrent capacity blocks another owner");
		release.store(true, std::memory_order_release);
		for (const auto& handle : handles)
			context.Check(
			    jobs.Wait(handle, error) && jobs.IsComplete(handle), "Wait includes callback and scheduler completion");
		context.Check(concurrent.GetStats().used == 0, "Worker completion returns transient reservations");
		JobHandle throwing;
		context.Check(
		    jobs.Submit([](const CancellationToken&) { throw std::runtime_error("controlled worker failure"); },
		        throwing, error),
		    "Submit diagnostic failure");
		context.Check(!jobs.Wait(throwing, error) && error == "controlled worker failure",
		    "Worker exception becomes a CPU job diagnostic");
		std::atomic_bool started{false}, cancelled{false};
		JobHandle stopping;
		context.Check(jobs.Submit(
		                  [&](const CancellationToken& token) {
			                  started.store(true, std::memory_order_release);
			                  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
			                  while (!token.IsCancellationRequested() && std::chrono::steady_clock::now() < until)
				                  std::this_thread::yield();
			                  cancelled.store(token.IsCancellationRequested(), std::memory_order_release);
		                  },
		                  stopping, error),
		    "Submit cooperative stop task");
		jobs.Shutdown();
		context.Check(started && cancelled && stopping.IsComplete() && !jobs.IsRunning(),
		    "Shutdown cancels and joins worker callbacks before destroying scheduler storage");
		JobHandle rejected;
		context.Check(
		    !jobs.Submit([](const CancellationToken&) {}, rejected, error), "Stopped scheduler rejects submission");
	}
}
