#include "catpch.h"

#include "Assets/AssetTestFixture.h"
#include "Assets/AssetManifest.h"
#include "Core/Serialization/JsonArchive.h"

namespace Catalpa::Tests {
	namespace {
		/**
		 * @brief 验证运行时资产容器的读取、写入及无效输入处理。
		 *
		 * @param[in,out] context 用于记录断言、失败原因和检查数量的测试上下文。
		 */
		void ContainerContracts(TestContext& context) {
			using namespace AssetTest;
			std::string error, hash;
			const std::string abc = "abc";
			context.Check(ComputeAssetContentHash(std::as_bytes(std::span(abc.data(), abc.size())), hash, error) &&
			        hash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
			    "Container hashing uses the known SHA-256 abc vector");
			auto original = Container(Id(1), 42);
			std::vector<std::byte> encoded;
			context.Check(WriteAssetContainer(original, encoded, error), "Write bounded canonical asset container");
			AssetContainer decoded;
			context.Check(AssetContainerReader::Read(encoded, decoded, error) && decoded.id == original.id &&
			        decoded.payload == original.payload,
			    "Container round trip preserves ID, version and payload");
			const auto preserved = decoded.payload;
			auto bad = encoded;
			bad.back() ^= std::byte{1};
			context.Check(!AssetContainerReader::Read(bad, decoded, error) && decoded.payload == preserved,
			    "Payload corruption is rejected without replacing old output");
			for (size_t cut : {size_t(0), size_t(7), size_t(16), encoded.size() - 1})
				context.Check(!AssetContainerReader::Read(std::span(encoded).first(cut), decoded, error),
				    "Truncated container is rejected");
			bad = encoded;
			bad[0] = std::byte{0};
			context.Check(!AssetContainerReader::Read(bad, decoded, error), "Wrong container magic is rejected");
			bad = encoded;
			bad[8] = std::byte{2};
			context.Check(!AssetContainerReader::Read(bad, decoded, error), "Unknown container version is rejected");
			bad = encoded;
			bad[12] = std::byte{0};
			context.Check(!AssetContainerReader::Read(bad, decoded, error), "Wrong byte-order marker is rejected");
			bad = encoded;
			bad.push_back(std::byte{0});
			context.Check(!AssetContainerReader::Read(bad, decoded, error), "Trailing block bytes are rejected");
			context.Check(
			    !AssetContainerReader::Read(encoded, decoded, error, 3), "Payload byte cap is enforced before copy");
			std::vector<std::byte> unchanged{std::byte{7}};
			context.Check(!WriteAssetContainer(original, unchanged, error, 8) &&
			        unchanged == std::vector<std::byte>{std::byte{7}},
			    "Writer limit preserves prior output");
			original.dependencies = {{original.id, 1, hash}};
			context.Check(!WriteAssetContainer(original, unchanged, error), "Self dependency is rejected");
			original.dependencies = {{Id(2), 1, hash}, {Id(2), 1, hash}};
			context.Check(!WriteAssetContainer(original, unchanged, error), "Duplicate dependency is rejected");
			original.dependencies = {{Id(2), 0, hash}};
			context.Check(!WriteAssetContainer(original, unchanged, error), "Zero dependency version is rejected");
			original.dependencies = {{Id(2), 1, "pretend-hash"}};
			context.Check(!WriteAssetContainer(original, unchanged, error), "Malformed dependency digest is rejected");
			BinaryWriter little;
			little.U32(0x12345678);
			little.U64(0x0102030405060708ULL);
			little.F32(-1.25f);
			context.Check(little.Data()[0] == std::byte{0x78} && little.Data()[3] == std::byte{0x12},
			    "Binary archive emits explicit little endian words");
			BinaryReader reader(little.Data());
			uint32_t u32 = 0;
			uint64_t u64 = 0;
			float number = 0;
			context.Check(reader.U32(u32) && reader.U64(u64) && reader.F32(number) && reader.AtEnd() &&
			        u32 == 0x12345678 && u64 == 0x0102030405060708ULL && number == -1.25f,
			    "Binary archive round trip does not serialize C++ layout");
			u32 = 19;
			context.Check(!reader.U32(u32) && u32 == 19, "Truncated scalar read preserves output");
		}
	}

