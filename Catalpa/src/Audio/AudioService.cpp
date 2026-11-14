#include "catpch.h"

#include "Audio/AudioService.h"

#include "Audio/AudioStreamService.h"
#include "Audio/Null/NullAudioBackend.h"
#include "Audio/Miniaudio/MiniaudioBackend.h"

namespace Catalpa {
	namespace {
		bool Finite(const Math::Vec3& value) {
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}
	} /* 命名空间 */

	struct AudioService::Impl {
		struct Command {
			enum class Type { Play, Stop, Seek, Pause, Position };
			Type type;
			uint64_t sequence;
			AudioVoiceHandle voice;
			AssetVersionRef<AudioClipAsset> clip;
			AudioVoiceSettings settings;
			uint64_t frame = 0;
			bool paused = false;
		};
		std::unique_ptr<IAudioBackend> backend;
		AudioServiceConfig config;
		std::thread::id owner = std::this_thread::get_id();
		std::deque<Command> queue;
		std::vector<AudioAcknowledgement> acks;
		std::set<AudioVoiceHandle> admitted;
		std::array<AudioVoiceHandle, 256> retiring{};
		size_t retiringCount = 0;
		JobSystem* jobs = nullptr;
		MemoryBudget streamBudget{64 * 1024 * 1024};
		std::map<AudioVoiceHandle, std::unique_ptr<AudioStreamService>> streams;
		std::map<AudioVoiceHandle, AudioVoiceSettings> settings;
		std::map<AudioVoiceHandle, AssetVersionRef<AudioClipAsset>> clips;
		std::map<AudioVoiceHandle, bool> paused;
		std::optional<Command> pendingStart;
		uint64_t nextVoice = 1, nextSequence = 1;
		bool nullBackend = false;
		float masterGain = 1;
		AudioListenerState listener;
		std::string diagnostic;

		bool Owner(std::string& error) const {
			if (owner != std::this_thread::get_id() || !backend) {
				error = "Audio service requires initialized owner thread";
				return false;
			}
			return true;
		}

		bool Push(Command command, std::string& error) {
			if (!Owner(error))
				return false;
			if (queue.size() + acks.size() + (pendingStart ? 1 : 0) >= config.maxCommands ||
			    nextSequence == UINT64_MAX) {
				error = "Audio command/ack budget exceeded";
				return false;
			}
			try {
				command.sequence = nextSequence;
				queue.push_back(std::move(command));
				++nextSequence;
				error.clear();
				return true;
			} catch (const std::bad_alloc&) {
				error = "Cannot reserve audio command";
				return false;
			}
		}

		void Release(AudioVoiceHandle id) {
			/* 调用前先将节点从后端移除，或确认音频回调尚未取得此播放实例。 */
			streams.erase(id);
			admitted.erase(id);
			settings.erase(id);
			clips.erase(id);
			paused.erase(id);
		}

		void Acknowledge(const Command& command, bool success, std::string error) {
			acks.push_back({command.sequence, command.voice, success, std::move(error)});
		}

		void PumpRetirements() {
			for (size_t index = 0; index < retiringCount;) {
				const auto voice = retiring[index];
				if (admitted.contains(voice)) {
					std::string error;
					if (!Push({Command::Type::Stop, 0, voice}, error))
						break; /* 已拥有退役请求；正常 ack 消费释放额度后，下帧继续按序入队。 */
				}
				std::move(retiring.begin() + index + 1, retiring.begin() + retiringCount, retiring.begin() + index);
				--retiringCount;
			}
		}

		void CheckDevice() {
			std::string error;
			if (backend->Healthy(error))
				return;
			diagnostic = error;
			if (!config.allowNullFallback)
				return;
			const auto voices = backend->Snapshot();
			backend->Shutdown();
			backend = CreateNullAudioBackend();
			nullBackend = true;
			backend->MasterGain(masterGain);
			backend->Listener(listener);
			for (const auto& voice : voices) {
				const auto id = voice.handle;
				if (const auto stream = streams.find(id); stream != streams.end())
					backend->StartStream(
					    id, clips.at(id), stream->second->Buffer(), settings.at(id), paused[id], error);
				else if (backend->Start(id, clips.at(id), settings.at(id), error))
					backend->Seek(id, voice.cursor, error);
				if (paused[id])
					backend->Pause(id, true, error);
			}
			diagnostic += "; voices continue in the Null backend";
		}

