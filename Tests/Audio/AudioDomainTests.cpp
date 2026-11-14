#include "catpch.h"

#include "TestRunner.h"
#include "Audio/AudioService.h"
#include "Audio/AudioStreamService.h"
#include "Audio/AudioAssetLoader.h"
#include "Audio/AudioConfig.h"
#include "Assets/AssetContainerReader.h"
#include "Assets/AssetManager.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"
#include "Scene/SceneDynamicComponents.h"
#include "Scene/Components/AudioSourceComponent.h"
#include "Runtime/Systems/AudioExtractSystem.h"
#include "Runtime/Systems/TransformSystem.h"

namespace Catalpa::Tests {
	void RunAudioDomainTests(TestContext& test) {
		std::string error;
		AudioClipAsset clip;
		clip.sourceUri = "generated:test-tone";
		clip.samples.resize(48000 * 2, .25f);
		test.Check(ValidateAudioClip(clip, error), "Resident audio validates explicit 48 kHz stereo finite PCM");
		auto invalid = clip;
		invalid.samples[0] = std::numeric_limits<float>::quiet_NaN();
		std::vector<std::byte> bytes{std::byte(42)};
		test.Check(!AudioAssetLoader::Encode(invalid, bytes, error) && bytes == std::vector{std::byte(42)},
		    "Invalid PCM preserves caller output instead of normalizing samples");
		AssetVersionRef<AudioClipAsset> fixed;
		test.Check(AssetVersionRef<AudioClipAsset>::TryCreate(
		               AssetId::FromWords(90, 1), 1, std::make_shared<const AudioClipAsset>(clip), fixed, error),
		    "Pin resident audio version");
		AudioService audio;
		AudioServiceConfig config;
		config.forceNull = true;
		config.maxVoices = 2;
		config.maxCommands = 4;
		if (!audio.Initialize(config, error)) {
			test.Check(false, error);
			return;
		}
		AudioVoiceHandle voice;
		test.Check(audio.Play(fixed, {}, voice, error), "Null audio admits an ordered Play command");
		audio.Update(.25);
		auto acks = audio.TakeAcknowledgements();
		const auto state = audio.Snapshot();
		test.Check(acks.size() == 1 && acks[0].success && state.size() == 1 && state[0].cursor == 12000,
		    "Null backend follows media frame time with an explicit Play acknowledgement");
		test.Check(audio.Pause(voice, true, error) && audio.Seek(voice, 24000, error),
		    "Pause and seek share the FIFO command order");
		audio.Update(.25);
		acks = audio.TakeAcknowledgements();
		test.Check(acks.size() == 2 && acks[0].sequence < acks[1].sequence && audio.Snapshot()[0].cursor == 24000,
		    "Paused seek is deterministic and does not advance media time");
		test.Check(audio.Seek(voice, 48001, error), "Out-of-range seek is reported at ordered application");
		audio.Update(0);
		acks = audio.TakeAcknowledgements();
		test.Check(acks.size() == 1 && !acks[0].success && audio.Snapshot()[0].cursor == 24000,
		    "Rejected seek retains a valid voice position");
		test.Check(audio.Stop(voice, error), "Stop is admitted");
		audio.Update(0);
		acks = audio.TakeAcknowledgements();
		test.Check(acks.size() == 1 && acks[0].success && audio.Snapshot().empty() && !audio.HasVoice(voice),
		    "Stop acknowledgement follows voice release");
		test.Check(!audio.SetMasterGain(std::numeric_limits<float>::infinity(), error), "Mixer rejects nonfinite gain");
		AudioVoiceHandle one, two, three;
		test.Check(audio.Play(fixed, {}, one, error) && audio.Play(fixed, {}, two, error) &&
		        !audio.Play(fixed, {}, three, error),
		    "Admission enforces the configured voice count before processing");
		audio.Shutdown();
		audio.Shutdown();

		AudioService closing;
		AudioServiceConfig closingConfig;
		closingConfig.forceNull = true;
		closingConfig.maxVoices = 1;
		closingConfig.maxCommands = 1;
		if (closing.Initialize(closingConfig, error)) {
			AudioVoiceSettings loop;
			loop.loop = true;
			AudioVoiceHandle preview;
			test.Check(closing.Play(fixed, loop, preview, error) && !closing.Stop(preview, error) &&
			        closing.Retire(preview, error) && closing.Retire(preview, error),
			    "Closing preview transfers retirement ownership even when its command queue is full");
			closing.Update(0);
			auto playAck = closing.TakeAcknowledgements();
			closing.Update(0);
			auto stopAck = closing.TakeAcknowledgements();
			test.Check(playAck.size() == 1 && stopAck.size() == 1 && stopAck[0].success &&
			        playAck[0].sequence < stopAck[0].sequence && !closing.HasVoice(preview),
			    "Deferred retirement keeps FIFO acknowledgement order and releases the looping voice");
			closing.Shutdown();
		} else
			test.Check(false, error);

		/* 使用两个不可变测试容器模拟 I/O 边界。工作线程实际解包 PCM 数据，音频回调只读定长队列。 */
		JobSystem jobs;
		if (!jobs.Start({1, 8}, error)) {
			test.Check(false, error);
			return;
		}
		AudioClipAsset stream;
		stream.sourceUri = "generated:stream";
		stream.frameCount = 65536 + 32;
		std::map<AssetId, std::vector<std::byte>> containers;
		for (uint64_t i = 0; i < 2; ++i) {
			const auto id = AssetId::FromWords(91, i + 1);
			AssetContainer container;
			container.id = id;
			container.version = 1;
			AssetType::TryParse("audio_chunk", container.type);
			std::vector<float> pcm(size_t(i ? 32 : 65536) * 2, i ? .75f : .25f);
			if (!AudioAssetLoader::EncodeChunk(pcm, container.payload, error) ||
			    !WriteAssetContainer(container, containers[id], error)) {
				test.Check(false, error);
				return;
			}
			stream.chunks.push_back({id, 1, {}});
		}
		auto owned = std::make_shared<const decltype(containers)>(containers);
		auto workerReads = std::make_shared<std::atomic_uint32_t>(0);
		const auto owner = std::this_thread::get_id();
		stream.readDeferred = [owned, workerReads, owner](
		                          AssetId id, size_t, std::vector<std::byte>& out, std::string& error) {
			if (std::this_thread::get_id() == owner) {
				error = "Chunk read ran on owner thread";
				return false;
			}
			workerReads->fetch_add(1);
			const auto it = owned->find(id);
			if (it == owned->end()) {
				error = "Unknown chunk";
				return false;
			}
			out = it->second;
			return true;
		};
		AssetVersionRef<AudioClipAsset> streamed;
		AssetVersionRef<AudioClipAsset>::TryCreate(
		    AssetId::FromWords(91, 3), 1, std::make_shared<const AudioClipAsset>(std::move(stream)), streamed, error);
		MemoryBudget budget(8 * 1024 * 1024);
		AudioStreamService producer(jobs, budget, streamed, false);
		test.Check(producer.Initialize(65536, error),
		    "Streaming seek selects the final partial chunk without decoding earlier blocks");
		bool loaded = false;
		for (int n = 0; n < 1000 && !loaded; ++n) {
			if (!producer.Pump(error))
				break;
			loaded = producer.Primed();
			if (!loaded)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		test.Check(loaded && workerReads->load() == 1, "Worker reads only the selected seek-table chunk");
		if (loaded) {
			std::array<float, 64> samples{};
			test.Check(producer.Buffer()->Read(samples.data(), 32) == 32 && samples[0] == .75f &&
			        producer.Buffer()->Cursor() == 65568,
			    "Partial final chunk reaches exact media EOF without false underrun");
		}
		producer.Stop();
		test.Check(budget.GetStats().used == 0, "Stream stop joins its worker before releasing workspace");

		/* 包会话只退役所属场景的声音；独立编辑器预览继续存在，不因卸载另一 World 被一并停止。 */
		AudioService retiring;
		AudioServiceConfig retirementConfig;
		retirementConfig.forceNull = true;
		retirementConfig.maxVoices = 4;
		retirementConfig.maxCommands = 16;
		retiring.BindJobs(jobs);
		if (retiring.Initialize(retirementConfig, error)) {
			AssetManager assets(jobs);
			SceneManager scenes;
			SceneHandle handle;
			if (scenes.CreateScene("Audio retirement", handle).IsSuccess()) {
				auto* scene = scenes.GetScene(handle);
				const auto object = PersistentEntityId::FromWords(91, 4);
				auto status = scene->QueueCreate(object, "Streaming source", {});
				if (status.IsSuccess())
					status = scene->CommitPending();
				AudioSourceComponent source;
				source.clip = streamed.GetId();
				source.clipVersion = streamed;
				if (status.IsSuccess())
					status = QueueDynamicComponent(*scene, scene->Resolve(object),
					    [&](World& world) { return world.Add(scene->Resolve(object), source); });
				if (status.IsSuccess())
					status = scene->CommitPending();
				if (status.IsSuccess())
					status = TransformSystem::Update(*scene);
				AudioExtractSystem extract;
				AudioVoiceHandle preview;
				AudioVoiceSettings previewSettings;
				previewSettings.loop = true;
				test.Check(status.IsSuccess() && retiring.Play(fixed, previewSettings, preview, error) &&
				        extract.Update(handle, scene, assets, retiring, error),
				    "Scene stream and independent preview use the same audio service with distinct "
				    "ownership");
				retiring.Update(0);
				retiring.TakeAcknowledgements();
				bool drained = false;
				for (int attempt = 0; attempt < 1000 && !drained; ++attempt) {
					drained = extract.StopAndDrain(retiring, error);
					retiring.TakeAcknowledgements();
					if (!drained)
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				test.Check(drained && retiring.HasVoice(preview) && retiring.Snapshot().size() == 1,
				    "Scene Stop acknowledges detached stream workers without killing an unrelated "
				    "preview voice");
				test.Check(extract.Update(handle, scene, assets, retiring, error),
				    "Retiring World remains quiescent while the package host retries unmount");
				retiring.Update(0);
				retiring.TakeAcknowledgements();
				test.Check(retiring.Snapshot().size() == 1,
				    "A retiring Scene cannot automatically restart its stopped source");
			} else
				test.Check(false, "Create audio retirement scene");
			retiring.Shutdown();
		} else
			test.Check(false, error);
		jobs.Shutdown();
		AudioServiceConfig preserved;
		test.Check(!ReadAudioConfig("{\"schemaVersion\":1}", preserved, error) && preserved.maxVoices == 64,
		    "Malformed audio config preserves previous values");
	}
} /* 命名空间 Catalpa::Tests */
