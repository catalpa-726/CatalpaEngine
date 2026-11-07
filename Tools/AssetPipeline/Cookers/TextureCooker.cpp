#include "catpch.h"
#include "AssetPipeline/Cookers/TextureCooker.h"
#include "Images/ImageValidation.h"

namespace Catalpa::Tools {
    namespace {
        double Linear(double value) {
            return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
        }
        double Encoded(double value) {
            return value <= .0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - .055;
        }
        uint8_t Byte(double value) {
            return uint8_t(std::clamp(std::lround(value * 255), 0L, 255L));
        }
        void AppendMip(ImageData& image, uint32_t mip, uint32_t width, uint32_t height,
                       std::span<const std::byte> pixels, bool compressed) {
            const uint64_t row = compressed ? uint64_t((width + 3) / 4) * 8 : uint64_t(width) * 4;
            const uint64_t rows = compressed ? (height + 3) / 4 : height;
            const auto offset = image.pixels.size();
            image.pixels.insert(image.pixels.end(), pixels.begin(), pixels.end());
            image.subresources.push_back(
                {mip, 0, 0, width, height, 1, offset, row, row * rows, pixels.size()});
        }
        std::vector<std::byte> Downsample(const std::vector<std::byte>& source, uint32_t width,
                                          uint32_t height, const ImageData& description) {
            const uint32_t nextWidth = std::max(1u, width / 2), nextHeight = std::max(1u, height / 2);
            std::vector<std::byte> pixels(size_t(nextWidth) * nextHeight * 4);
            for (uint32_t y = 0; y < nextHeight; ++y)
                for (uint32_t x = 0; x < nextWidth; ++x) {
                    const double x0 = double(x) * width / nextWidth, x1 = double(x + 1) * width / nextWidth,
                                 y0 = double(y) * height / nextHeight,
                                 y1 = double(y + 1) * height / nextHeight;
                    double sum[4]{}, weightSum = 0;
                    for (uint32_t sy = uint32_t(std::floor(y0));
                         sy < std::min(height, uint32_t(std::ceil(y1))); ++sy)
                        for (uint32_t sx = uint32_t(std::floor(x0));
                             sx < std::min(width, uint32_t(std::ceil(x1))); ++sx) {
                            const double weight = (std::min(x1, double(sx + 1)) - std::max(x0, double(sx))) *
                                                  (std::min(y1, double(sy + 1)) - std::max(y0, double(sy)));
                            const auto offset = (size_t(sy) * width + sx) * 4;
                            double sample[4];
                            for (size_t c = 0; c < 4; ++c)
                                sample[c] = std::to_integer<uint8_t>(source[offset + c]) / 255.0;
                            if (description.semantic == ImageSemantic::Normal)
                                for (size_t c = 0; c < 3; ++c)
                                    sample[c] = sample[c] * 2 - 1;
                            else if (description.colorSpace == ImageColorSpace::SRGB)
                                for (size_t c = 0; c < 3; ++c)
                                    sample[c] = Linear(sample[c]);
                            if (description.semantic == ImageSemantic::Color &&
                                description.alphaMode == ImageAlphaMode::Straight)
                                for (size_t c = 0; c < 3; ++c)
                                    sample[c] *= sample[3];
                            for (size_t c = 0; c < 4; ++c)
                                sum[c] += sample[c] * weight;
                            weightSum += weight;
                        }
                    for (auto& value : sum)
                        value /= weightSum;
                    if (description.semantic == ImageSemantic::Normal) {
                        const double length = std::hypot(sum[0], sum[1], sum[2]);
                        if (length > 1e-12)
                            for (size_t c = 0; c < 3; ++c)
                                sum[c] = sum[c] / length * .5 + .5;
                        else {
                            sum[0] = sum[1] = .5;
                            sum[2] = 1;
                        }
                    } else {
                        if (description.semantic == ImageSemantic::Color &&
                            description.alphaMode == ImageAlphaMode::Straight)
                            for (size_t c = 0; c < 3; ++c)
                                sum[c] = sum[3] > 0 ? sum[c] / sum[3] : 0;
                        if (description.colorSpace == ImageColorSpace::SRGB)
                            for (size_t c = 0; c < 3; ++c)
                                sum[c] = Encoded(std::clamp(sum[c], 0.0, 1.0));
                    }
                    const auto destination = (size_t(y) * nextWidth + x) * 4;
                    for (size_t c = 0; c < 4; ++c)
                        pixels[destination + c] = std::byte(Byte(sum[c]));
                }
            return pixels;
        }
        uint16_t Pack(const std::array<uint8_t, 3>& color) {
            return uint16_t((uint16_t(color[0] >> 3) << 11) | (uint16_t(color[1] >> 2) << 5) |
                            (color[2] >> 3));
        }
        std::array<uint8_t, 3> Unpack(uint16_t value) {
            const auto r = (value >> 11) & 31, g = (value >> 5) & 63, b = value & 31;
            return {uint8_t((r << 3) | (r >> 2)), uint8_t((g << 2) | (g >> 4)), uint8_t((b << 3) | (b >> 2))};
        }
        std::vector<std::byte> Compress(const std::vector<std::byte>& source, uint32_t width, uint32_t height,
                                        bool srgb, double& square, double& maximum, uint64_t& samples) {
            const uint32_t blocksX = (width + 3) / 4, blocksY = (height + 3) / 4;
            std::vector<std::byte> bytes(size_t(blocksX) * blocksY * 8);
            for (uint32_t by = 0; by < blocksY; ++by)
                for (uint32_t bx = 0; bx < blocksX; ++bx) {
                    std::array<std::array<uint8_t, 3>, 16> colors;
                    std::array<uint8_t, 3> low{255, 255, 255}, high{};
                    for (uint32_t y = 0; y < 4; ++y)
                        for (uint32_t x = 0; x < 4; ++x) {
                            const auto pixel = (size_t(std::min(height - 1, by * 4 + y)) * width +
                                                std::min(width - 1, bx * 4 + x)) *
                                               4;
                            for (size_t c = 0; c < 3; ++c) {
                                const auto value = std::to_integer<uint8_t>(source[pixel + c]);
                                colors[y * 4 + x][c] = value;
                                low[c] = std::min(low[c], value);
                                high[c] = std::max(high[c], value);
                            }
                        }
                    uint16_t first = Pack(high), second = Pack(low);
                    if (first < second)
                        std::swap(first, second);
                    if (first == second) {
                        if (first < 65535)
                            ++first;
                        else
                            --second;
                    }
                    std::array<std::array<uint8_t, 3>, 4> palette;
                    palette[0] = Unpack(first);
                    palette[1] = Unpack(second);
                    for (size_t c = 0; c < 3; ++c) {
                        palette[2][c] = uint8_t((2 * uint32_t(palette[0][c]) + palette[1][c]) / 3);
                        palette[3][c] = uint8_t((uint32_t(palette[0][c]) + 2 * uint32_t(palette[1][c])) / 3);
                    }
                    uint32_t selectors = 0;
                    for (size_t i = 0; i < 16; ++i) {
                        double best = std::numeric_limits<double>::infinity();
                        uint32_t selected = 0;
                        for (uint32_t p = 0; p < 4; ++p) {
                            double distance = 0;
                            for (size_t c = 0; c < 3; ++c) {
                                double a = colors[i][c] / 255.0, b = palette[p][c] / 255.0;
                                if (srgb) {
                                    a = Linear(a);
                                    b = Linear(b);
                                }
                                distance += (a - b) * (a - b);
                            }
                            if (distance < best) {
                                best = distance;
                                selected = p;
                            }
                        }
                        selectors |= selected << (2 * i);
                        const uint32_t x = bx * 4 + uint32_t(i % 4), y = by * 4 + uint32_t(i / 4);
                        if (x < width && y < height)
                            for (size_t c = 0; c < 3; ++c) {
                                double a = colors[i][c] / 255.0, b = palette[selected][c] / 255.0;
                                if (srgb) {
                                    a = Linear(a);
                                    b = Linear(b);
                                }
                                const double difference = std::abs(a - b);
                                square += difference * difference;
                                maximum = std::max(maximum, difference);
                                ++samples;
                            }
                    }
                    const auto offset = (size_t(by) * blocksX + bx) * 8;
                    bytes[offset] = std::byte(first & 255);
                    bytes[offset + 1] = std::byte(first >> 8);
                    bytes[offset + 2] = std::byte(second & 255);
                    bytes[offset + 3] = std::byte(second >> 8);
                    for (size_t i = 0; i < 4; ++i)
                        bytes[offset + 4 + i] = std::byte((selectors >> (8 * i)) & 255);
                }
            return bytes;
        }
    } // namespace