		void PumpStreams() {
			std::vector<AudioVoiceHandle> failed;
			for (auto& [id, stream] : streams) {
				std::string error;
				if (!stream->Pump(error)) {
					diagnostic = error;
					failed.push_back(id);
					if (pendingStart && pendingStart->voice == id) {
						Acknowledge(*pendingStart, false, error);
						pendingStart.reset();
					}
				} else if (stream->Buffer()->UnderrunFrames()) {
					diagnostic = "Audio stream underrun inserted silence; media cursor is retained";
				}
			}
			/* 先将节点从音频回调移除，再取消并等待磁盘任务结束，最后释放 PCM 队列。 */
			for (const auto id : failed) {
				std::string ignored;
				backend->Stop(id, ignored);
				Release(id);
			}
		}

		bool FinishPendingStart() {
			if (!pendingStart)
				return true;
			const auto found = streams.find(pendingStart->voice);
			if (found == streams.end()) {
				Acknowledge(*pendingStart, false, "Stream was cancelled");
				pendingStart.reset();
				return true;
			}
			if (!found->second->Primed())
				return false;
			const auto id = pendingStart->voice;
			std::string error;
			bool ok =
			    backend->StartStream(id, clips.at(id), found->second->Buffer(), settings.at(id), paused[id], error);
			Acknowledge(*pendingStart, ok, std::move(error));
			if (!ok) {
				backend->Stop(id, error);
				Release(id);
			}
			pendingStart.reset();
			return true;
		}
	};

	AudioService::AudioService() : m_Impl(std::make_unique<Impl>()) {}

	AudioService::~AudioService() {
		Shutdown();
	}

	void AudioService::BindJobs(JobSystem& jobs) {
		m_Impl->jobs = &jobs;
	}

	bool AudioService::Initialize(AudioServiceConfig config, std::string& error) {
		if (m_Impl->backend || config.maxVoices == 0 || config.maxVoices > 256 ||
		    config.maxCommands < config.maxVoices || config.maxCommands > 4096) {
			error = "Invalid or already initialized audio service";
			return false;
		}
		m_Impl->owner = std::this_thread::get_id();
		m_Impl->config = config;
		m_Impl->acks.reserve(config.maxCommands);
		m_Impl->nullBackend = false;
		m_Impl->diagnostic.clear();
		if (!config.forceNull)
			m_Impl->backend = CreateMiniaudioBackend(error);
		if (!m_Impl->backend) {
			if (!config.forceNull && !config.allowNullFallback)
				return false;
			m_Impl->diagnostic =
			    error.empty() ? "Null audio backend explicitly selected" : error + "; using Null audio backend";
			m_Impl->backend = CreateNullAudioBackend();
			m_Impl->nullBackend = true;
		}
		m_Impl->backend->MasterGain(m_Impl->masterGain);
		m_Impl->backend->Listener(m_Impl->listener);
		error.clear();
		return true;
	}

	bool AudioService::Play(const AssetVersionRef<AudioClipAsset>& clip, AudioVoiceSettings settings,
	    AudioVoiceHandle& output, std::string& error) {
		if (!m_Impl->Owner(error))
			return false;
		if (!clip.HasValue()) {
			error = "Play requires a pinned audio clip";
			return false;
		}
		if (!ValidateAudioClip(*clip.Get(), error))
			return false;
		if (!Finite(settings.position) || !std::isfinite(settings.gain) || settings.gain < 0 || settings.gain > 16 ||
		    !std::isfinite(settings.pitch) || settings.pitch < .125f || settings.pitch > 8 ||
		    !std::isfinite(settings.minDistance) || settings.minDistance <= 0 || !std::isfinite(settings.maxDistance) ||
		    settings.maxDistance < settings.minDistance || m_Impl->admitted.size() >= m_Impl->config.maxVoices ||
		    m_Impl->nextVoice == UINT64_MAX) {
			error = "Audio voice parameters or capacity rejected";
			return false;
		}
		if (clip.Get()->Streaming() && (!m_Impl->jobs || !m_Impl->jobs->IsRunning())) {
			error = "Streaming audio requires the Runtime job system";
			return false;
		}
		const AudioVoiceHandle id{m_Impl->nextVoice};
		try {
			m_Impl->settings.emplace(id, settings);
			m_Impl->clips.emplace(id, clip);
			m_Impl->admitted.insert(id);
			if (!m_Impl->Push({Impl::Command::Type::Play, 0, id, clip, settings}, error)) {
				m_Impl->Release(id);
				return false;
			}
		} catch (const std::bad_alloc&) {
			m_Impl->Release(id);
			error = "Cannot reserve audio voice";
			return false;
		}
		++m_Impl->nextVoice;
		output = id;
		return true;
	}

