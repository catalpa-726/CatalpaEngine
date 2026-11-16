#include "catpch.h"

#include "TestRunner.h"
#include "Assets/AssetTestFixture.h"
#include "Assets/PackageReader.h"
#include "Assets/AssetCompression.h"
#include "Packager/PackageBuilder.h"
#include "Delivery/GameExport.h"
#include "Commands/ToolCommandRunner.h"
#include "Runtime/PackageSession.h"
#include "Scene/SceneSerializer.h"
#include "Scene/SceneAssetLoader.h"
#include "Core/Localization/LocalizationCatalog.h"
#include "Core/IO/FileWatcher.h"
#include "Networking/Backends/Loopback/LoopbackTransport.h"
#include "Networking/NetworkSession.h"
#include "UI/FontAsset.h"
#include "ECS/SystemScheduler.h"
#include "ECS/SystemContext.h"
#include "Runtime/RuntimeServices.h"
#include "Scene/Scene.h"

namespace Catalpa::Tests {
	namespace {
		/** @brief 用只能同时容纳一个请求的管理器装载宽依赖包，防止把在途临时预算误判为内容太大。 */
		void CheckWidePackage(TestContext& test, AssetTest::Fixture& fixture, JobSystem& jobs, AssetContainer scene) {
			std::string error;
			std::vector<AssetRecord> records;
			for (uint64_t i = 0; i < 4; ++i) {
				AssetRecord record;
				if (!fixture.Save(AssetTest::Container(AssetTest::Id(950 + i), 7), record, error)) {
					test.Check(false, error);
					return;
				}
				scene.dependencies.push_back({record.id, record.version, record.contentHash});
				records.push_back(std::move(record));
			}
			scene.id = AssetTest::Id(970);
			AssetRecord parent;
			if (!fixture.Save(scene, parent, error)) {
				test.Check(false, error);
				return;
			}
			records.push_back(parent);
			const auto file = fixture.root / "wide.catpkg";
			fixture.files.push_back(file);
			AssetRegistry registry;
			VirtualFileSystem files;
			std::shared_ptr<const PackageReader> package;
			if (!fixture.Mount(files, error) || !registry.Replace(records, error) ||
			    !Tools::PackageBuilder::Build(registry, files, parent.id, file, {}, error) ||
			    !PackageReader::Open(file, package, error)) {
				test.Check(false, error);
				return;
			}
			auto config = AssetTest::Config();
			config.maxPending = 1;
			config.memoryBudgetBytes = 24 * 1024;
			AssetManager assets(jobs, {}, {}, config);
			struct EntryProbe {};
			AssetLoader loader;
			loader.type = parent.type;
			loader.cppType = typeid(EntryProbe);
			loader.decode = [](const AssetContainer&, const AssetDecodeContext& context, AssetLoadResult& out,
			                    std::string& e) {
				if (context.dependencies.size() != 4) {
					e = "Missing fixed children";
					return false;
				}
				out = MakeAssetLoadResult<EntryProbe>(std::make_shared<EntryProbe>(), sizeof(EntryProbe));
				return true;
			};
			bool ok = assets.RegisterLoader(AssetTest::Loader(), error) &&
			    assets.RegisterLoader(std::move(loader), error) && assets.InstallPackage(package, error);
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (ok && assets.GetStatus(parent.id) != AssetStatus::Ready &&
			    std::chrono::steady_clock::now() < deadline) {
				ok = assets.AdvancePackageLoad(error);
				assets.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			test.Check(ok && assets.GetStatus(parent.id) == AssetStatus::Ready,
			    "Wide package reaches entry with one pending slot");
			test.Check(assets.UninstallPackage(error), "Wide package releases its mount");
			assets.Shutdown();
		}
	}
	void RunDeliveryContractsTests(TestContext& test) {
		std::string error;
		AssetContainer raw = AssetTest::Container(AssetTest::Id(900), 7);
		raw.payload.assign(16384, std::byte{42});
		std::vector<std::byte> compressed;
		AssetContainer decoded;
		test.Check(WriteCompressedAssetContainer(raw, compressed, error) && compressed.size() < 16384 &&
		        AssetContainerReader::Read(compressed, decoded, error) && decoded.payload == raw.payload,
		    "zstd wrapper roundtrips exact payload bytes");
		const auto good = compressed;
		test.Check(
		    !AssetContainerReader::Read(good, decoded, error, 100), "Decompression obeys the decoded payload budget");
		compressed.back() ^= std::byte{1};
		test.Check(!AssetContainerReader::Read(compressed, decoded, error) && decoded.payload == raw.payload,
		    "Corrupt compressed input retains previous decoded output");
		compressed = good;
		compressed.push_back(std::byte{0});
		test.Check(!AssetContainerReader::Read(compressed, decoded, error), "Compressed trailing data is rejected");

		LocalizationCatalog catalog;
		test.Check(catalog.Load(R"({"language":"zh-CN","strings":{"panel.scene":"场景"}})", "zh-CN", error) &&
		        std::string_view(catalog.Text("panel.scene", "Scene")) == "场景",
		    "Catalog accepts UTF-8 Chinese without changing keys");
		test.Check(std::string_view(catalog.Text("missing", "Fallback")) == "Fallback",
		    "Missing translation uses caller's fallback");
		test.Check(!catalog.Load(R"({"language":"zh-CN","strings":{"panel.scene":"bad##id"}})", "zh-CN", error) &&
		        std::string_view(catalog.Text("panel.scene", "Scene")) == "场景",
		    "Rejected catalog retains previous language and blocks ImGui ID injection");
		test.Check(!catalog.Load(R"({"language":"en-US","strings":{"a":"one","a":"two"}})", "en-US", error),
		    "Duplicate language keys are rejected");
		test.Check(!catalog.Load(R"({"language":"en-US","strings":{"a":"\uD800"}})", "en-US", error),
		    "Unpaired Unicode surrogate is rejected");
		std::string family = "unchanged";
		uint32_t glyphs = 123;
		test.Check(!ValidateFontBytes(std::span<const std::byte>{good.data(), 16}, family, glyphs, error) &&
		        family == "unchanged" && glyphs == 123,
		    "Malformed font preserves existing metadata");

		AssetTest::Fixture fixture;
		if (!fixture.Create(error)) {
			test.Check(false, error);
			return;
		}
		Tools::GameExportRequest exportRequest;
		exportRequest.output = fixture.root / "NotPublished";
		exportRequest.runtimeTemplate = fixture.root / "MissingRuntimeTemplate";
		test.Check(!Tools::ExportWindowsGame(exportRequest, error) &&
		        error.find("runtime-template.json") != std::string::npos &&
		        !std::filesystem::exists(exportRequest.output),
		    "Export identifies a missing template without creating a launchable output");
		CancellationSource cancelled;
		cancelled.Cancel();
		exportRequest.cancellation = cancelled.Token();
		test.Check(!Tools::ExportWindowsGame(exportRequest, error) && error.find("cancelled") != std::string::npos &&
		        !std::filesystem::exists(exportRequest.output),
		    "Cancelled export preserves the selected directory");
		SceneAsset scene;
		scene.name = "Package scene";
		SceneEntityAsset root;
		root.id = PersistentEntityId::FromWords(1, 1);
		root.name = "Root";
		scene.entities.push_back(root);
		std::string sceneJson;
		if (!SceneSerializer::Write(scene, sceneJson, error)) {
			test.Check(false, error);
			return;
		}
		AssetContainer source;
		source.id = AssetTest::Id(901);
		source.version = 1;
		AssetType::TryParse("scene", source.type);
		const auto payload = std::as_bytes(std::span(sceneJson.data(), sceneJson.size()));
		source.payload.assign(payload.begin(), payload.end());
		AssetRecord record;
		if (!fixture.Save(source, record, error)) {
			test.Check(false, error);
			return;
		}
		VirtualFileSystem files;
		AssetRegistry registry;
		test.Check(files.MountReadOnly(record.locator.path.MountName(), fixture.root, 0, error) &&
		        registry.Replace({record}, error),
		    "Explicit source mount and catalog are accepted");
		const auto packagePath = fixture.root / "runtime.catpkg";
		fixture.files.push_back(packagePath);
		test.Check(Tools::PackageBuilder::Build(registry, files, record.id, packagePath, {}, error),
		    "Package closes and validates all entry dependencies");
		const auto commandPackage = fixture.root / "command.catpkg";
		const auto commandManifest = fixture.root / "Command.manifest";
		fixture.files.push_back(commandPackage);
		fixture.files.push_back(commandManifest);
		std::vector<std::byte> manifestBytes;
		if (!AssetManifest::Write(registry, manifestBytes, error)) {
			test.Check(false, error);
			return;
		}
		{
			std::ofstream manifest(commandManifest, std::ios::binary);
			manifest.write(reinterpret_cast<const char*>(manifestBytes.data()), std::streamsize(manifestBytes.size()));
			if (!manifest) {
				test.Check(false, "Write isolated command manifest");
				return;
			}
		}
		const auto pathText = [](const std::filesystem::path& file) {
			const auto text = file.generic_u8string();
			return std::string(reinterpret_cast<const char*>(text.data()), text.size());
		};
		const std::vector<std::string> command{"--package", "--manifest", pathText(commandManifest), "--entry",
		    record.id.ToString(), "--output", pathText(commandPackage)};
		test.Check(Tools::ToolCommandRunner::Run(command).exitCode == 0,
		    "Package command resolves the explicit manifest root without source-directory or "
		    "mount-name knowledge");
		std::shared_ptr<const PackageReader> package;
		if (!PackageReader::Open(packagePath, package, error)) {
			test.Check(false, error);
			return;
		}
		test.Check(package->EntryScene() == record.id && package->Catalog().GetRecords().size() == 1,
		    "Package preserves scene identity and exact catalog");
		VirtualFileSystem mounted;
		test.Check(mounted.MountPackage(package, error), "Package mounts without source paths or extraction");
		std::vector<std::byte> read;
		test.Check(mounted.ReadFile(package->Catalog().Find(record.id)->locator.path, 4096, read, error),
		    "VFS reads immutable package bytes");
		test.Check(
		    !mounted.MountReadOnly("package", fixture.root, 0, error), "Directory cannot shadow a package namespace");
		auto prior = package;
		std::ifstream stream(packagePath, std::ios::binary | std::ios::ate);
		std::vector<std::byte> bytes(static_cast<size_t>(stream.tellg()));
		stream.seekg(0);
		stream.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
		stream.close();
		auto corrupt = bytes;
		corrupt.back() ^= std::byte{1};
		test.Check(!PackageReader::Decode(std::move(corrupt), package, error) && package == prior,
		    "Corrupt package cannot replace a valid snapshot");
		corrupt = bytes;
		corrupt.push_back(std::byte{0});
		test.Check(!PackageReader::Decode(std::move(corrupt), package, error), "Package rejects trailing bytes");
		test.Check(!PackageReader::Decode(bytes, package, error, {32, 4096, 8}),
		    "Package rejects total byte budget before parsing");

		JobSystem jobs;
		if (!jobs.Start({1, 8}, error)) {
			test.Check(false, error);
			return;
		}
		{
			AssetManager assets(jobs, {}, {}, AssetTest::Config());
			SceneManager scenes;
			test.Check(RegisterSceneAssetLoader(assets, error), "Package session uses existing scene loader");
			PackageSession session;
			test.Check(session.Start(prior, assets, scenes, error),
			    "Package session starts without a device or a second asset manager");
			test.Check(AssetTest::Await(assets, record.id), "Package scene loading settles");
			test.Check(session.Pump(assets, scenes, error) && session.State() == PackageSessionState::Running &&
			        scenes.GetActive() && scenes.GetActive()->GetName() == scene.name,
			    "Runtime instantiates and activates the packaged scene");
			test.Check(session.Stop(assets, scenes, error) && !scenes.GetActive() &&
			        session.State() == PackageSessionState::Idle,
			    "Stopping releases only the session scene and restores mounts");
			assets.Shutdown();
		}
		CheckWidePackage(test, fixture, jobs, source);
		jobs.Shutdown();

		FileWatcher watcher;
		const auto watched = fixture.files.front();
		test.Check(watcher.SetPaths(std::span(&watched, 1), error), "Watcher observes explicit absolute inputs");
		{
			std::ofstream append(watched, std::ios::binary | std::ios::app);
			append.put('x');
		}
		std::vector<std::filesystem::path> changed;
		test.Check(watcher.Poll(changed, error) && changed.empty(), "Watcher defers changes until the quiet window");
		std::this_thread::sleep_for(std::chrono::milliseconds(800));
		test.Check(watcher.Poll(changed, error) && changed.size() == 1, "Watcher coalesces the settled file change");
		watcher.Clear();
	}

	void RunLoopbackContractsTests(TestContext& test) {
		std::string error;
		std::unique_ptr<INetworkTransport> a, b;
		test.Check(!CreateLoopbackPair(a, a, error), "Loopback rejects aliased output owners");
		if (!CreateLoopbackPair(a, b, error, {64 * 1024, 64 * 1024, 1})) {
			test.Check(false, error);
			return;
		}
		NetworkSession first(std::move(a), 17), second(std::move(b), 17);
		const std::array<std::byte, 2> data{std::byte{7}, std::byte{9}};
		test.Check(first.Send(1, data, error), "Loopback sends an owned versioned message");
		test.Check(!first.Send(2, data, error), "Full mailbox reports backpressure without advancing sequence");
		NetworkMessage message;
		bool available = false;
		uint32_t type = 0;
		test.Check(second.Receive(type, message, available, error) && available && type == 1 &&
		        message.payload == std::vector<std::byte>(data.begin(), data.end()),
		    "Peer receives exact payload and message type");
		test.Check(second.Receive(type, message, available, error) && !available,
		    "Empty receive is distinct from disconnection");
		second.Close();
		test.Check(!first.Send(1, data, error), "Send detects disconnected peer");
		test.Check(CreateLoopbackPair(a, b, error), "Reconnect creates fresh mailbox owners");
		NetworkSession old(std::move(a), 1), fresh(std::move(b), 2);
		test.Check(old.Send(1, data, error) && !fresh.Receive(type, message, available, error),
		    "Connection generation mismatch rejects stale traffic");
	}

	void RunDynamicPhaseContractsTests(TestContext& test) {
		Scene scene("Dynamic phase ordering");
		FrameTime time;
		RuntimeServices services;
		SystemContext context{scene, time, services};
		SystemScheduler scheduler;
		std::vector<SystemPhase> observed;
		const std::array phases{SystemPhase::Gameplay, SystemPhase::Animation, SystemPhase::Kinematic,
		    SystemPhase::Physics, SystemPhase::PostPhysics, SystemPhase::Transform, SystemPhase::Camera,
		    SystemPhase::AudioExtract, SystemPhase::UI, SystemPhase::Extract};
		for (auto phase : phases) {
			SystemDesc system;
			system.id = "dynamic-" + std::to_string(int(phase));
			system.phase = phase;
			system.execute = [&, phase](SystemContext&) {
				observed.push_back(phase);
				return WorldStatus{};
			};
			test.Check(scheduler.Add(std::move(system)).IsSuccess(), "Dynamic phase accepts an explicit system");
		}
		test.Check(scheduler.Compile().IsSuccess(), "Dynamic phase schedule compiles");
		for (auto phase : phases)
			test.Check(scheduler.RunPhase(phase, context).IsSuccess(), "Each phase executes successfully");
		test.Check(std::equal(observed.begin(), observed.end(), phases.begin(), phases.end()),
		    "Dynamic and presentation phases preserve the declared order");
	}
} /* 命名空间 Catalpa::Tests */
