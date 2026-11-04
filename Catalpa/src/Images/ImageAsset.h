#pragma once
#include "Images/ImageData.h"

namespace Catalpa {
    // CPU图像内容由AssetVersionRef保活，身份/内容版本位于外层；不持有采样器或GPU纹理。
    struct ImageAsset { ImageData image; std::string sourceUri; };
}