	bool AudioService::Stop(AudioVoiceHandle id, std::string& error) {
		return m_Impl->Push({Impl::Command::Type::Stop, 0, id}, error);
	}

	bool AudioService::Retire(AudioVoiceHandle id, std::string& error) {
		if (!m_Impl->Owner(error))
			return false;
		for (size_t index = 0; index < m_Impl->retiringCount;)
			if (!m_Impl->admitted.contains(m_Impl->retiring[index])) {
				std::move(m_Impl->retiring.begin() + index + 1, m_Impl->retiring.begin() + m_Impl->retiringCount,
				    m_Impl->retiring.begin() + index);
				--m_Impl->retiringCount;
			} else {
				if (m_Impl->retiring[index] == id) {
					error.clear();
					return true;
				}
				++index;
			}
		if (!m_Impl->admitted.contains(id)) {
			error.clear();
			return true;
		}
		if (m_Impl->retiringCount == m_Impl->retiring.size()) {
			error = "Audio retirement ownership capacity exceeded";
			return false;
		}
		m_Impl->retiring[m_Impl->retiringCount++] = id;
		error.clear();
		return true;
	}

	bool AudioService::Seek(AudioVoiceHandle id, uint64_t frame, std::string& error) {
		Impl::Command command{Impl::Command::Type::Seek, 0, id};
		command.frame = frame;
		return m_Impl->Push(std::move(command), error);
	}

	bool AudioService::Pause(AudioVoiceHandle id, bool paused, std::string& error) {
		Impl::Command command{Impl::Command::Type::Pause, 0, id};
		command.paused = paused;
		return m_Impl->Push(std::move(command), error);
	}

	bool AudioService::SetPosition(AudioVoiceHandle id, Math::Vec3 position, std::string& error) {
		if (!Finite(position)) {
			error = "Audio position must be finite";
			return false;
		}
		Impl::Command command{Impl::Command::Type::Position, 0, id};
		command.settings.position = position;
		return m_Impl->Push(std::move(command), error);
	}

	bool AudioService::SetListener(AudioListenerState listener, std::string& error) {
		if (!m_Impl->Owner(error))
			return false;
		const auto forward = glm::dvec3(listener.forward), up = glm::dvec3(listener.up);
		if (!Finite(listener.position) || !Finite(listener.forward) || !Finite(listener.up) ||
		    glm::length(forward) < 1e-5 || glm::length(up) < 1e-5 || glm::length(glm::cross(forward, up)) < 1e-5) {
			error = "Listener vectors must be finite and nondegenerate";
			return false;
		}
		/* 只构造派生的单位朝向，不修改 Scene 作者变换。 */
		listener.forward = Math::Vec3(glm::normalize(forward));
		listener.up =
		    Math::Vec3(glm::normalize(glm::cross(glm::normalize(glm::cross(forward, up)), glm::normalize(forward))));
		m_Impl->listener = listener;
		m_Impl->backend->Listener(listener);
		error.clear();
		return true;
	}

	bool AudioService::SetMasterGain(float gain, std::string& error) {
		if (!m_Impl->Owner(error))
			return false;
		if (!std::isfinite(gain) || gain < 0 || gain > 4) {
			error = "Master gain must lie in [0,4]";
			return false;
		}
		m_Impl->masterGain = gain;
		m_Impl->backend->MasterGain(gain);
		error.clear();
		return true;
	}

