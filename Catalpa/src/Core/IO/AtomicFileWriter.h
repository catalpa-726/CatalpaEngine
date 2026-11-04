#pragma once

#include "catpch.h"
#include "Core/Base.h"

namespace Catalpa {
    // Explicit author-document/derived output only; callers decide which targets may be overwritten.
    // Requires an existing absolute parent directory. Does not create directories or edit raw inputs.
    class CATALPA_API AtomicFileWriter {
    public:
        // Writes an exclusive same-directory temporary, flushes it, then commits one rename.
        // Failure before commit leaves the target alone; cleanup only targets our own temporary.
        // A successful rename is not a promise of power-loss durability on every filesystem.
        // Callers keep ancestor directory topology stable; holding the parent alone does not pin all ancestors.
        [[nodiscard]] static bool Write(const std::filesystem::path& absoluteTarget,
            std::span<const std::byte> bytes, std::string& error);
        [[nodiscard]] static bool Write(const std::filesystem::path& absoluteTarget,
            std::string_view text, std::string& error);
    };
}
