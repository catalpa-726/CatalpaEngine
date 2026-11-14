#pragma once
#include "Audio/AudioTypes.h"
namespace Catalpa {
	class AudioStreamBuffer;
	class IAudioBackend {
	public:
		virtual ~IAudioBackend() = default;
		/**
		 * @brief 使用驻留内存的音频片段创建播放实例。
		 * @details 第 1 个参数（AudioVoiceHandle）为输入。目标声音句柄。
		 * @details 第 2 个参数（const AssetVersionRef<AudioClipAsset>&）为输入。为该声音持有的固定版本音频片段。
		 * @details 第 3 个参数（const AudioVoiceSettings&）为输入。该声音的播放与空间化设置。
		 * @details 第 4 个参数（std::string&）为输出。失败时接收具体诊断。
		 *
		 * @return 布尔结果，true 表示上述操作或判断成立。
		 */
		virtual bool Start(
		    AudioVoiceHandle, const AssetVersionRef<AudioClipAsset>&, const AudioVoiceSettings&, std::string&) = 0;
		/**
		 * @brief 启动数据流。
		 * @details 第 1 个参数（AudioVoiceHandle）为输入。本次操作使用的 AudioVoiceHandle。
		 * @details 第 2 个参数（const AssetVersionRef<AudioClipAsset>&）为输入。本次操作使用的 AssetVersionRef<AudioClipAsset>。
		 * @details 第 3 个参数（std::shared_ptr<AudioStreamBuffer>）为输入。本次操作使用的 AudioStreamBuffer。
		 * @details 第 4 个参数（const AudioVoiceSettings&）为输入。本次操作使用的 AudioVoiceSettings。
		 * @details 第 6 个参数（std::string&）为输出。失败时接收原因说明。
		 *
		 * @param[in] paused 是否暂停。
		 *
		 * @return 布尔结果，true 表示上述操作或判断成立。
		 */
		virtual bool StartStream(AudioVoiceHandle, const AssetVersionRef<AudioClipAsset>&,
		    std::shared_ptr<AudioStreamBuffer>, const AudioVoiceSettings&, bool paused, std::string&) = 0;
		/**
		 * @brief 从后端移除指定播放实例并结束对其数据的访问。
		 * @details 第 1 个参数（AudioVoiceHandle）为输入。目标声音句柄。
		 * @details 第 2 个参数（std::string&）为输出。失败时接收具体诊断。
		 *
		 * @return 布尔结果，true 表示上述操作或判断成立。
		 */
		virtual bool Stop(AudioVoiceHandle, std::string&) = 0; /* 成功后音频回调无法再读取此播放实例。 */
		/**
		 * @brief 将读取位置移至指定位置。
		 * @details 第 1 个参数（AudioVoiceHandle）为输入。本次操作使用的 AudioVoiceHandle。
		 * @details 第 3 个参数（std::string&）为输出。失败时接收原因说明。
		 *
		 * @param[in] frame 目标 PCM 帧索引，以所有声道的一组样本为一帧。
		 *
		 * @return 布尔结果，true 表示上述操作或判断成立。
		 */
		virtual bool Seek(AudioVoiceHandle, uint64_t frame, std::string&) = 0;
		/**
		 * @brief 切换指定后端声音的暂停状态。
		 * @details 第 1 个参数（AudioVoiceHandle）为输入。目标声音句柄。
		 * @details 第 3 个参数（std::string&）为输出。失败时接收具体诊断。
		 *
		 * @param[in] paused 为 true 时暂停，为 false 时恢复。
		 *
		 * @return 布尔结果，true 表示上述操作或判断成立。
		 */
		virtual bool Pause(AudioVoiceHandle, bool paused, std::string&) = 0;
		/**
		 * @brief 更新空间化声音在场景中的位置。
		 * @details 第 1 个参数（AudioVoiceHandle）为输入。目标声音句柄。
		 * @details 第 2 个参数（const Math::Vec3&）为输入。声音源的世界坐标。
		 * @details 第 3 个参数（std::string&）为输出。失败时接收具体诊断。
		 *
		 * @return 布尔结果，true 表示上述操作或判断成立。
		 */
		virtual bool Position(AudioVoiceHandle, const Math::Vec3&, std::string&) = 0;
		/**
		 * @brief 向音频后端设置当前监听者的位置与朝向。
		 * @details 第 1 个参数（const AudioListenerState&）为输入。场景中有效监听者的空间状态。
		 */
		virtual void Listener(const AudioListenerState&) = 0;
		/**
		 * @brief 设置后端混音输出的主增益。
		 * @details 第 1 个参数（float）为输入。线性音量倍率。
		 */
		virtual void MasterGain(float) = 0;
		/**
		 * @brief 检查后端设备是否处于可播放状态。
		 *
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 操作成功时为 true，失败时为 false。
		 */
		virtual bool Healthy(std::string& error) const = 0;
		/**
		 * @brief 让需要手动推进的后端按时间步更新声音。
		 *
		 * @param[in] seconds 本次模拟经过的时间（秒）。
		 */
		virtual void Advance(double seconds) = 0;
		/**
		 * @brief 返回后端已登记声音的播放帧和暂停状态快照。
		 *
		 * @return 返回后端已登记声音的播放帧和暂停状态快照。
		 */
		virtual std::vector<AudioVoiceSnapshot> Snapshot() const = 0;
		/**
		 * @brief 释放后端持有的声音实例并结束其运行。
		 */
		virtual void Shutdown() noexcept = 0;
	};
} /* 命名空间 Catalpa */
