#pragma once

#include "Core/Memory/GenerationalHandle.h"

namespace Catalpa {

	struct SceneTag;
	/* Manager身份与槽位代次共同校验；卸载后不因槽位复用而重新生效。 */
	struct SceneHandle {
		uint64_t owner = 0;
		GenerationalHandle<SceneTag> slot;
		[[nodiscard]] constexpr bool HasValue() const noexcept { return owner != 0 && slot.HasValue(); }
		constexpr bool operator==(const SceneHandle&) const = default;
	};

}
