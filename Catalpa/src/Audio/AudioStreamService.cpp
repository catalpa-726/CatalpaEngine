#include "catpch.h"

#include "Audio/AudioStreamService.h"

#include "Audio/AudioAssetLoader.h"
#include "Assets/AssetContainerReader.h"
namespace Catalpa {
	AudioStreamBuffer::AudioStreamBuffer(uint64_t frames, bool loop) : m_Frames(frames), m_Loop(loop) {}

	uint32_t AudioStreamBuffer::Available() const noexcept {
		const auto read = m_Read.load(std::memory_order_acquire);
		return uint32_t(m_Write.load(std::memory_order_acquire) - read);
	}

	uint32_t AudioStreamBuffer::Writable() const noexcept {
		return Capacity - Available();
	}

	uint32_t AudioStreamBuffer::Write(std::span<const float> data) noexcept {
		const auto write = m_Write.load(std::memory_order_relaxed);
		const auto read = m_Read.load(std::memory_order_acquire);
		const uint32_t count = uint32_t(std::min<size_t>(data.size() / 2, Capacity - (write - read)));
		for (uint32_t i = 0; i < count; ++i) {
			const auto p = size_t((write + i) % Capacity) * 2;
			m_PCM[p] = data[i * 2];
			m_PCM[p + 1] = data[i * 2 + 1];
		}
		m_Write.store(write + count, std::memory_order_release);
		return count;
	}

	uint32_t AudioStreamBuffer::Read(float* out, uint32_t frames) noexcept {
		const auto read = m_Read.load(std::memory_order_relaxed);
		const auto write = m_Write.load(std::memory_order_acquire);
		const uint32_t count = uint32_t(std::min<uint64_t>(frames, write - read));
		for (uint32_t i = 0; i < count; ++i) {
			const auto p = size_t((read + i) % Capacity) * 2;
			if (out) {
				out[i * 2] = m_PCM[p];
				out[i * 2 + 1] = m_PCM[p + 1];
			}
		}
		m_Read.store(read + count, std::memory_order_release);
		m_Consumed.fetch_add(count, std::memory_order_relaxed);
		if (count < frames && !m_End.load(std::memory_order_acquire)) {
			if (out)
				std::fill(out + size_t(count) * 2, out + size_t(frames) * 2, 0.0f);
			m_Underruns.fetch_add(frames - count, std::memory_order_relaxed);
			return frames;
		}
		return count;
	}

	uint64_t AudioStreamBuffer::Cursor() const noexcept {
		const auto frame = m_Base + m_Consumed.load(std::memory_order_relaxed);
		return m_Loop && m_Frames ? frame % m_Frames : std::min(frame, m_Frames);
	}

	void AudioStreamBuffer::Reset(uint64_t frame) noexcept {
		m_Read = 0;
		m_Write = 0;
		m_Consumed = 0;
		m_Underruns = 0;
		m_End = false;
		m_Base = frame;
	}

	AudioStreamService::AudioStreamService(
	    JobSystem& jobs, MemoryBudget& budget, AssetVersionRef<AudioClipAsset> clip, bool loop)
	    : m_Jobs(jobs), m_Budget(budget), m_Clip(std::move(clip)), m_Loop(loop) {}

	AudioStreamService::~AudioStreamService() {
		Stop();
	}

	bool AudioStreamService::Initialize(uint64_t frame, std::string& error) {
		if (!m_Clip.HasValue() || !m_Clip.Get()->Streaming() || !m_Clip.Get()->readDeferred ||
		    frame > m_Clip.Get()->Frames()) {
			error = "Invalid streaming clip or seek";
			return false;
		}
		if (!m_Reservation.Bytes() && !m_Budget.TryReserve(4 * 1024 * 1024, m_Reservation)) {
			error = "Audio stream workspace budget exceeded";
			return false;
		}
		if (!m_Buffer)
			m_Buffer = std::make_shared<AudioStreamBuffer>(m_Clip.Get()->Frames(), m_Loop);
		return Seek(frame, error);
	}

