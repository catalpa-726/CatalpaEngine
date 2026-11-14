#pragma once
#include "Audio/Backends/IAudioBackend.h"
#include "Scene/SceneHandle.h"
namespace Catalpa {
	class AssetManager;
	class JobSystem;
	class Scene;
	class CATALPA_API AudioService final {
	public:
		AudioService();
		~AudioService();
		void BindJobs(JobSystem&);
		/**
		 * @brief 创建音频后端，记录服务的所属线程并应用容量设置。
		 * 验证容量、创建音频后端，并绑定服务所有者线程。
		 *
		 * @param[in] config 后端、声音数量与命令容量设置。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 配置与后端初始化成功时为 true，否则为 false。
		 */
		bool Initialize(AudioServiceConfig, std::string& error);
		/**
		 * @brief 为指定片段分配声音实例，并排入播放请求。
		 *
		 * @param[in] clip 持有固定版本的音频片段。
		 * @param[in] settings 音量、循环、空间位置等播放设置。
		 * @param[out] output 成功时返回新声音句柄。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Play(const AssetVersionRef<AudioClipAsset>&, AudioVoiceSettings, AudioVoiceHandle&, std::string& error);
		bool Stop(AudioVoiceHandle, std::string& error);
		/**
		 * @brief 将播放实例交给音频服务退役，可重复请求同一句柄。
		 * 预览宿主关闭时移交退役责任；有界地等待命令额度，不要求已销毁的宿主继续重试。
		 *
		 * @param[in] id 要释放所有权的声音句柄。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 句柄已无须退役、已登记或本次成功登记时为 true；线程或容量不满足时为 false。
		 */
		bool Retire(AudioVoiceHandle, std::string& error);
		/**
		 * @brief 将读取位置移至指定位置。
		 *
		 * @param[in] id 需要重新定位播放游标的声部句柄。
		 * @param[in] frame 目标 PCM 帧索引，以所有声道的一组样本为一帧。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Seek(AudioVoiceHandle, uint64_t frame, std::string& error);
		/**
		 * @brief 为声音实例排入暂停或恢复命令。
		 *
		 * @param[in] id 目标声音句柄。
		 * @param[in] paused 为 true 时暂停，为 false 时恢复。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Pause(AudioVoiceHandle, bool paused, std::string& error);
		/**
		 * @brief 设置位置。
		 *
		 * @param[in] id 需要更新空间位置的活动声部句柄。
		 * @param[in] position 目标位置。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool SetPosition(AudioVoiceHandle, Math::Vec3, std::string& error);
		/**
		 * @brief 设置监听器。
		 *
		 * @param[in] listener 用来更新空间音频参考位置和朝向的监听器状态。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool SetListener(AudioListenerState, std::string& error);
		/**
		 * @brief 设置主增益。
		 *
		 * @param[in] gain 应用到声源或混音输出的增益。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool SetMasterGain(float, std::string& error);
		/**
		 * @brief 推进音频命令、流送和声音退休处理。
		 *
		 * @param[in] seconds 本次更新时间（秒）。
		 */
		void Update(double seconds);
		/**
		 * @brief 取出已收到的确认记录。
		 *
		 * @return 待取走的音频命令确认记录，返回后这些记录从服务队列移出。
		 */
		std::vector<AudioAcknowledgement> TakeAcknowledgements();
		std::vector<AudioVoiceSnapshot> Snapshot() const;
		const std::string& Diagnostic() const noexcept;
		bool IsNull() const noexcept;
		bool HasVoice(AudioVoiceHandle) const noexcept;
		/**
		 * @brief 停止后端回调后收束流送任务，释放音频服务。
		 */
		void Shutdown() noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
} /* 命名空间 Catalpa */
