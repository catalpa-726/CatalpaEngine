#include "catpch.h"

#include "TestRunner.h"
#include "Core/Id/StableIdGenerator.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Core/IO/AtomicFileWriter.h"

#if defined(CATALPA_PLATFORM_WINDOW)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winioctl.h>
#endif

namespace Catalpa::Tests {
	namespace {
		/*
		 * 所有写入均位于本次调用独占创建的目录中。
		 * 仅清理明确列出的文件和目录，遇到重解析点不向下递归。
		 */
		struct TemporaryTree {
			std::filesystem::path root;
			std::vector<std::filesystem::path> paths;

			~TemporaryTree() {
				std::error_code ignored;
				for (auto it = paths.rbegin(); it != paths.rend(); ++it)
					std::filesystem::remove(*it, ignored);
				if (!root.empty())
					std::filesystem::remove(root, ignored);
			}

			bool Create(std::string& error) {
				const auto temporaryParent = std::filesystem::canonical(std::filesystem::temp_directory_path());
				for (unsigned i = 0; i < 8; ++i) {
					StableId id;
					if (!GenerateStableId(id, error))
						return false;
					auto candidate = temporaryParent / ("Catalpa-IO-" + id.ToString());
					if (std::filesystem::create_directory(candidate)) {
						root = std::move(candidate);
						return true;
					}
				}
				error = "Cannot reserve exclusive IO test directory";
				return false;
			}

			std::filesystem::path Directory(std::string_view name) {
				auto result = root / name;
				if (!std::filesystem::create_directory(result))
					throw std::runtime_error("IO fixture directory already exists");
				paths.push_back(result);
				return result;
			}

			bool File(const std::filesystem::path& file, std::string_view text) {
				paths.push_back(file);
				std::ofstream stream(file, std::ios::binary | std::ios::trunc);
				stream.write(text.data(), static_cast<std::streamsize>(text.size()));
				stream.close();
				return !stream.fail();
			}
		};

		std::string Text(const std::vector<std::byte>& bytes) {
			if (bytes.empty())
				return {};
			return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		}

#if defined(CATALPA_PLATFORM_WINDOW)
		/**
		 * @brief 在测试目录内创建指向目标目录的 Windows 联接。
		 * 创建目录联接不需要符号链接权限；目标目录和联接均由测试持有。
		 *
		 * @param[in] link 测试目录中要创建的联接点路径。
		 * @param[in] target 联接点应指向的另一个测试目录。
		 *
		 * @return 成功建立指向目标目录的 Windows 联接时为 true。
		 */
		bool MakeJunction(const std::filesystem::path& link, const std::filesystem::path& target) {
			struct JunctionHeader {
				DWORD tag;
				WORD length, reserved;
				WORD substituteOffset, substituteLength, printOffset, printLength;
			};
			static_assert(sizeof(JunctionHeader) == 16);
			const auto print = target.native();
			const auto substitute = print.starts_with(L"\\\\") ? L"\\??\\UNC\\" + print.substr(2) : L"\\??\\" + print;
			const size_t pathBytes = (substitute.size() + print.size() + 2) * sizeof(wchar_t);
			if (pathBytes + 8 > std::numeric_limits<WORD>::max())
				return false;
			std::vector<std::byte> storage(sizeof(JunctionHeader) + pathBytes);
			auto* header = ::new (static_cast<void*>(storage.data())) JunctionHeader{};
			header->tag = IO_REPARSE_TAG_MOUNT_POINT;
			header->length = static_cast<WORD>(8 + pathBytes);
			header->substituteLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
			header->printOffset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t));
			header->printLength = static_cast<WORD>(print.size() * sizeof(wchar_t));
			auto* data = storage.data() + sizeof(JunctionHeader);
			std::memcpy(data, substitute.c_str(), (substitute.size() + 1) * sizeof(wchar_t));
			std::memcpy(data + header->printOffset, print.c_str(), (print.size() + 1) * sizeof(wchar_t));
			const HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
			    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
				return false;
			DWORD returned = 0;
			const bool success = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, storage.data(),
			                         static_cast<DWORD>(storage.size()), nullptr, 0, &returned, nullptr) != FALSE;
			CloseHandle(handle);
			return success;
		}
