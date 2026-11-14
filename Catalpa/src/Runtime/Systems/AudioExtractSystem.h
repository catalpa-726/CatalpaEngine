#pragma once
#include "Audio/AudioService.h"
#include "Scene/SceneHandle.h"
#include "Scene/PersistentEntityId.h"
namespace Catalpa {
	class Scene;
	class CATALPA_API AudioExtractSystem final {
	public:
		/**
		 * @brief 将活动场景中的音频源与监听器状态同步到音频服务。
		 *
		 * @param[in] handle 目标对象的场景句柄。
		 * @param[in] scene 此次操作所针对的场景。
		 * @param[in] assets 提供资产查找和版本访问的管理器。
		 * @param[in] audio 接收从场景提取的声音变化的音频服务。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Update(SceneHandle, const Scene*, AssetManager&, AudioService&, std::string& error);
		/**
		 * @brief 将已提取的声部移交退休流程。
		 *
		 * @param[in] audio 用于停止本系统所跟踪声音的音频服务。
		 */
		void Stop(AudioService&) noexcept;
		/**
		 * @brief 停止当前场景持有的声音，并检查流式读取是否已全部退役。
		 * 只退役本场景拥有的 voice；流送 worker/回调脱离前返回 Busy，调用者保留包挂载后重试。
		 *
		 * @param[in,out] audio 负责停止本场景声音并完成异步退役的音频服务。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 所有本场景声音已退役时为 true；仍忙或退役失败时为 false。
		 */
		bool StopAndDrain(AudioService&, std::string& error);

	private:
		bool PumpRetiring(AudioService&, std::string& error);
		struct Retiring {
			AudioVoiceHandle voice;
			bool queued = false;
		};
		struct Source {
			AudioVoiceHandle voice;
			AssetId clip;
			uint64_t version = 0;
			AudioVoiceSettings settings;
		};
		SceneHandle m_Scene;
		uint64_t m_World = 0;
		std::map<PersistentEntityId, Source> m_Sources;
		std::array<Retiring, 256> m_Retiring{};
		size_t m_RetiringCount = 0;
		uint64_t m_StoppingWorld = 0;
	};
} /* 命名空间 Catalpa */
