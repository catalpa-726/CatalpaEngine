#pragma once
#include "Assets/AssetHandle.h"
#include "Materials/MaterialInstance.h"
#include "Core/Math/MathTypes.h"

namespace Catalpa {
	inline constexpr uint32_t MaximumParticleCapacity = 16384;
	struct ParticleEffect {
		AssetId material;
		AssetVersionRef<MaterialInstance> materialVersion;
		uint32_t seed = 1, maxParticles = 1024;
		double emissionRate = 30, duration = 5;
		bool loop = true, localSpace = false;
		float minLifetime = 1, maxLifetime = 2, minSize = .05f, maxSize = .15f;
		Math::Vec3 minVelocity{-1, 1, -1}, maxVelocity{1, 3, 1}, gravity{0, -9.81f, 0};
		Math::Vec4 startColor{1}, endColor{1, 1, 1, 0};
		float endSizeScale = 1, minAngularVelocity = 0, maxAngularVelocity = 0;
	};
	/**
	 * @brief 校验粒子效果。
	 *
	 * @param[in] effect 待验证生命周期、发射及粒子外观参数的效果定义。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateParticleEffect(const ParticleEffect&, std::string& error);
	/**
	 * @brief 读取粒子效果。
	 *
	 * @param[in] text 描述粒子效果参数的 JSON 文本。
	 * @param[out] output 接收粒子效果结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadParticleEffect(std::string_view, ParticleEffect&, std::string& error);
	/**
	 * @brief 写入粒子效果。
	 *
	 * @param[in] value 要写入文档的粒子效果定义。
	 * @param[out] output 成功时接收记录粒子发射和外观参数的 JSON 文本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool WriteParticleEffect(const ParticleEffect&, std::string&, std::string& error);
	class AssetManager;
	/**
	 * @brief 为粒子注册资产加载器。
	 *
	 * @param[in] assets 提供资产查找和版本访问的管理器。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RegisterParticleLoader(AssetManager&, std::string& error);

	struct ParticleBillboard {
		Math::Vec3 position{0};
		/* size 是世界空间完整边长（米），rotation 是面向相机平面内的弧度。 */
		float size = 0, rotation = 0;
		Math::Vec4 color{1};
		uint64_t serial = 0;
	};
	/* 可单独对照的确定性积分器；粒子池不持有 Scene、AssetManager 或 Renderer 指针。 */
	class CATALPA_API ParticlePool {
	public:
		/**
		 * @brief 校验效果配置和种子，并重新建立粒子池。
		 *
		 * @param[in] effect 新粒子池所采用的发射、寿命及运动参数。
		 * @param[in] seed 随机数序列的种子。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Reset(const ParticleEffect&, uint32_t seed, std::string& error);
		/**
		 * @brief 按时间步更新存活粒子并处理发射。
		 *
		 * @param[in] dt 本次模拟或采样步长，单位为秒。
		 * @param[in] emitting 发射状态。
		 * @param[in] emitter 本步发射器的局部到世界变换。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Tick(double dt, bool emitting, const Math::Mat4& emitter, std::string& error);
		/**
		 * @brief 将存活粒子的状态转换为可渲染的朝向相机四边形数据。
		 *
		 * @param[in] emitter 生成渲染快照时用于变换局部空间粒子的发射器矩阵。
		 *
		 * @return 由活动粒子生成的广告牌实例数据。
		 */
		std::vector<ParticleBillboard> Snapshot(const Math::Mat4& emitter) const;
		size_t AliveCount() const noexcept { return m_Live; }
		uint64_t DroppedCount() const noexcept { return m_Dropped; }

	private:
		struct Particle {
			bool alive = false;
			Math::Vec3 position{0}, velocity{0};
			float age = 0, lifetime = 1, size = 1, rotation = 0, angularVelocity = 0;
			uint64_t serial = 0;
		};
		float Random();
		ParticleEffect m_Effect;
		std::vector<Particle> m_Particles;
		uint32_t m_Random = 1;
		size_t m_Live = 0;
		double m_Time = 0, m_Fraction = 0;
		uint64_t m_Serial = 0, m_Dropped = 0;
	};
} /* 命名空间 Catalpa */