#endif
	}

	void RunVirtualFileSystemTests(TestContext& context) {
		std::string error;
		VirtualPath path;
		const auto setPath = [&](std::string_view text) {
			context.Check(VirtualPath::Parse(text, path, error), "Test virtual locator parses");
		};
		context.Check(
		    VirtualPath::Parse("content:/Models/mesh.bin", path, error), "VFS accepts an explicit UTF-8 locator");
		context.Check(path.MountName() == "content" && path.RelativePath() == "Models/mesh.bin",
		    "VFS keeps mount and relative spelling separate");
		const VirtualPath original = path;
		const std::array invalid = {"", "content:/", "Content:/x", "content:x", ":/x", "content://x", "content:/a//x",
		    "content:/a/", "content:/./x", "content:/a/../x", "content:/a\\x", "content:/x:stream", "content:/CON",
		    "content:/nul.txt", "content:/COM1.bin", "content:/LPT9", "content:/CON .txt", "content:/x.", "content:/x ",
		    "content:/a?b", "content:/a*b", "content:/a|b", "content:/a<b", "content:/\xC0\xAF",
		    "content:/\xED\xA0\x80", "content:/\xF4\x90\x80\x80", "content:/\xE2\x82"};
		for (const auto* value : invalid) {
			context.Check(!VirtualPath::Parse(value, path, error) && path == original && !error.empty(),
			    "Invalid virtual path preserves its previous value");
		}
		const std::string embeddedNull("content:/a\0b", 12);
		context.Check(!VirtualPath::Parse(embeddedNull, path, error), "VFS rejects embedded NUL");
		context.Check(VirtualPath::Parse("content:/\xE6\xA8\xA1\xE5\x9E\x8B.bin", path, error),
		    "VFS accepts scalar UTF-8 file names");

		try {
			TemporaryTree tree;
			if (!tree.Create(error)) {
				context.Check(false, "Reserve exclusive IO test directory");
				return;
			}
			const auto low = tree.Directory("low");
			const auto high = tree.Directory("high");
			const auto other = tree.Directory("other");
			context.Check(tree.File(low / "shared.txt", "low") && tree.File(high / "shared.txt", "high"),
			    "Prepare overlay files in test-owned directories");
			context.Check(tree.File(low / "fallback.txt", "fallback") && tree.File(high / "large.txt", "123456"),
			    "Prepare bounded-read files");
			context.Check(tree.File(low / "large.txt", "x") && tree.File(low / "Case.txt", "case") &&
			        tree.File(low / "empty.txt", ""),
			    "Prepare alias and empty-file cases");
			context.Check(tree.File(low / std::filesystem::path(L"\u6A21\u578B.bin"), "utf8"),
			    "Prepare a Unicode file name without locale-dependent encoding");
			context.Check(tree.File(other / "secret.txt", "fixture only"), "Prepare test-owned outside-root data");
			VirtualFileSystem vfs;
			std::vector<std::byte> output{std::byte{0x7F}};
			const auto sentinel = output;
			setPath("content:/shared.txt");
			context.Check(
			    !vfs.ReadFile(path, 100, output, error) && output == sentinel, "Unconfigured mount preserves output");
			context.Check(
			    !vfs.MountReadOnly("content", "relative", 0, error), "VFS requires an absolute physical root");
			context.Check(vfs.MountReadOnly("content", low, 0, error) && vfs.MountReadOnly("content", high, 10, error),
			    "VFS accepts explicit priority overlays");
			context.Check(
			    !vfs.MountReadOnly("content", other, 10, error) && !vfs.MountReadOnly("content", high, 20, error),
			    "VFS rejects duplicate priorities and roots");
			context.Check(
			    vfs.ReadFile(path, 100, output, error) && Text(output) == "high", "Higher priority root wins");
			setPath("content:/fallback.txt");
			context.Check(vfs.ReadFile(path, 100, output, error) && Text(output) == "fallback",
			    "Missing upper file falls back to lower root");
			output = sentinel;
			setPath("content:/large.txt");
			context.Check(!vfs.ReadFile(path, 2, output, error) && output == sentinel,
			    "Oversize upper file fails without lower-root fallback or output loss");
			setPath("content:/case.txt");
			context.Check(
			    !vfs.ReadFile(path, 100, output, error) && output == sentinel, "Windows case alias is rejected");
			setPath("content:/empty.txt");
			context.Check(
			    vfs.ReadFile(path, 0, output, error) && output.empty(), "Zero byte limit accepts an empty file");
			setPath("content:/\xE6\xA8\xA1\xE5\x9E\x8B.bin");
			context.Check(vfs.ReadFile(path, 100, output, error) && Text(output) == "utf8",
			    "UTF-8 virtual path resolves a real Unicode file name");
			setPath("content:/missing.txt");
			output = sentinel;
			context.Check(
			    !vfs.ReadFile(path, 100, output, error) && output == sentinel, "Missing file leaves output unchanged");
			VirtualFileSystem remapped;
			context.Check(remapped.MountReadOnly("content", other, 0, error),
			    "Another host supplies a replacement root without Box coupling");
			setPath("content:/secret.txt");
			context.Check(remapped.ReadFile(path, 100, output, error) && Text(output) == "fixture only",
			    "Explicit replacement root supplies the same logical locator");

			const auto saved = low / "saved.json";
			tree.paths.push_back(saved);
			const bool createdSave = AtomicFileWriter::Write(saved, "first", error);
			context.Check(createdSave, std::string("Atomic save creates an explicit author document: ") + error);
			const bool replacedSave = AtomicFileWriter::Write(saved, "second", error);
			context.Check(replacedSave, std::string("Atomic save replaces an existing author document: ") + error);
			setPath("content:/saved.json");
			context.Check(vfs.ReadFile(path, 100, output, error) && Text(output) == "second",
			    "Atomic save publishes all new bytes");
			context.Check(
			    !AtomicFileWriter::Write("relative.json", "bad", error), "Atomic save rejects relative paths");
			context.Check(!AtomicFileWriter::Write(low, "bad", error), "Atomic save rejects directory targets");
#if defined(CATALPA_PLATFORM_WINDOW)
			const HANDLE locked =
			    CreateFileW(saved.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
			context.Check(locked != INVALID_HANDLE_VALUE, "Hold target open to exercise a commit failure");
			if (locked != INVALID_HANDLE_VALUE) {
				const bool replacedLocked = AtomicFileWriter::Write(saved, "must not replace", error);
				context.Check(!replacedLocked && !error.empty(),
				    "A locked target reports atomic commit failure with a diagnostic");
				CloseHandle(locked);
				context.Check(vfs.ReadFile(path, 100, output, error) && Text(output) == "second",
				    "Failed commit preserves the original document");
			}
			/* 完整Win32目标路径必须按UTF-16字节数传递，并支持空格/Unicode文件名。 */
			const auto unicodeSave = low / std::filesystem::path(L"\u4FDD\u5B58 \u6587\u6863.json");
			tree.paths.push_back(unicodeSave);
			const bool savedUnicode = AtomicFileWriter::Write(unicodeSave, "unicode document", error);
			context.Check(savedUnicode, std::string("Atomic save accepts a Unicode file name with spaces: ") + error);
			setPath("content:/\xE4\xBF\x9D\xE5\xAD\x98 \xE6\x96\x87\xE6\xA1\xA3.json");
			context.Check(vfs.ReadFile(path, 100, output, error) && Text(output) == "unicode document",
			    "Atomic Unicode save publishes bytes at the exact requested name");
			const auto junction = low / "jump";
			if (!std::filesystem::create_directory(junction))
				throw std::runtime_error("IO fixture junction directory already exists");
			tree.paths.push_back(junction);
			const bool junctionReady = MakeJunction(junction, other);
			context.Check(junctionReady, "Create test-owned directory junction for reparse escape verification");
			if (junctionReady) {
				setPath("content:/jump/secret.txt");
				output = sentinel;
				context.Check(!vfs.ReadFile(path, 100, output, error) && output == sentinel,
				    "Reparse target outside its mounted root is rejected");
				context.Check(!AtomicFileWriter::Write(junction, "bad", error), "Atomic writer rejects reparse leaves");
			}
#endif
			bool leakedTemporary = false;
			for (const auto& entry : std::filesystem::directory_iterator(low)) {
				if (entry.path().filename().native().starts_with(std::filesystem::path(".catalpa-write-").native()))
					leakedTemporary = true;
			}
			context.Check(!leakedTemporary, "Successful and failed saves leave no owned temporary behind");
		} catch (const std::exception&) {
			context.Check(false, "IO fixtures complete without filesystem exceptions");
		}
	}
}
