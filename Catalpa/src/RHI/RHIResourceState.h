#pragma once

#include "RHIHandle.h"
#include "RHIBufferDesc.h"
#include "RHIResourceAccess.h"
#include "RHITypes.h"
#include "Images/PixelFormat.h"

// 定义资源状态快照、待提交更新，以及 Buffer 初始化区间的公共操作
namespace Catalpa {

    // 字节区间；initialized 中保持有序、互不重叠且不相邻。
    struct RHIBufferRange {
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    struct RHIBufferState {
        uint64_t revision = 1;
        RHIBufferAccess access{};
        std::vector<RHIBufferRange> initialized;
    };

    // 当前只描述颜色 mip 0、layer 0。
    struct RHITextureState {
        uint64_t revision = 1;
        RHITextureAccess access{};
        bool initialized = false;
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
    };

    struct RHIBufferStateUpdate {
        BufferHandle buffer{};
        uint64_t expectedRevision = 0;
        RHIBufferState after{};
    };

    struct RHITextureStateUpdate {
        TextureViewHandle view{};
        uint64_t expectedRevision = 0;
        RHITextureState after{};
    };

    // 用减法检查边界，避免 offset + size 溢出。
    [[nodiscard]] inline bool IsValidBufferRange(uint64_t capacity, uint64_t offset, uint64_t size) noexcept
    {
        return offset <= capacity && size <= capacity - offset;
    }

    // 检查读取区间是否已初始化；调用方先验证实际 Buffer 边界。
    [[nodiscard]] inline bool RangeContains(std::span<const RHIBufferRange> ranges, uint64_t offset, uint64_t size) noexcept
    {
        if (size == 0) return true;
        if (size > std::numeric_limits<uint64_t>::max() - offset) return false;

        for (const auto& range : ranges) {
            if (range.offset > offset) return false;

            const uint64_t relative = offset - range.offset;
            if (relative <= range.size && size <= range.size - relative) return true;
        }

        return false;
    }

    // 登记已写入区间并合并相邻/重叠区间；输入范围须已检查。
    // 在实际 CPU 写入后使用时，调用方须预先 reserve(size + 1)。
    inline void MarkInitialized(std::vector<RHIBufferRange>& ranges, uint64_t offset, uint64_t size)
    {
        if (size == 0) return;

        uint64_t begin = offset;
        uint64_t end = offset + size;
        size_t first = 0;

        while (first < ranges.size() && ranges[first].offset + ranges[first].size < begin) ++first;

        size_t last = first;

        while (last < ranges.size() && ranges[last].offset <= end) {
            begin = std::min(begin, ranges[last].offset);
            end = std::max(end, ranges[last].offset + ranges[last].size);
            ++last;
        }

        if (first == last) {
            ranges.insert(ranges.begin() + static_cast<std::ptrdiff_t>(first), { begin, end - begin });
            return;
        }

        ranges[first] = { begin, end - begin };
        ranges.erase(ranges.begin() + static_cast<std::ptrdiff_t>(first + 1),
            ranges.begin() + static_cast<std::ptrdiff_t>(last));
    }

}