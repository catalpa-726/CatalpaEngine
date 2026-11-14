#include "catpch.h"

#include "VulkanPipelineCache.h"

#include "Core/PlatformServices.h"
#include "Core/IO/AtomicFileWriter.h"
#include "Log/Log.h"
namespace Catalpa {
	namespace {
		constexpr size_t Limit = 64 * 1024 * 1024;
		/**
		 * @brief Vulkan缓存头明确规定little endian；不reinterpret_cast到可能有padding的C结构体。
		 *
		 * @param[in] b 含有小端 32 位字段的缓存字节序列。
		 * @param[in] offset 字段首字节相对 b 开头的偏移，调用前须保证剩余至少 4 字节。
		 *
		 * @return 返回从 offset 开始按小端顺序解出的 32 位无符号整数。
		 */
		uint32_t Read32(std::span<const std::byte> b, size_t offset) noexcept {
			uint32_t value = 0;
			for (size_t i = 0; i < 4; ++i)
				value |= std::to_integer<uint32_t>(b[offset + i]) << (8 * i);
			return value;
		}
	}

	/**
	 * @brief 比较原生管线缓存版本、厂商、设备与缓存 UUID，避免加载不兼容的驱动缓存。
	 *
	 * @param[in] blob 待导入的原生管线缓存字节，开头至少包含 32 字节的第一版 Vulkan 缓存头。
	 *
	 * @return 只有缓存头长度、版本及各项物理设备标识均匹配时返回 true。
	 */
	bool VulkanPipelineCache::Matches(std::span<const std::byte> blob) const noexcept {
		return blob.size() >= 32 && Read32(blob, 0) == 32 && Read32(blob, 4) == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
		    Read32(blob, 8) == m_Properties.vendorID && Read32(blob, 12) == m_Properties.deviceID &&
		    std::memcmp(blob.data() + 16, m_Properties.pipelineCacheUUID, VK_UUID_SIZE) == 0;
	}

	VulkanPipelineCache::VulkanPipelineCache(VkDevice device, const VkPhysicalDeviceProperties& properties)
	    : m_Device(device), m_Properties(properties) {
		std::vector<std::byte> blob;
		try {
			std::filesystem::path executable;
			std::string error;
			if (GetExecutableDirectory(executable, error)) {
				std::string uuid;
				constexpr char hex[] = "0123456789abcdef";
				for (auto byte : properties.pipelineCacheUUID) {
					uuid += hex[byte >> 4];
					uuid += hex[byte & 15];
				}
				m_Path = executable / "Saved" / "PipelineCache" /
				    (std::to_string(properties.vendorID) + "-" + std::to_string(properties.deviceID) + "-" +
				        std::to_string(properties.driverVersion) + "-" + uuid + ".bin");
				std::ifstream input(m_Path, std::ios::binary | std::ios::ate);
				const auto size = input ? input.tellg() : std::streampos(-1);
				if (size >= std::streampos(64) && size <= std::streampos(Limit + 32)) {
					std::vector<std::byte> stored(static_cast<size_t>(size));
					input.seekg(0);
					input.read(reinterpret_cast<char*>(stored.data()), static_cast<std::streamsize>(stored.size()));
					std::array<std::byte, 32> digest;
					const auto data = std::span<const std::byte>(stored).subspan(32);
					if (input && Matches(data) && ComputeSha256(data, digest, error) &&
					    std::equal(digest.begin(), digest.end(), stored.begin()))
						blob.assign(data.begin(), data.end());
				}
			}
		} catch (const std::exception&) {
			blob.clear();
		}
		VkPipelineCacheCreateInfo info{.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
		    .initialDataSize = blob.size(),
		    .pInitialData = blob.empty() ? nullptr : blob.data()};
		auto result = vkCreatePipelineCache(device, &info, nullptr, &m_Cache);
		if (result != VK_SUCCESS && !blob.empty()) {
			info.initialDataSize = 0;
			info.pInitialData = nullptr;
			result = vkCreatePipelineCache(device, &info, nullptr, &m_Cache);
		}
		/* 缓存是加速数据；失败显式日志并使用VK_NULL_HANDLE正常创建管线。 */
		if (result != VK_SUCCESS) {
			m_Cache = VK_NULL_HANDLE;
			CAT_CORE_WARN("Vulkan pipeline cache unavailable: {0}", static_cast<int>(result));
		}
	}

	VulkanPipelineCache::~VulkanPipelineCache() {
		if (!m_Cache)
			return;
		try {
			size_t size = 0;
			if (!m_Path.empty() && vkGetPipelineCacheData(m_Device, m_Cache, &size, nullptr) == VK_SUCCESS &&
			    size <= Limit) {
				std::vector<std::byte> blob(size);
				if (vkGetPipelineCacheData(m_Device, m_Cache, &size, blob.data()) == VK_SUCCESS) {
					blob.resize(size);
					std::array<std::byte, 32> digest;
					std::string error;
					if (Matches(blob) && ComputeSha256(blob, digest, error)) {
						std::vector<std::byte> stored(digest.begin(), digest.end());
						stored.insert(stored.end(), blob.begin(), blob.end());
						std::error_code code;
						std::filesystem::create_directories(m_Path.parent_path(), code);
						if (code || !AtomicFileWriter::Write(m_Path, stored, error))
							CAT_CORE_WARN("Pipeline cache save skipped: {0}", code ? code.message() : error);
					}
				}
			}
		} catch (const std::exception& exception) {
			CAT_CORE_WARN("Pipeline cache save skipped: {0}", exception.what());
		}
		vkDestroyPipelineCache(m_Device, m_Cache, nullptr);
	}
}
