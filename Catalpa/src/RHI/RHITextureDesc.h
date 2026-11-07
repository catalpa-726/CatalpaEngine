#pragma once

#include "RHITypes.h"
#include "RHIResourceAccess.h"
#include "Images/PixelFormat.h"

// 定义后端无关的纹理用途、创建参数、View 参数和上传字节数计算
namespace Catalpa {

    enum class RHITextureUsage : uint32_t {
        None = 0,
        TransferSource = 1u << 0,
        TransferDestination = 1u << 1,
        Sampled = 1u << 2,
        ColorAttachment = 1u << 3,
        DepthAttachment = 1u << 4
    };

    // 组合图像用途。
    constexpr RHITextureUsage operator|(RHITextureUsage left, RHITextureUsage right) noexcept
    {
        return static_cast<RHITextureUsage>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
    }

    // 检查是否包含全部指定用途。
    [[nodiscard]] constexpr bool HasTextureUsage(RHITextureUsage usage, RHITextureUsage required) noexcept
    {
        return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(required)) == static_cast<uint32_t>(required);
    }

    struct RHITextureDesc {
        Extent2D extent{};
        PixelFormat format = PixelFormat::Unknown;
        RHITextureUsage usage = RHITextureUsage::None;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        uint32_t sampleCount = 1;
        bool cubeCompatible = false;
    };

    enum class RHITextureViewType { Texture2D, Texture2DArray, Cube };

    struct RHITextureViewDesc {
        // Unknown 表示沿用父图像格式。
        PixelFormat format = PixelFormat::Unknown;
        RHITextureSubresourceRange range{};
        RHITextureViewType type = RHITextureViewType::Texture2D;
    };

    // 计算整幅紧密排列上传数据的字节数，不表示原生图像的实际分配大小。
    [[nodiscard]] inline bool TryGetTextureUploadByteSize(Extent2D extent, PixelFormat format, uint64_t& bytes) noexcept
    {
        bytes = 0;
        const PixelFormatInfo info = GetPixelFormatInfo(format);

        if (extent.IsEmpty() || info.isDepth || info.blockWidth == 0 || info.blockHeight == 0 ||
            info.bytesPerBlock == 0)
            return false;

        const uint64_t rowBytes =
            ((uint64_t(extent.width) + info.blockWidth - 1) / info.blockWidth) * info.bytesPerBlock;
        const uint64_t rows = (uint64_t(extent.height) + info.blockHeight - 1) / info.blockHeight;

        if (rows > std::numeric_limits<uint64_t>::max() / rowBytes)
            return false;

        bytes = rowBytes * rows;
        return true;
    }

}
