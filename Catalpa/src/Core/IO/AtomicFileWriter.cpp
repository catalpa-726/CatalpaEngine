#include "catpch.h"
#include "Core/IO/AtomicFileWriter.h"

namespace Catalpa::FileSystemDetail {
    bool WriteAtomic(const std::filesystem::path&, std::span<const std::byte>, std::string&);
}

namespace Catalpa {
    bool AtomicFileWriter::Write(const std::filesystem::path& target,
        std::span<const std::byte> bytes, std::string& error)
    {
        try { return FileSystemDetail::WriteAtomic(target, bytes, error); }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    bool AtomicFileWriter::Write(const std::filesystem::path& target, std::string_view text, std::string& error)
    {
        return Write(target, std::as_bytes(std::span(text.data(), text.size())), error);
    }
}
