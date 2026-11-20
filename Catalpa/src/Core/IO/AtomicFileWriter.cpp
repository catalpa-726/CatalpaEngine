#include "catpch.h"

#include "Core/IO/AtomicFileWriter.h"

namespace Catalpa::FileSystemDetail {
	bool WriteAtomic(const std::filesystem::path&, std::span<const std::byte>, bool replaceExisting, std::string&);
}

namespace Catalpa {
	namespace {
		bool Publish(const std::filesystem::path& target, std::span<const std::byte> bytes,
		    bool replaceExisting, std::string& error) try {
			return FileSystemDetail::WriteAtomic(target, bytes, replaceExisting, error);
		} catch (const std::exception& exception) {
			error = exception.what();
			return false;
		}
	}

	bool AtomicFileWriter::Write(
	    const std::filesystem::path& target, std::span<const std::byte> bytes, std::string& error) {
		return Publish(target, bytes, true, error);
	}

	bool AtomicFileWriter::CreateNew(const std::filesystem::path& target, std::string_view text, std::string& error) {
		return Publish(target, std::as_bytes(std::span(text.data(), text.size())), false, error);
	}

	bool AtomicFileWriter::Write(const std::filesystem::path& target, std::string_view text, std::string& error) {
		return Write(target, std::as_bytes(std::span(text.data(), text.size())), error);
	}
}
