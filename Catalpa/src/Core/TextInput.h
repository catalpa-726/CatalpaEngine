#pragma once
#include <cstdint>
#include <string>
namespace Catalpa {
	/* 坐标由调用方映射到主窗口逻辑 client 空间；UI 内部快照使用目标像素空间。 */
	struct TextInputSnapshot {
		bool active = false;
		float x = 0, y = 0, width = 1, height = 1;
	};
	struct TextComposition {
		enum class Phase { Start, Update, Commit, Cancel };
		Phase phase = Phase::Start;
		std::string text;
		uint32_t caret = 0; /* 偏移以 Unicode 标量为单位，与 UTF-16 代码单元计数不同。 */
	};
} /* 命名空间 Catalpa */