	void RunAssetLifecycleTests(TestContext& context) {
		using namespace AssetTest;
		ContainerContracts(context);
		std::string error;
		Fixture fixture;
		if (!fixture.Create(error)) {
			context.Check(false, error);
			return;
		}
		auto v1 = Container(Id(10), 10, 100);
		AssetRecord record;
		if (!fixture.Save(v1, record, error)) {
			context.Check(false, error);
			return;
		}
		VirtualFileSystem files;
		AssetRegistry registry;
		if (!fixture.Mount(files, error) || !registry.Replace({record}, error)) {
			context.Check(false, error);
			return;
		}
		std::vector<std::byte> manifest;
		AssetRegistry restored;
		context.Check(AssetManifest::Write(registry, manifest, error) &&
		        AssetManifest::Read(manifest, restored, error) && restored.Find(record.id) &&
		        restored.Find(record.id)->contentHash == record.contentHash,
		    "Cooked manifest round trip preserves real complete-container digest");
		manifest.push_back(std::byte{0});
		context.Check(!AssetManifest::Read(manifest, restored, error) && restored.Find(record.id),
		    "Manifest trailing bytes preserve old registry");
		auto missingRecord = record;
		missingRecord.dependencies = {Id(999)};
		AssetRegistry missing;
		context.Check(missing.Replace({missingRecord}, error) && !AssetManifest::Write(missing, manifest, error),
		    "Runtime manifest rejects unresolved dependencies");
		auto sourceRecord = record;
		sourceRecord.locator.kind = AssetLocationKind::Source;
		AssetRegistry source;
		context.Check(source.Replace({sourceRecord}, error) && !AssetManifest::Write(source, manifest, error),
		    "Runtime manifest rejects author source locators");
		JobSystem jobs;
		if (!jobs.Start({2, 16}, error)) {
			context.Check(false, error);
			return;
		}
		AssetManager manager(jobs, files, registry, Config());
		context.Check(manager.RegisterLoader(Loader(), error), "Register typed CPU decoder");
		context.Check(!manager.RegisterLoader(Loader(), error), "Duplicate loader type is rejected");
		context.Check(manager.Request(record.id, error) && Await(manager, record.id),
		    "Cooked asset reaches terminal state asynchronously");
		context.Check(manager.GetStatus(record.id) == AssetStatus::Ready, manager.GetDiagnostic(record.id));
		AssetVersionRef<Value> first;
		bool fallback = false;
		context.Check(manager.Acquire(record.id, first, fallback, error) && !fallback && first.GetVersion() == 100 &&
		        first.Get()->number == 10,
		    "Acquire fixes exact ready content version");
		if (!first.HasValue())
			return;
		context.Check(first.Get()->decodedOn != std::this_thread::get_id(), "Decode actually runs on an enkiTS worker");
		const size_t single = manager.GetBudgetStats().used;
		auto v2 = Container(record.id, 20, 2);
		AssetRecord next;
		context.Check(fixture.Save(v2, next, error), "Create another cooked version in exclusive fixture");
		AssetRegistry replacement;
		context.Check(replacement.Replace({next}, error), "Prepare replacement registry");
		context.Check(
		    manager.ReplaceRegistry(replacement, error) && manager.Request(next.id, error) && Await(manager, next.id),
		    "Opaque content version may decrease numerically while request generation advances");
		AssetVersionRef<Value> second;
		context.Check(manager.Acquire(next.id, second, fallback, error) && second.GetVersion() == 2 &&
		        second.Get()->number == 20 && first.Get()->number == 10,
		    "Reload publishes new version while old reference remains unchanged");
		context.Check(manager.GetBudgetStats().used == single * 2, "Retired referenced payload remains charged");
		first = {};
		context.Check(manager.GetBudgetStats().used == single, "Dropping old reference releases only its old version");
		auto inconsistent = Container(record.id, 30, 2);
		AssetRecord reused;
		context.Check(fixture.Save(inconsistent, reused, error), "Write same-version changed-content negative fixture");
		AssetRegistry reusedRegistry;
		context.Check(reusedRegistry.Replace({reused}, error), "Prepare reused content key negative registry");
		context.Check(!manager.ReplaceRegistry(reusedRegistry, error),
		    "Same version cannot describe a different verified digest");
		context.Check(manager.Request(next.id, error, true) && Await(manager, next.id) &&
		        manager.GetStatus(next.id) == AssetStatus::Failed,
		    "Registry digest catches changed container bytes on forced reload");
		AssetVersionRef<Value> retained;
		context.Check(
		    manager.Acquire(next.id, retained, fallback, error) && !fallback && retained.Get() == second.Get(),
		    "Failed replacement preserves the last known-good immutable payload");
		manager.Unload(next.id);
		context.Check(manager.GetBudgetStats().used == single && second.Get()->number == 20,
		    "Unload never destroys external frame/version ownership");
		context.Check(!manager.ReplaceRegistry(reusedRegistry, error),
		    "Unload cannot forget an already published content key and digest");
		AssetRecord historicalCollision;
		context.Check(fixture.Save(Container(record.id, 40, 100), historicalCollision, error),
		    "Create old content-version reuse fixture");
		AssetRegistry historicalRegistry;
		context.Check(historicalRegistry.Replace({historicalCollision}, error) &&
		        !manager.ReplaceRegistry(historicalRegistry, error),
		    "Returning to an earlier opaque content key cannot change its original bytes");
		historicalCollision.contentHash.clear();
		context.Check(historicalRegistry.Replace({historicalCollision}, error) &&
		        manager.ReplaceRegistry(historicalRegistry, error) && manager.Request(record.id, error) &&
		        Await(manager, record.id) && manager.GetStatus(record.id) == AssetStatus::Failed,
		    "Omitting registry digest cannot bypass actual-byte identity history verification");
		retained = {};
		second = {};
		context.Check(
		    manager.GetBudgetStats().used == 0, "Unloaded version releases budget after last external reference");
		context.Check(!manager.Request(Id(999), error), "Missing asset request reports failure");
		/* 故意延迟旧解码器，使新一代资源先就绪，再让旧作业返回。 */
		auto gate = std::make_shared<Gate>();
		AssetManager delayed(jobs, files, {}, Config());
		context.Check(delayed.RegisterLoader(Loader([gate](uint32_t number, const CancellationToken&) {
			if (number == 1)
				gate->Enter();
		}),
		                  error),
		    "Register controlled late decoder");
		AssetRecord lateOld, lateNew;
		context.Check(fixture.Save(Container(Id(20), 1, 1), lateOld, error), "Write delayed old version");
		AssetRegistry oldRegistry;
		context.Check(oldRegistry.Replace({lateOld}, error) && delayed.ReplaceRegistry(oldRegistry, error) &&
		        delayed.Request(lateOld.id, error),
		    "Schedule delayed generation");
		context.Check(gate->AwaitEntry(), "Old worker entered decode before cancellation");
		delayed.Cancel(lateOld.id);
		context.Check(fixture.Save(Container(Id(20), 2, 2), lateNew, error), "Write newer generation fixture");
		AssetRegistry newRegistry;
		context.Check(newRegistry.Replace({lateNew}, error) && delayed.ReplaceRegistry(newRegistry, error) &&
		        delayed.Request(lateNew.id, error) && Await(delayed, lateNew.id),
		    "Schedule new generation without waiting for cancelled worker");
		gate->release.store(true, std::memory_order_release);
		context.Check(Drain(delayed), "Drain late cancelled result");
		AssetVersionRef<Value> newest;
		context.Check(delayed.Acquire(lateNew.id, newest, fallback, error) && newest.GetVersion() == 2 &&
		        newest.Get()->number == 2,
		    "Late old generation cannot overwrite newer publication");
		delayed.Shutdown();
		context.Check(newest.HasValue() && newest.Get()->number == 2, "Shutdown retains externally owned version data");
		newest = {};
		context.Check(
		    delayed.GetBudgetStats().used == 0, "Final external release after shutdown returns managed bytes");
		/* 此夹具仅测试 JSON 的传递，场景语义由 S10 测试覆盖。 */
		const auto badScene =
		    std::filesystem::path(__FILE__).parent_path().parent_path() / "Fixtures" / "InvalidScene.scene.json";
		std::ifstream input(badScene, std::ios::binary);
		std::ostringstream contents;
		contents << input.rdbuf();
		context.Check(input.is_open(), "Malformed scene JSON fixture is available read-only");
		if (input.is_open()) {
			std::unique_ptr<ArchiveValue> value;
			ArchiveError diagnostic;
			context.Check(!JsonArchive::Parse(contents.str(), value, diagnostic),
			    "Malformed scene fixture is rejected at JSON layer, without claiming scene loading");
		}
	}
}
