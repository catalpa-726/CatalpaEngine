#pragma once

namespace Catalpa {

	// 保留 Layer 的墙钟秒接口；固定模拟 tick 统一使用 Core/Time/FrameTime。
	class Deltatime
	{
	public:
		Deltatime(float time = 0.0f) : m_Time(time) {}

		operator float() const { return m_Time; }

		float GetSeconds() const { return m_Time; }
		float GetMilliseconds() const { return m_Time * 1000.0f; }

	private:
		float m_Time;
	};

}