	bool AudioStreamService::Seek(uint64_t frame, std::string& error) {
		if (!m_Buffer || frame > m_Clip.Get()->Frames() || m_Generation == UINT64_MAX) {
			error = "Invalid stream seek or exhausted generation";
			return false;
		}
		m_Job.Cancel();
		if (m_Job.HasValue() && !m_Jobs.Wait(m_Job, error))
			return false;
		m_Job = {};
		m_Result.reset();
		m_Chunk = {};
		m_ChunkIndex = SIZE_MAX;
		++m_Generation;
		m_Producer = frame;
		m_Buffer->Reset(frame);
		error.clear();
		return true;
	}

	bool AudioStreamService::Pump(std::string& error) {
		if (!m_Buffer) {
			error = "Stream is not initialized";
			return false;
		}
		if (m_Job.HasValue()) {
			if (!m_Jobs.IsComplete(m_Job)) {
				error.clear();
				return true;
			}
			const auto completed = m_Result;
			std::lock_guard lock(completed->mutex);
			if (m_Result->generation != m_Generation) {
				m_Job = {};
				m_Result.reset();
				error.clear();
				return true;
			}
			if (!m_Result->success) {
				error = m_Result->error;
				return false;
			}
			m_Chunk = std::move(m_Result->chunk);
			m_Job = {};
		}
		const auto* clip = m_Clip.Get();
		while (m_Buffer->Writable()) {
			if (m_Producer == clip->Frames()) {
				if (m_Loop) {
					m_Producer = 0;
					m_ChunkIndex = SIZE_MAX;
					m_Chunk = {};
				} else {
					m_Buffer->End();
					error.clear();
					return true;
				}
			}
			const auto index = size_t(m_Producer / clip->chunkFrames);
			if (m_ChunkIndex != index || m_Chunk.samples.empty()) {
				if (m_Job.HasValue()) {
					error.clear();
					return true;
				}
				m_ChunkIndex = index;
				m_Chunk = {};
				m_Result = std::make_shared<ReadResult>();
				m_Result->generation = m_Generation;
				const auto result = m_Result;
				const auto fixed = m_Clip;
				const auto dependency = clip->chunks.at(index);
				if (!m_Jobs.Submit(
				        [result, fixed, dependency](const CancellationToken& token) {
					        AudioChunkAsset decoded;
					        std::string diagnostic;
					        std::vector<std::byte> bytes;
					        AssetContainer container;
					        bool ok = false;
					        try {
						        if (!token.IsCancellationRequested() &&
						            fixed.Get()->readDeferred(dependency.id, 1024 * 1024, bytes, diagnostic) &&
						            AssetContainerReader::Read(bytes, container, diagnostic, 1024 * 1024) &&
						            container.id == dependency.id && container.version == dependency.version &&
						            AudioAssetLoader::DecodeChunk(container, decoded, diagnostic) &&
						            !token.IsCancellationRequested())
							        ok = true;
					        } catch (const std::exception& e) {
						        diagnostic = e.what();
					        }
					        std::lock_guard lock(result->mutex);
					        result->chunk = std::move(decoded);
					        result->success = ok;
					        result->error = diagnostic.empty() && !ok
					            ? "Audio chunk read cancelled or identity mismatch"
					            : std::move(diagnostic);
				        },
				        m_Job, error)) {
					m_ChunkIndex = SIZE_MAX;
					m_Result.reset();
					return false;
				}
				error.clear();
				return true;
			}
			const auto offset = size_t(m_Producer % clip->chunkFrames) * 2;
			const auto expected =
			    std::min<uint64_t>(clip->chunkFrames, clip->Frames() - uint64_t(index) * clip->chunkFrames) * 2;
			if (m_Chunk.samples.size() != expected || offset >= m_Chunk.samples.size()) {
				error = "Audio chunk frame table mismatch";
				return false;
			}
			const auto written = m_Buffer->Write(std::span(m_Chunk.samples).subspan(offset));
			if (!written)
				break;
			m_Producer += written;
		}
		error.clear();
		return true;
	}

	void AudioStreamService::Stop() noexcept {
		m_Job.Cancel();
		if (m_Job.HasValue()) {
			std::string ignored;
			m_Jobs.Wait(m_Job, ignored);
		}
		m_Job = {};
		m_Result.reset();
		m_Chunk = {};
		m_Buffer.reset();
		m_Reservation = {};
	}
} /* 命名空间 Catalpa */
