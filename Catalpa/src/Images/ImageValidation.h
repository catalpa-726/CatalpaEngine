#pragma once
#include "Core/Base.h"
#include "Images/ImageAsset.h"

namespace Catalpa {
    // 校验完整声明的mip/layer/face集合、溢出/pitch/范围及格式语义；输入保持不变。
    [[nodiscard]] CATALPA_API bool ValidateImageData(const ImageData& image, std::string& error);
    // 校验资产像素和来源文本，来源只作元数据，不自行打开任何文件。
    [[nodiscard]] CATALPA_API bool ValidateImageAsset(const ImageAsset& image, std::string& error);
}
