#pragma once

#include "RHIHandle.h"
#include "RHICompletionPoint.h"
#include "RHIBufferDesc.h"
#include "RHIResourceAccess.h"
#include "RHITypes.h"
#include "RHITextureDesc.h"
#include "Images/PixelFormat.h"
#include "RHIQueueTypes.h"
#include "RHICompletionSet.h"

/* 定义资源状态快照、待提交更新，以及 Buffer 初始化区间的公共操作 */
namespace Catalpa {
	struct RHIResourceOwnership {
		QueueId owner, releasedTo;
		RHICompletionPoint releasePoint;
		RHITextureLayout releaseOldLayout = RHITextureLayout::Undefined, releaseNewLayout = RHITextureLayout::Undefined;
		[[nodiscard]] bool IsReleased() const noexcept { return releasedTo.HasValue(); }
	};
	struct RHIResourceFrontier {
		RHICompletionPoint writer;
		RHICompletionSet readers;
	};

	/*
	 * 字节区间；initialized 中保持有序、互不重叠且不相邻。
	 * 范围以字节计。检查 size <= capacity - offset 前须确认 offset <= capacity，以免加法溢出。
	 */
	struct RHIBufferRange {
		uint64_t offset = 0;
		uint64_t size = 0;
	};

	struct RHIBufferState {
		uint64_t revision = 1;
		RHIBufferAccess access{};
		/* 实际写入后合并已验证的区间。若随后须无分配地登记结果，应在写入前预留一个额外位置。 */
		std::vector<RHIBufferRange> initialized;
		RHICompletionPoint lastUse{};
		RHIResourceOwnership ownership;
		RHIResourceFrontier frontier;
	};

	/* 布局按整个图像跟踪；初始化区间按 layer * mipLevels + mip 跟踪。 */
	struct RHITextureState {
		uint64_t revision = 1;
		RHITextureAccess access{};
		bool initialized = false;
		std::vector<RHIBufferRange> initializedSubresources;
		RHICompletionPoint lastUse{};
		RHIResourceOwnership ownership;
		RHIResourceFrontier frontier;
	};

	struct RHIBufferStateSnapshot {
		RHIBufferDesc desc{};
		RHIBufferState state{};
	};

	struct RHITextureStateSnapshot {
		Extent2D extent{};
		PixelFormat format = PixelFormat::Unknown;
		uint32_t sampleCount = 1;
		RHITextureState state{};
		RHITextureUsage usage = RHITextureUsage::None;
		bool presentable = false;
		uint32_t mipLevels = 1;
		uint32_t arrayLayers = 1;
	};

	struct RHIBufferStateUpdate {
		BufferHandle buffer{};
		uint64_t expectedRevision = 0;
		RHIBufferState after{};
		bool writes = true;
	};

	struct RHITextureStateUpdate {
		TextureViewHandle view{};
		uint64_t expectedRevision = 0;
		RHITextureState after{};
		bool writes = true;
	};

	[[nodiscard]] inline bool IsValidBufferRange(uint64_t capacity, uint64_t offset, uint64_t size) noexcept {
		return offset <= capacity && size <= capacity - offset;
	}

	/**
	 * @brief 检查请求的连续字节范围是否已被已初始化区间覆盖。
	 *
	 * @param[in] ranges 已登记的缓冲区初始化区间。
	 * @param[in] offset 请求区间的首字节偏移。
	 * @param[in] size 要检查的字节数。
	 *
	 * @return 布尔结果，true 表示上述操作或判断成立。
	 */
	[[nodiscard]] inline bool RangeContains(
	    std::span<const RHIBufferRange> ranges, uint64_t offset, uint64_t size) noexcept {
		if (size == 0)
			return true;
		if (size > std::numeric_limits<uint64_t>::max() - offset)
			return false;

		for (const auto& range : ranges) {
			if (range.offset > offset)
				return false;

			const uint64_t relative = offset - range.offset;
			if (relative <= range.size && size <= range.size - relative)
				return true;
		}

		return false;
	}

	/**
	 * @brief 把新写入的字节区间与已有相邻或重叠区间合并，保留有序的已初始化范围。
	 *
	 * @param[in,out] ranges 按起始位置排序的已初始化区间；合并后仍按起点排序。
	 * @param[in] offset 新写入区间起点相对于缓冲区首字节的偏移。
	 * @param[in] size 本次写入的字节数，为零时跳过；调用方须确保区间末端可由 uint64_t 表示。
	 */
	inline void MarkInitialized(std::vector<RHIBufferRange>& ranges, uint64_t offset, uint64_t size) {
		if (size == 0)
			return;

		uint64_t begin = offset;
		uint64_t end = offset + size;
		size_t first = 0;

		while (first < ranges.size() && ranges[first].offset + ranges[first].size < begin)
			++first;

		size_t last = first;

		while (last < ranges.size() && ranges[last].offset <= end) {
			begin = std::min(begin, ranges[last].offset);
			end = std::max(end, ranges[last].offset + ranges[last].size);
			++last;
		}

		if (first == last) {
			ranges.insert(ranges.begin() + static_cast<std::ptrdiff_t>(first), {begin, end - begin});
			return;
		}

		ranges[first] = {begin, end - begin};
		ranges.erase(ranges.begin() + static_cast<std::ptrdiff_t>(first + 1),
		    ranges.begin() + static_cast<std::ptrdiff_t>(last));
	}

}
