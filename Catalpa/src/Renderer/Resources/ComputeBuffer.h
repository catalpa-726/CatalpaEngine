#pragma once
#include "RHI/RHIDevice.h"
namespace Catalpa {
	/* 资源由帧槽持有。已录制的命令保活旧句柄，CPU 只能在该槽工作完成后重写上传数据。 */
	struct ComputeBuffer {
		RHIDevice* device = nullptr;
		BufferHandle buffer{};
		uint64_t bytes = 0;
		ComputeBuffer() = default;
		ComputeBuffer(const ComputeBuffer&) = delete;
		ComputeBuffer& operator=(const ComputeBuffer&) = delete;
		ComputeBuffer(ComputeBuffer&& other) noexcept { *this = std::move(other); }
		/**
		 * @brief 释放目的对象原先持有的缓冲，并接管源对象的缓冲句柄和字节数。
		 *
		 * @param[in,out] other 转出缓冲句柄和容量的源对象，转移后其句柄和容量清零。
		 *
		 * @return 返回当前对象的引用；自移动时保留原有缓冲。
		 */
		ComputeBuffer& operator=(ComputeBuffer&& other) noexcept {
			if (this != &other) {
				Reset();
				device = other.device;
				buffer = std::exchange(other.buffer, {});
				bytes = std::exchange(other.bytes, 0);
			}
			return *this;
		}
		~ComputeBuffer() { Reset(); }
		/**
		 * @brief 归还当前缓冲区句柄并清空已登记的字节数。
		 */
		void Reset() noexcept {
			if (device)
				device->ReleaseBuffer(std::exchange(buffer, {}));
			bytes = 0;
		}
		/**
		 * @brief 复用大小相同的缓冲区或创建匹配所需字节数的新缓冲区。
		 *
		 * @param[in] owner 分配缓冲区的逻辑设备。
		 * @param[in] size 需要的非零字节数。
		 * @param[in] usage 新缓冲区的访问用途。
		 * @param[in] memory 新缓冲区所在的内存域。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		RHIStatus Ensure(RHIDevice& owner, uint64_t size, RHIBufferUsage usage, RHIMemoryDomain memory) {
			if (!size || size > owner.GetCapabilities().maxStorageBufferRange)
				return {RHIErrorCode::Unsupported, 0, "Compute/buffer bound"};
			if (buffer.HasValue() && bytes == size)
				return {};
			BufferHandle replacement;
			if (const auto status = owner.CreateBuffer({size, usage, memory}, replacement); !status.IsSuccess())
				return status;
			Reset();
			device = &owner;
			buffer = replacement;
			bytes = size;
			return {};
		}
	};
} /* 命名空间 Catalpa */
