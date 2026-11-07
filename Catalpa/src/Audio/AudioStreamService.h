#pragma once
#include "Audio/AudioClipAsset.h"
#include "Core/Jobs/JobSystem.h"
#include "Core/Memory/MemoryBudget.h"
namespace Catalpa {
    // 单生产者(owner)、单消费者(audio callback)的预分配PCM FIFO。
    class CATALPA_API AudioStreamBuffer final {
        static_assert(std::atomic_uint64_t::is_always_lock_free,
                      "Audio callback requires lock-free 64-bit counters");

      public:
        explicit AudioStreamBuffer(uint64_t frames, bool loop);
        uint32_t Read(float* stereo, uint32_t frames) noexcept;
        uint32_t Write(std::span<const float> stereo) noexcept;
        uint32_t Available() const noexcept;
        uint32_t Writable() const noexcept;
        bool Ended() const noexcept {
            return m_End.load(std::memory_order_acquire) && Available() == 0;
        }
        void End() noexcept {
            m_End.store(true, std::memory_order_release);
        }
        uint64_t Cursor() const noexcept;
        uint64_t UnderrunFrames() const noexcept {
            return m_Underruns.load(std::memory_order_relaxed);
        }
        // callback已脱离节点后才能Reset；从不与Read并发重置。
        void Reset(uint64_t frame) noexcept;

      private:
        static constexpr uint32_t Capacity = 8192;
        std::array<float, Capacity * 2> m_PCM{};
        std::atomic_uint64_t m_Read{0}, m_Write{0}, m_Consumed{0}, m_Underruns{0};
        std::atomic_bool m_End{false};
        uint64_t m_Frames = 0, m_Base = 0;
        bool m_Loop = false;
    };
    class CATALPA_API AudioStreamService final {
      public:
        AudioStreamService(JobSystem&, MemoryBudget&, AssetVersionRef<AudioClipAsset>, bool loop);
        ~AudioStreamService();
        bool Initialize(uint64_t frame, std::string& error);
        bool Pump(std::string& error);
        bool Seek(uint64_t frame, std::string& error);
        void Stop() noexcept;
        const auto& Buffer() const noexcept {
            return m_Buffer;
        }
        bool Primed() const noexcept {
            return m_Buffer && (m_Buffer->Available() != 0 || m_Buffer->Ended());
        }

      private:
        struct ReadResult {
            std::mutex mutex;
            AudioChunkAsset chunk;
            std::string error;
            bool success = false;
            uint64_t generation = 0;
        };
        JobSystem& m_Jobs;
        MemoryBudget& m_Budget;
        MemoryReservation m_Reservation;
        AssetVersionRef<AudioClipAsset> m_Clip;
        bool m_Loop = false;
        std::shared_ptr<AudioStreamBuffer> m_Buffer;
        JobHandle m_Job;
        std::shared_ptr<ReadResult> m_Result;
        AudioChunkAsset m_Chunk;
        size_t m_ChunkIndex = SIZE_MAX;
        uint64_t m_Producer = 0, m_Generation = 1;
    };
} // namespace Catalpa