	void AudioService::Update(double seconds) {
		if (m_Impl->owner != std::this_thread::get_id() || !m_Impl->backend)
			return;
		if (!std::isfinite(seconds) || seconds < 0 || seconds > 1) {
			m_Impl->diagnostic = "Audio frame delta must lie in [0,1] seconds";
			return;
		}
		m_Impl->CheckDevice();
		m_Impl->PumpRetirements();
		m_Impl->PumpStreams();
		if (!m_Impl->FinishPendingStart()) {
			m_Impl->backend->Advance(seconds);
			return;
		}
		while (!m_Impl->queue.empty()) {
			auto command = std::move(m_Impl->queue.front());
			m_Impl->queue.pop_front();
			std::string error;
			bool ok = false;
			switch (command.type) {
			case Impl::Command::Type::Play:
				if (command.clip.Get()->Streaming()) {
					auto stream = std::make_unique<AudioStreamService>(
					    *m_Impl->jobs, m_Impl->streamBudget, command.clip, command.settings.loop);
					ok = stream->Initialize(0, error) && stream->Pump(error);
					if (ok) {
						m_Impl->streams.emplace(command.voice, std::move(stream));
						m_Impl->pendingStart = std::move(command);
						m_Impl->backend->Advance(seconds);
						return;
					}
				} else
					ok = m_Impl->backend->Start(command.voice, command.clip, command.settings, error);
				if (!ok)
					m_Impl->Release(command.voice);
				break;
			case Impl::Command::Type::Stop:
				ok = m_Impl->backend->Stop(command.voice, error);
				if (ok)
					m_Impl->Release(command.voice);
				break;
			case Impl::Command::Type::Seek:
				if (const auto stream = m_Impl->streams.find(command.voice); stream != m_Impl->streams.end()) {
					/* 无效 seek 不能先停止原本仍有效的声音。 */
					if (command.frame > m_Impl->clips.at(command.voice).Get()->Frames()) {
						error = "Seek exceeds clip frame count";
						break;
					}
					ok = m_Impl->backend->Stop(command.voice, error) && stream->second->Seek(command.frame, error);
					if (ok) {
						m_Impl->pendingStart = std::move(command);
						m_Impl->backend->Advance(seconds);
						return;
					}
				} else
					ok = m_Impl->backend->Seek(command.voice, command.frame, error);
				break;
			case Impl::Command::Type::Pause:
				ok = m_Impl->backend->Pause(command.voice, command.paused, error);
				if (ok)
					m_Impl->paused[command.voice] = command.paused;
				break;
			case Impl::Command::Type::Position:
				ok = m_Impl->backend->Position(command.voice, command.settings.position, error);
				if (ok)
					m_Impl->settings.at(command.voice).position = command.settings.position;
				break;
			}
			m_Impl->Acknowledge(command, ok, std::move(error));
		}
		m_Impl->backend->Advance(seconds);
		for (const auto& voice : m_Impl->backend->Snapshot()) {
			if (voice.playing || voice.cursor != voice.frames)
				continue;
			std::string ignored;
			m_Impl->backend->Stop(voice.handle, ignored);
			m_Impl->Release(voice.handle);
		}
	}

	std::vector<AudioAcknowledgement> AudioService::TakeAcknowledgements() {
		auto result = std::move(m_Impl->acks);
		m_Impl->acks = {};
		m_Impl->acks.reserve(m_Impl->config.maxCommands);
		return result;
	}

	std::vector<AudioVoiceSnapshot> AudioService::Snapshot() const {
		return m_Impl->backend ? m_Impl->backend->Snapshot() : std::vector<AudioVoiceSnapshot>{};
	}

	const std::string& AudioService::Diagnostic() const noexcept {
		return m_Impl->diagnostic;
	}

	bool AudioService::IsNull() const noexcept {
		return m_Impl->nullBackend;
	}

	bool AudioService::HasVoice(AudioVoiceHandle id) const noexcept {
		return m_Impl->admitted.contains(id);
	}

	void AudioService::Shutdown() noexcept {
		/* 设备回调先静止，再等待流送 worker；Jobs 必须比本服务晚关闭。 */
		if (m_Impl->backend)
			m_Impl->backend->Shutdown();
		m_Impl->backend.reset();
		m_Impl->streams.clear();
		m_Impl->pendingStart.reset();
		m_Impl->settings.clear();
		m_Impl->clips.clear();
		m_Impl->paused.clear();
		m_Impl->queue.clear();
		m_Impl->acks.clear();
		m_Impl->admitted.clear();
		m_Impl->retiringCount = 0;
	}
} /* 命名空间 Catalpa */
