#pragma once
#include "catpch.h"
#include "Images/PixelFormat.h"

namespace Catalpa {
    // CPU图像维度和语义不代表设备采样/附件能力；格式身份只由PixelFormat定义。
    enum class ImageDimension { Texture2D, Texture3D, Cube };
    enum class ImageColorSpace { Linear, SRGB };
    enum class ImageAlphaMode { Opaque, Straight, Premultiplied };
    enum class ImageSemantic { Color, Normal, Data, Depth };
}
