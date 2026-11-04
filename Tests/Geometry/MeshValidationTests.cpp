#include "TestRunner.h"

#include "Geometry/PrimitiveMesh.h"
#include "Geometry/MeshValidation.h"

namespace Catalpa::Tests {
    namespace {
        // 只在确认流长度的有效样例上解码；不依赖 vector<byte> 的浮点对齐或 strict aliasing。
        float ReadFloat(const MeshData& mesh, size_t offset)
        {
            float value;
            std::memcpy(&value, mesh.vertexData.data() + offset, sizeof(value));
            return value;
        }
        uint32_t ReadIndex(const MeshData& mesh, size_t element)
        {
            uint32_t value;
            std::memcpy(&value, mesh.indexData.data() + element * sizeof(value), sizeof(value));
            return value;
        }
        void WriteIndex(MeshData& mesh, size_t element, uint32_t value)
        {
            std::memcpy(mesh.indexData.data() + element * sizeof(value), &value, sizeof(value));
        }
        Math::Vec3 Position(const MeshData& mesh, uint32_t vertex)
        {
            const size_t start = static_cast<size_t>(vertex) * mesh.vertexStride;
            return { ReadFloat(mesh, start), ReadFloat(mesh, start + 4), ReadFloat(mesh, start + 8) };
        }
        bool Near(float a, float b) { return std::abs(a - b) <= 2.0e-5f * (1.0f + std::abs(b)); }
        bool SameBounds(const Aabb& a, const Aabb& b) { return a.empty == b.empty && a.minimum == b.minimum && a.maximum == b.maximum; }

        // 用每个三角形的几何积核对外向绕序，而非只检查索引数量。
        void CheckWinding(TestContext& test, const MeshData& mesh)
        {
            for (size_t index = 0; index < mesh.GetIndexCount(); index += 3) {
                const uint32_t first = ReadIndex(mesh, index);
                const auto a = Position(mesh, first), b = Position(mesh, ReadIndex(mesh, index + 1)), c = Position(mesh, ReadIndex(mesh, index + 2));
                const size_t start = static_cast<size_t>(first) * mesh.vertexStride + 12;
                const Math::Vec3 normal{ ReadFloat(mesh, start), ReadFloat(mesh, start + 4), ReadFloat(mesh, start + 8) };
                test.Check(glm::dot(glm::cross(b - a, c - a), normal) > 0.0f, "Primitive triangles wind outward consistently with normals");
            }
        }
    }