    bool TextureCooker::Prepare(const ImageAsset& source, ImageAsset& output, TextureCookReport& report,
                                std::string& error) try {
        if (!ValidateImageAsset(source, error))
            return false;
        const auto& input = source.image;
        if (input.pixels.size() > 80 * 1024 * 1024) {
            error = "Texture Cook source pixels exceed the 80 MiB bounded workspace input";
            return false;
        }
        if (input.dimension != ImageDimension::Texture2D || input.arrayLayers != 1 || input.mipLevels != 1 ||
            (input.format != PixelFormat::RGBA8_UNorm && input.format != PixelFormat::RGBA8_SRGB)) {
            output = source;
            report = {false, false, 0, 0,
                      "Existing/float/non-RGBA8 layouts preserved; " + std::string(Signature)};
            error.clear();
            return true;
        }
        const auto& base = input.subresources.front();
        std::vector<std::byte> pixels(size_t(input.width) * input.height * 4);
        for (uint32_t y = 0; y < input.height; ++y)
            std::memcpy(pixels.data() + size_t(y) * input.width * 4,
                        input.pixels.data() + base.offset + size_t(y) * base.rowPitch,
                        size_t(input.width) * 4);
        bool opaque = input.semantic == ImageSemantic::Color;
        for (size_t i = 3; i < pixels.size(); i += 4)
            opaque &= pixels[i] == std::byte{255};
        std::vector<std::vector<std::byte>> levels;
        std::vector<std::pair<uint32_t, uint32_t>> sizes;
        uint32_t width = input.width, height = input.height;
        for (;;) {
            levels.push_back(pixels);
            sizes.emplace_back(width, height);
            if (width == 1 && height == 1)
                break;
            pixels = Downsample(pixels, width, height, input);
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
        }
        ImageAsset derived = source;
        derived.image.pixels.clear();
        derived.image.subresources.clear();
        derived.image.mipLevels = uint32_t(levels.size());
        TextureCookReport result;
        result.mipmaps = levels.size() > 1;
        result.policy = std::string(Signature) + "; source unchanged; linear area filter";
        if (opaque) {
            double square = 0, maximum = 0;
            uint64_t samples = 0;
            std::vector<std::vector<std::byte>> blocks;
            for (size_t i = 0; i < levels.size(); ++i)
                blocks.push_back(Compress(levels[i], sizes[i].first, sizes[i].second,
                                          input.colorSpace == ImageColorSpace::SRGB, square, maximum,
                                          samples));
            result.linearRmse = samples ? std::sqrt(square / double(samples)) : 0;
            result.maximumLinearError = maximum;
            if (result.linearRmse <= .08) {
                result.compressed = true;
                derived.image.format = input.colorSpace == ImageColorSpace::SRGB ? PixelFormat::BC1_SRGB
                                                                                 : PixelFormat::BC1_UNorm;
                derived.image.alphaMode = ImageAlphaMode::Opaque;
                for (size_t i = 0; i < blocks.size(); ++i)
                    AppendMip(derived.image, uint32_t(i), sizes[i].first, sizes[i].second, blocks[i], true);
                result.policy += "; BC1 opaque, measured linear RGB RMSE <=0.08";
            } else
                result.policy += "; BC1 rejected by quality bound; RGBA8 retained";
        } else
            result.policy += input.semantic == ImageSemantic::Normal
                                 ? "; normal vectors renormalized only in derived mip levels; RGBA8 retained"
                                 : "; transparent/data RGBA8 retained; straight color filtered premultiplied "
                                   "then unpremultiplied";
        if (!result.compressed)
            for (size_t i = 0; i < levels.size(); ++i)
                AppendMip(derived.image, uint32_t(i), sizes[i].first, sizes[i].second, levels[i], false);
        if (!ValidateImageAsset(derived, error))
            return false;
        output = std::move(derived);
        report = std::move(result);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    bool TextureCooker::Encode(const ImageAsset& source, std::vector<std::byte>& output, std::string& error,
                               TextureCookReport* report) {
        ImageAsset derived;
        TextureCookReport result;
        if (!Prepare(source, derived, result, error) ||
            !ImageAssetLoader::EncodePayload(derived, output, error))
            return false;
        if (report)
            *report = std::move(result);
        return true;
    }
} // namespace Catalpa::Tools
