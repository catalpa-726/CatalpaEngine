#include "catpch.h"
#include "TestRunner.h"
#include "AssetPipeline/Cookers/ModelLodBuilder.h"
#include "AssetPipeline/Cookers/TextureCooker.h"
#include "Geometry/MeshValidation.h"
#include "Geometry/MeshLod.h"

namespace Catalpa::Tests {
    namespace {
        ImageAsset Image(uint32_t width, uint32_t height, ImageSemantic semantic, ImageColorSpace space) {
            ImageAsset value;
            value.sourceUri = "generated-test-fixture";
            auto& image = value.image;
            image.width = width;
            image.height = height;
            image.semantic = semantic;
            image.colorSpace = space;
            image.format =
                space == ImageColorSpace::SRGB ? PixelFormat::RGBA8_SRGB : PixelFormat::RGBA8_UNorm;
            image.alphaMode = ImageAlphaMode::Straight;
            image.pixels.resize(size_t(width) * height * 4, std::byte{255});
            image.subresources.push_back({0, 0, 0, width, height, 1, 0, uint64_t(width) * 4,
                                          uint64_t(width) * height * 4, uint64_t(width) * height * 4});
            return value;
        }
    } // namespace
    void RunOptimizationContractsTests(TestContext& test) {
        std::string error;
        auto source = Image(4, 4, ImageSemantic::Color, ImageColorSpace::SRGB);
        const auto original = source.image.pixels;
        ImageAsset cooked;
        Tools::TextureCookReport report;
        test.Check(Tools::TextureCooker::Prepare(source, cooked, report, error) && report.compressed &&
                       cooked.image.format == PixelFormat::BC1_SRGB && cooked.image.mipLevels == 3,
                   "Opaque color cooks to real BC1 blocks and a complete mip chain");
        test.Check(cooked.image.pixels.size() == 24 && cooked.image.subresources[2].rowPitch == 8,
                   "BC1 subresources use block rows even below four pixels");
        test.Check(source.image.pixels == original && source.image.mipLevels == 1,
                   "Cook transformations do not modify decoded source assets");
        test.Check(report.linearRmse <= .08 && std::isfinite(report.maximumLinearError),
                   "Compression quality reports the measured linear RGB error");
        auto srgb = Image(2, 1, ImageSemantic::Color, ImageColorSpace::SRGB);
        for (size_t c = 0; c < 3; ++c)
            srgb.image.pixels[c] = std::byte{0};
        srgb.image.pixels[3] = srgb.image.pixels[7] = std::byte{128};
        test.Check(Tools::TextureCooker::Prepare(srgb, cooked, report, error) && !report.compressed &&
                       cooked.image.mipLevels == 2,
                   "Transparent images retain RGBA8 storage");
        const auto average =
            std::to_integer<unsigned>(cooked.image.pixels[cooked.image.subresources[1].offset]);
        test.Check(average >= 187 && average <= 189, "sRGB mip RGB averages in linear light before encoding");
        auto odd = Image(3, 1, ImageSemantic::Data, ImageColorSpace::Linear);
        for (size_t c = 0; c < 3; ++c) {
            odd.image.pixels[c] = std::byte{0};
            odd.image.pixels[4 + c] = std::byte{0};
        }
        test.Check(Tools::TextureCooker::Prepare(odd, cooked, report, error) &&
                       std::to_integer<unsigned>(cooked.image.pixels[cooked.image.subresources[1].offset]) ==
                           85,
                   "Odd-width area filtering includes the entire source footprint");
        auto normal = Image(2, 1, ImageSemantic::Normal, ImageColorSpace::Linear);
        normal.image.pixels = {std::byte{255}, std::byte{128}, std::byte{128}, std::byte{255},
                               std::byte{128}, std::byte{255}, std::byte{128}, std::byte{255}};
        test.Check(Tools::TextureCooker::Prepare(normal, cooked, report, error) && !report.compressed,
                   "Normal maps retain reconstructable RGB rather than unsupported BC5 semantics");
        const auto normalOffset = cooked.image.subresources[1].offset;
        test.Check(std::to_integer<unsigned>(cooked.image.pixels[normalOffset]) >= 214 &&
                       std::to_integer<unsigned>(cooked.image.pixels[normalOffset + 1]) >= 214,
                   "Derived normal mip vectors are normalized after vector averaging");

        MeshData grid;
        constexpr uint32_t side = 17;
        grid.vertexCount = side * side;
        grid.vertexStride = 12;
        grid.attributes = {{0, VertexFormat::Float3, 0, VertexSemantic::Position}};
        grid.vertexData.resize(size_t(grid.vertexCount) * 12);
        std::vector<Math::Vec3> points;
        for (uint32_t y = 0; y < side; ++y)
            for (uint32_t x = 0; x < side; ++x)
                points.emplace_back(float(x), 0, float(y));
        std::memcpy(grid.vertexData.data(), points.data(), grid.vertexData.size());
        std::vector<uint32_t> indices;
        for (uint32_t y = 0; y + 1 < side; ++y)
            for (uint32_t x = 0; x + 1 < side; ++x) {
                const auto a = y * side + x, b = a + 1, c = a + side, d = c + 1;
                indices.insert(indices.end(), {a, c, b, b, c, d});
            }
        grid.indexData.resize(indices.size() * 4);
        std::memcpy(grid.indexData.data(), indices.data(), grid.indexData.size());
        grid.submeshes = {
            {0, uint32_t(indices.size()), 0, 0, grid.vertexCount, 0, MeshTopology::TriangleList}};
        (void)ComputeBounds(points, grid.localBounds);
        MeshAsset base;
        base.materialSlots = {"ground"};
        test.Check(CreateMeshResource(grid, base.mesh).IsSuccess(),
                   "Construct validated source topology for LOD tests");
        MeshAsset lod;
        float measured = 0;
        test.Check(Tools::SimplifyMeshLod(base, .5f, .01f, lod, measured, error) &&
                       lod.mesh->GetData().GetIndexCount() < base.mesh->GetData().GetIndexCount(),
                   "Meshoptimizer produces actual reduced triangle geometry");
        test.Check(lod.materialSlots == base.materialSlots && std::isfinite(measured) && measured >= 0,
                   "LOD retains material slots and reports geometric error in source units");
        test.Check(base.mesh->GetData().indexData == grid.indexData,
                   "LOD building preserves the original mesh and indices");
        test.Check(!Tools::SimplifyMeshLod(base, 1.2f, .01f, lod, measured, error),
                   "Invalid target ratios cannot overwrite an existing LOD result");
        AssetVersionRef<MeshAsset> malformedBase, validLevel;
        const auto assetId = [](uint64_t value) {
            return AssetId{StableId::FromWords(0x4c4f44434f4e5452ULL, value)};
        };
        const bool versionsReady =
            AssetVersionRef<MeshAsset>::TryCreate(assetId(1), 1, std::make_shared<const MeshAsset>(),
                                                  malformedBase, error) &&
            AssetVersionRef<MeshAsset>::TryCreate(assetId(2), 1, std::make_shared<const MeshAsset>(base),
                                                  validLevel, error);
        const std::array levels{validLevel};
        const std::array errors{.1f};
        test.Check(versionsReady && !ValidateMeshLodChain(malformedBase, levels, errors, error),
                   "An invalid fixed base mesh is rejected before a nonempty LOD chain dereferences it");
        test.Check(ValidateMeshLodChain(malformedBase, {}, {}, error),
                   "An empty LOD chain retains the legacy CPU geometry entry contract");
    }
} // namespace Catalpa::Tests