    // CPU 几何/资源冻结/包围体契约；不创建 GPU、窗口或另一个测试入口。
    void RunMeshValidationTests(TestContext& test)
    {
        MeshData empty;
        test.Check(ValidateMesh(empty).IsSuccess(), "Canonical empty CPU mesh is valid");
        MeshData cube, plane;
        test.Check(CreateCubeMesh(2.0f, cube).IsSuccess(), "Cube generator creates validated geometry");
        test.Check(CreatePlaneMesh(4.0f, 6.0f, plane).IsSuccess(), "Plane generator creates validated geometry");
        if (!ValidateMesh(cube).IsSuccess() || !ValidateMesh(plane).IsSuccess()) return;
        test.Check(cube.vertexCount == 24 && cube.GetIndexCount() == 36 && cube.submeshes.size() == 1, "Cube has per-face vertices and twelve indexed triangles");
        test.Check(plane.vertexCount == 4 && plane.GetIndexCount() == 6, "Plane has two indexed triangles");
        test.Check(cube.vertexStride == 32 && cube.GetVertexLayout().attributes.size() == 3, "Owned mesh exposes a short-lived layout borrow");
        test.Check(cube.localBounds.minimum == Math::Vec3(-1) && cube.localBounds.maximum == Math::Vec3(1), "Cube local bounds match explicit size");
        test.Check(plane.localBounds.minimum == Math::Vec3(-2,0,-3) && plane.localBounds.maximum == Math::Vec3(2,0,3), "Plane dimensions and XZ convention are explicit");
        CheckWinding(test, cube);
        CheckWinding(test, plane);
        for (const auto& mesh : { &cube, &plane }) {
            for (uint32_t vertex = 0; vertex < mesh->vertexCount; ++vertex) {
                const size_t offset = static_cast<size_t>(vertex) * mesh->vertexStride + 24;
                const float u = ReadFloat(*mesh, offset), v = ReadFloat(*mesh, offset + 4);
                test.Check(u >= 0 && u <= 1 && v >= 0 && v <= 1, "Primitive UVs stay inside declared unit square");
            }
        }
        const auto originalBytes = cube.vertexData;
        test.Check(!CreateCubeMesh(0, cube).IsSuccess() && cube.vertexData == originalBytes, "Invalid primitive dimensions preserve output");
        test.Check(!CreatePlaneMesh(std::numeric_limits<float>::infinity(), 1, cube).IsSuccess() && cube.vertexData == originalBytes, "Nonfinite primitive dimensions do not overwrite output");
        test.Check(!CreateCubeMesh(std::numeric_limits<float>::denorm_min(), cube).IsSuccess(), "Unrepresentable half extent is rejected");

        MeshResourceRef resource;
        test.Check(CreateMeshResource(cube, resource).IsSuccess() && ValidateMeshResource(resource).IsSuccess(), "Validated CPU resource is published");
        if (!resource) return;
        const auto resourceBytes = resource->GetData().vertexData;
        cube.vertexData[0] ^= std::byte{ 0xff };
        cube.attributes.clear();
        test.Check(resource->GetData().vertexData == resourceBytes && resource->GetData().attributes.size() == 3, "Resource owns deep copies unaffected by author data mutation");
        const auto retained = resource;
        test.Check(!CreateMeshResource(cube, resource).IsSuccess() && resource == retained, "Invalid resource publication preserves existing reference");
        test.Check(!ValidateMeshResource({}).IsSuccess(), "Null CPU geometry references are rejected");

        MeshData bad = plane;
        bad.vertexData.pop_back();
        test.Check(!ValidateMesh(bad).IsSuccess(), "Truncated vertex stream is rejected before decoding");
        bad = plane; bad.vertexStride = std::numeric_limits<uint32_t>::max();
        test.Check(!ValidateMesh(bad).IsSuccess(), "Count times stride cannot wrap into an accepted stream");
        bad = plane; bad.attributes[1].offset = 4;
        test.Check(!ValidateMesh(bad).IsSuccess(), "Overlapping attributes are rejected");
        bad = plane; bad.attributes[1].location = 0;
        test.Check(!ValidateMesh(bad).IsSuccess(), "Duplicate shader locations are rejected");
        bad = plane; bad.attributes[0].semantic = VertexSemantic::Unknown;
        test.Check(!ValidateMesh(bad).IsSuccess(), "Mesh position semantic must be explicit");
        bad = plane; bad.attributes[0].format = VertexFormat::Float2;
        test.Check(!ValidateMesh(bad).IsSuccess(), "Position semantic requires Float3");
        bad = plane; bad.indexData.pop_back();
        test.Check(!ValidateMesh(bad).IsSuccess(), "Incomplete index element bytes are rejected");
        bad = plane; bad.indexType = static_cast<MeshIndexType>(100);
        test.Check(!ValidateMesh(bad).IsSuccess(), "Unknown index type is rejected");
        bad = plane; WriteIndex(bad, 0, 4);
        test.Check(!ValidateMesh(bad).IsSuccess(), "Index outside vertex range is rejected");
        bad = plane; bad.submeshes[0].baseVertex = -1;
        test.Check(!ValidateMesh(bad).IsSuccess(), "Negative adjusted vertex cannot underflow into valid unsigned range");
        bad = plane; bad.submeshes[0].firstIndex = std::numeric_limits<uint32_t>::max();
        test.Check(!ValidateMesh(bad).IsSuccess(), "Submesh firstIndex plus count uses wide range arithmetic");
        bad = plane; bad.submeshes[0].indexCount = 5;
        test.Check(!ValidateMesh(bad).IsSuccess(), "Triangle list submesh count must be divisible by three");
        bad = plane; bad.submeshes[0].vertexCount = 3;
        test.Check(!ValidateMesh(bad).IsSuccess(), "Submesh vertex window constrains adjusted indices");
        bad = plane; bad.submeshes[0].firstVertex = std::numeric_limits<uint32_t>::max();
        test.Check(!ValidateMesh(bad).IsSuccess(), "Submesh vertex range overflow is rejected");
        bad = plane; bad.submeshes[0].topology = static_cast<MeshTopology>(100);
        test.Check(!ValidateMesh(bad).IsSuccess(), "Unsupported topology is rejected explicitly");
        bad = plane; bad.localBounds.minimum.x = 0;
        test.Check(!ValidateMesh(bad).IsSuccess(), "Local bounds must contain every declared position");
        bad = plane; bad.localBounds.empty = true;
        test.Check(!ValidateMesh(bad).IsSuccess(), "Nonempty mesh cannot omit bounds");
        bad = plane;
        const float zero = 0;
        for (uint32_t component = 0; component < 3; ++component) std::memcpy(bad.vertexData.data() + 12 + 4 * component, &zero, sizeof(zero));
        test.Check(!ValidateMesh(bad).IsSuccess(), "Zero normals are rejected without silently normalizing data");

        bad = plane;
        for (size_t element = 0; element < bad.GetIndexCount(); ++element) WriteIndex(bad, element, ReadIndex(bad, element) + 1);
        bad.submeshes[0].baseVertex = -1;
        test.Check(ValidateMesh(bad).IsSuccess(), "Raw index may exceed global count when signed baseVertex maps it into a valid window");
        bad = plane; bad.submeshes[0].indexCount = 3; bad.submeshes[0].firstVertex = 1; bad.submeshes[0].vertexCount = 3; bad.submeshes[0].baseVertex = 1;
        test.Check(ValidateMesh(bad).IsSuccess(), "Positive baseVertex and nonzero firstVertex use element ranges");
        bad = plane; bad.indexType = MeshIndexType::UInt16;
        const std::array<uint16_t, 6> shortIndices{ 0,1,2,2,3,0 };
        bad.indexData.resize(sizeof(shortIndices));
        std::memcpy(bad.indexData.data(), shortIndices.data(), sizeof(shortIndices));
        test.Check(ValidateMesh(bad).IsSuccess() && bad.GetIndexCount() == 6, "UInt16 streams follow the same indexed geometry contract");

        bad = resource->GetData();
        const float nan = std::numeric_limits<float>::quiet_NaN();
        for (size_t byte = 0; byte < bad.vertexData.size(); byte += sizeof(float)) std::memcpy(bad.vertexData.data() + byte, &nan, sizeof(nan));
        const auto rawBadBytes = bad.vertexData;
        MeshValidationReport report;
        test.Check(!ValidateMesh(bad, &report).IsSuccess() && report.issues.size() == 64 && report.truncated, "Diagnostic cap never converts additional errors into success");
        test.Check(bad.vertexData == rawBadBytes, "Validation diagnostics preserve source bytes");
        test.Check(ValidateMesh(plane, &report).IsSuccess() && report.issues.empty() && !report.truncated, "Validation report resets between calls");

        Aabb bounds;
        const std::array<Math::Vec3, 3> points{{ {-1,-2,-3},{2,3,4},{0,0,0} }};
        test.Check(ComputeBounds(points, bounds).IsSuccess() && bounds.minimum == points[0] && bounds.maximum == points[1], "Bounds are computed from all finite source positions");
        const Aabb saved = bounds;
        const std::array<Math::Vec3, 1> invalidPoints{{ {nan,0,0} }};
        test.Check(!ComputeBounds(invalidPoints, bounds).IsSuccess() && SameBounds(bounds, saved), "Invalid position preserves existing bounds");
        test.Check(MergeBounds({}, saved, bounds).IsSuccess() && SameBounds(bounds, saved), "Empty bounds are merge identity");
        test.Check(ComputeBounds({}, bounds).IsSuccess() && bounds.empty, "Empty point span has explicit empty bounds");
        Aabb unit{ {-1,-1,-1}, {1,1,1}, false };
        Math::Mat4 world{1}; world[0][0] = -2; world[1][0] = 1; world[1][1] = 3; world[2][2] = 0.5f; world[3] = {10,-2,5,1};
        test.Check(TransformBounds(unit, world, bounds).IsSuccess() && Near(bounds.minimum.x,7) && Near(bounds.maximum.x,13)
            && Near(bounds.minimum.y,-5) && Near(bounds.maximum.y,1) && Near(bounds.minimum.z,4.5f) && Near(bounds.maximum.z,5.5f), "Bounds conservatively handle reflection, nonuniform scale and shear");
        for (unsigned corner = 0; corner < 8; ++corner) {
            const Math::Vec4 point{ (corner & 1) ? 1.0f : -1.0f,(corner & 2) ? 1.0f : -1.0f,(corner & 4) ? 1.0f : -1.0f,1 };
            const auto transformed = world * point;
            test.Check(transformed.x >= bounds.minimum.x && transformed.x <= bounds.maximum.x && transformed.y >= bounds.minimum.y
                && transformed.y <= bounds.maximum.y && transformed.z >= bounds.minimum.z && transformed.z <= bounds.maximum.z, "Transformed bounds contain every transformed source corner");
        }
        Aabb previous = bounds;
        world[0][3] = 0.1f;
        test.Check(!TransformBounds(unit, world, bounds).IsSuccess() && SameBounds(bounds, previous), "Projective bounds transform is rejected without output overwrite");
        world = Math::Mat4(1); world[0][0] = std::numeric_limits<float>::max();
        Aabb huge{ {-2,-1,-1}, {2,1,1}, false };
        test.Check(!TransformBounds(huge, world, bounds).IsSuccess() && SameBounds(bounds, previous), "Bounds float overflow is reported without clamping");
        world = Math::Mat4(1); world[0][0] = 0;
        test.Check(TransformBounds(unit, world, bounds).IsSuccess(), "Bounds support a collapsed affine axis without requiring an inverse");
        BoundingSphere sphere;
        test.Check(ComputeBoundingSphere(unit, sphere).IsSuccess() && !sphere.empty && static_cast<double>(sphere.radius) >= std::sqrt(3.0), "Sphere conservatively contains all AABB corners");
        test.Check(ComputeBoundingSphere({}, sphere).IsSuccess() && sphere.empty, "Empty bounds produce empty sphere");
        Aabb point{ {1,2,3}, {1,2,3}, false };
        test.Check(ComputeBoundingSphere(point, sphere).IsSuccess() && sphere.radius == 0.0f, "A point has an exact zero-radius nonempty sphere");
        const float limit = std::numeric_limits<float>::max();
        Aabb extreme{ {limit,0,0}, {limit,0,0}, false };
        test.Check(TransformBounds(extreme, Math::Mat4(1), bounds).IsSuccess() && SameBounds(bounds, extreme), "Exact finite boundary survives identity transform without artificial overflow");
    }
}
