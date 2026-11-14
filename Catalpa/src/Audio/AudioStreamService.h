#pragma once
#include "Audio/AudioClipAsset.h"
#include "Core/Jobs/JobSystem.h"
#include "Core/Memory/MemoryBudget.h"
namespace Catalpa {
	/* 预分配的 PCM 先进先出队列。所有者是唯一生产者，音频回调是唯一消费者。 */
	class CATALPA_API AudioStreamBuffer final {
		static_assert(std::atomic_uint64_t::is_always_lock_free, "Audio callback requires lock-free 64-bit counters");

	public:
		explicit AudioStreamBuffer(uint64_t frames, bool loop);
		/**
		 * @brief 消费立体声采样帧，并在未结束的流发生欠载时用静音补足。
		 *
		 * @param[out] stereo 接收交错立体声样本的缓冲区；空指针表示只推进读取位置。
		 * @param[in] frames 请求读取的帧数。
		 *
		 * @return 交付的立体声帧数。流未结束时欠载部分以静音补足，返回请求帧数；流已结束时只返回实际读到的帧数。
		 */
		uint32_t Read(float* stereo, uint32_t frames) noexcept;
		/**
		 * @brief 将交错排列的左右声道样本写入缓冲，返回实际写入的帧数。
		 *
		 * @param[in] stereo 交错立体声 float 样本，每两项为一帧。
		 *
		 * @return 实际入队的完整立体声帧数，受当前空闲容量限制。
		 */
		uint32_t Write(std::span<const float> stereo) noexcept;
		uint32_t Available() const noexcept;
		uint32_t Writable() const noexcept;
		bool Ended() const noexcept { return m_End.load(std::memory_order_acquire) && Available() == 0; }
		void End() noexcept { m_End.store(true, std::memory_order_release); }
		uint64_t Cursor() const noexcept;
		uint64_t UnderrunFrames() const noexcept { return m_Underruns.load(std::memory_order_relaxed); }
		/**
		 * @brief 重置流式音频队列及其读游标，供已脱离回调的播放节点重新定位。
		 * 调用 Reset 前须将节点从回调移除，避免重置与 Read 并发。
		 *
		 * @param[in] frame 重置后的片段起始帧。
		 */
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
		/**
		 * @brief 为流送片段预留解码预算，并从指定帧初始化读取。
		 *
		 * @param[in] frame 首次输出的片段帧索引。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Initialize(uint64_t frame, std::string& error);
		/**
		 * @brief 回收已完成的音频分块读取，按需启动后续读取任务。
		 * 回收已完成的音频分块读取，并按需发起后续流送作业。
		 *
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 本次处理成功时为 true；任务或服务状态阻止流送时为 false。
		 */
		bool Pump(std::string& error);
		/**
		 * @brief 将读取位置移至指定位置。
		 *
		 * @param[in] frame 目标 PCM 帧索引，以所有声道的一组样本为一帧。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Seek(uint64_t frame, std::string& error);
		/**
		 * @brief 取消在途分块读取，并等待作业退出后清理流送状态。
		 */
		void Stop() noexcept;
		const auto& Buffer() const noexcept { return m_Buffer; }
		bool Primed() const noexcept { return m_Buffer && (m_Buffer->Available() != 0 || m_Buffer->Ended()); }

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
} /* 命名空间 Catalpa */
