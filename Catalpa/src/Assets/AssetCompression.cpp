#include "catpch.h"

#include "Assets/AssetCompression.h"

#include "Core/Serialization/BinaryArchive.h"
#include "Core/PlatformServices.h"

#include <zstd.h>

namespace Catalpa {
	namespace {
		constexpr std::array<std::byte, 8> Magic{std::byte{'C'}, std::byte{'A'}, std::byte{'T'}, std::byte{'A'},
		    std::byte{'S'}, std::byte{'S'}, std::byte{'E'}, std::byte{'T'}};
	}

	bool WriteCompressedAssetContainer(
	    const AssetContainer& value, std::vector<std::byte>& out, std::string& error, size_t limit) {
		try {
			std::vector<std::byte> raw;
			if (!WriteAssetContainer(value, raw, error, limit))
				return false;
			const auto bound = ZSTD_compressBound(raw.size());
			if (ZSTD_isError(bound) || bound > limit) {
				out.swap(raw);
				error.clear();
				return true;
			}
			std::vector<std::byte> compressed(bound);
			const auto length = ZSTD_compress(compressed.data(), compressed.size(), raw.data(), raw.size(), 3);
			if (ZSTD_isError(length)) {
				error = ZSTD_getErrorName(length);
				return false;
			}
			/* 小文件或不可压缩内容维持原格式，不扩大输出，也不改原载荷。 */
			if (length + 64 >= raw.size()) {
				out.swap(raw);
				error.clear();
				return true;
			}
			compressed.resize(length);
			std::array<std::byte, 32> digest;
			if (!ComputeSha256(raw, digest, error))
				return false;
			BinaryWriter writer(limit);
			if (!writer.Bytes(Magic) || !writer.U32(2) || !writer.U32(0x01020304) || !writer.U64(raw.size()) ||
			    !writer.U64(compressed.size()) || !writer.Bytes(digest) || !writer.Bytes(compressed)) {
				error = writer.Error();
				return false;
			}
			out = writer.Data();
			error.clear();
			return true;
		} catch (const std::exception& e) {
			error = e.what();
			return false;
		}
	}

	bool ReadCompressedAssetContainer(
	    std::span<const std::byte> input, AssetContainer& out, std::string& error, size_t limit) {
		try {
			BinaryReader reader(input);
			std::span<const std::byte> magic, digest, compressed;
			uint32_t format = 0, endian = 0;
			uint64_t decodedSize = 0, storedSize = 0;
			if (limit > std::numeric_limits<size_t>::max() - 128 * 1024 || !reader.Bytes(8, magic) ||
			    !std::equal(magic.begin(), magic.end(), Magic.begin()) || !reader.U32(format) || format != 2 ||
			    !reader.U32(endian) || endian != 0x01020304 || !reader.U64(decodedSize) || decodedSize < 12 ||
			    decodedSize > limit + 128 * 1024 || !reader.U64(storedSize) || !reader.Bytes(32, digest) ||
			    storedSize != reader.Remaining() || !reader.Bytes(size_t(storedSize), compressed) || !reader.AtEnd()) {
				error = "Invalid compressed asset size, version or byte order";
				return false;
			}
			if (ZSTD_getFrameContentSize(compressed.data(), compressed.size()) != decodedSize ||
			    ZSTD_findFrameCompressedSize(compressed.data(), compressed.size()) != storedSize) {
				error = "Compressed asset requires exactly one size-declared zstd frame";
				return false;
			}
			std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> context(ZSTD_createDCtx(), ZSTD_freeDCtx);
			if (!context) {
				error = "Cannot allocate zstd decoder";
				return false;
			}
			const auto configured = ZSTD_DCtx_setParameter(context.get(), ZSTD_d_windowLogMax, 27);
			if (ZSTD_isError(configured)) {
				error = ZSTD_getErrorName(configured);
				return false;
			}
			std::vector<std::byte> raw(static_cast<size_t>(decodedSize));
			const auto decoded =
			    ZSTD_decompressDCtx(context.get(), raw.data(), raw.size(), compressed.data(), compressed.size());
			if (ZSTD_isError(decoded) || decoded != raw.size()) {
				error = "Asset decompression failed or size differs";
				return false;
			}
			std::array<std::byte, 32> actual;
			if (!ComputeSha256(raw, actual, error))
				return false;
			if (!std::equal(digest.begin(), digest.end(), actual.begin()) || raw[8] != std::byte{1} ||
			    raw[9] != std::byte{0} || raw[10] != std::byte{0} || raw[11] != std::byte{0}) {
				error = "Compressed asset digest mismatch or nested compression";
				return false;
			}
			return AssetContainerReader::Read(raw, out, error, limit);
		} catch (const std::exception& e) {
			error = e.what();
			return false;
		}
	}
} /* 命名空间 Catalpa */
