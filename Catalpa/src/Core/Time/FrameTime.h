#pragma once

#include "catpch.h"

namespace Catalpa {

	/* 一次展示帧的时间快照，全部时长以秒计；不保存任何 World 或表现对象。 */
	struct FrameTime {
		double realDeltaSeconds = 0.0;
		double fixedDeltaSeconds = 1.0 / 60.0;
		std::uint32_t tickCount = 0;
		/* tick 从 1 开始；零 tick 帧仍给出下一个 tick 的编号。 */
		std::uint64_t firstTick = 1;
		/* 本次 Advance 完成后的累计权威模拟秒数。 */
		double simulationSeconds = 0.0;
		double interpolationAlpha = 0.0;
		/* 本帧因墙钟时长上限或补步上限而丢弃的、已乘 timeScale 的模拟秒数。 */
		double droppedSeconds = 0.0;
	};

}
