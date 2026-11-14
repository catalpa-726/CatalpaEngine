#include "catpch.h"

#include "TestRunner.h"
#include "Assets/AssetTestFixture.h"

namespace Catalpa::Tests {
	void RunAssetChangeTests(TestContext& test) {
		JobSystem jobs;
		std::string error;
		if (!jobs.Start({1, 8}, error)) {
			test.Check(false, error);
			return;
		}
		{
			AssetManager assets(jobs, {}, {}, AssetTest::Config());
			test.Check(assets.RegisterLoader(AssetTest::Loader(), error), "Journal uses a real registered asset type");
			const auto id = AssetTest::Id(17001);
			const auto publish = [&](uint64_t version) {
				return assets.PublishBuiltin(id, version,
				    std::make_shared<const AssetTest::Value>(
				        AssetTest::Value{uint32_t(version), std::this_thread::get_id()}),
				    sizeof(AssetTest::Value), error);
			};
			uint64_t first = 0, second = 0;
			std::vector<AssetChangeEvent> changes;
			bool reset = false;
			test.Check(publish(1) && publish(2) && assets.ReadChanges(first, changes, reset, error) && !reset &&
			        changes.size() == 2 && changes.back().previousVersion == 1 && changes.back().version == 2,
			    "Published changes contain precise old/new versions");
			test.Check(assets.ReadChanges(second, changes, reset, error) && changes.size() == 2 && second == first,
			    "Independent consumers do not drain each other's change journal");
			test.Check(!publish(2) && assets.ReadChanges(first, changes, reset, error) && changes.empty(),
			    "Rejected replacement does not emit a successful publication event");
			AssetDependencyValue fixed;
			test.Check(assets.GetDependencySnapshot(id, fixed, error) && publish(3) && fixed.version == 2 &&
			        fixed.cppType == typeid(AssetTest::Value) &&
			        std::static_pointer_cast<const AssetTest::Value>(fixed.value)->number == 2,
			    "Author dependency snapshot retains its precise version after publication changes");
			const auto previous = fixed.value;
			test.Check(!assets.GetDependencySnapshot(AssetTest::Id(17002), fixed, error) && fixed.value == previous,
			    "Untyped dependency pin never substitutes a fallback or overwrites output on failure");
			for (uint64_t version = 4; version <= 1027; ++version)
				if (!publish(version)) {
					test.Check(false, error);
					break;
				}
			test.Check(assets.ReadChanges(second, changes, reset, error) && reset && changes.size() == 1024 &&
			        changes.back().version == 1027,
			    "Slow consumer explicitly detects bounded history overflow");
			assets.Shutdown();
		}
		jobs.Shutdown();
	}
} /* 命名空间 Catalpa::Tests */
