#include "catpch.h"
#include "AssetPipeline/Importers/ImportGeometry.h"
#include "Geometry/MeshValidation.h"
#include "Core/Math/CoordinateConvention.h"
#include <mikktspace.h>

namespace Catalpa::Tools {
    namespace {
        struct TangentData {
            std::vector<ImportVertex>* vertices;
            size_t repairedCorners = 0;
            bool nonFiniteOutput = false;
        };

        int Faces(const SMikkTSpaceContext* c) {
            return int(static_cast<TangentData*>(c->m_pUserData)->vertices->size() / 3);
        }

        int Count(const SMikkTSpaceContext*, int) {
            return 3;
        }

        const ImportVertex& Vertex(const SMikkTSpaceContext* c, int f, int v) {
            return (*static_cast<TangentData*>(c->m_pUserData)->vertices)[size_t(f) * 3 + size_t(v)];
        }

        void Position(const SMikkTSpaceContext* c, float out[], int f, int v) {
            const auto& p = Vertex(c, f, v).position;
            std::copy(p.begin(), p.end(), out);
        }

        // MikkTSpace 要求单位法线。只对回调输出归一化，原顶点保留导入值。
        void Normal(const SMikkTSpaceContext* c, float out[], int f, int v) {
            const auto& p = Vertex(c, f, v).normal;
            const double length = std::sqrt(double(p[0]) * p[0] + double(p[1]) * p[1] + double(p[2]) * p[2]);
            for (size_t i = 0; i < 3; ++i)
                out[i] = float(p[i] / length);
        }

        void UV(const SMikkTSpaceContext* c, float out[], int f, int v) {
            const auto& p = Vertex(c, f, v).uv;
            std::copy(p.begin(), p.end(), out);
        }

        // UV 退化时一个角可能没有有效的 dP/du。只修正生成值，源切线走独立的严格校验。
        void Tangent(const SMikkTSpaceContext* c, const float p[], const float b[], float, float,
                     tbool orientationPreserving, int f, int v) {
            auto& data = *static_cast<TangentData*>(c->m_pUserData);
            auto& vertex = (*data.vertices)[size_t(f) * 3 + size_t(v)];
            const float sign = orientationPreserving ? 1.0f : -1.0f;
            for (size_t k = 0; k < 3; ++k) {
                if (!std::isfinite(p[k]) || !std::isfinite(b[k])) {
                    data.nonFiniteOutput = true;
                    return;
                }
            }

            const glm::dvec3 n =
                glm::normalize(glm::dvec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]));
            const glm::dvec3 t(p[0], p[1], p[2]);
            const double lengthSquared = glm::dot(t, t);
            const double parallel = glm::dot(n, t);
            constexpr double tolerance = 16.0 * std::numeric_limits<float>::epsilon();
            if (lengthSquared > 0 && parallel * parallel <= tolerance * tolerance * lengthSquared) {
                vertex.tangent = {p[0], p[1], p[2], sign};
                return;
            }

            glm::dvec3 projected = t - n * parallel;
            if (glm::dot(projected, projected) <= tolerance * tolerance * lengthSquared) {
                // B = sign * cross(N, T)。优先使用仍有效的副切线，保留 UV 的方向信息。
                const glm::dvec3 bitangent(b[0], b[1], b[2]);
                projected = double(sign) * glm::cross(bitangent, n);
                if (glm::dot(projected, projected) <=
                    tolerance * tolerance * glm::dot(bitangent, bitangent)) {
                    // 两个 UV 导数都没有方向时，选与 N 最不平行的坐标轴来构造局部正交基。
                    const auto axisIndex = std::abs(n.x) <= std::abs(n.y) && std::abs(n.x) <= std::abs(n.z)
                                               ? 0
                                               : (std::abs(n.y) <= std::abs(n.z) ? 1 : 2);
                    glm::dvec3 axis(0.0);
                    axis[axisIndex] = 1.0;
                    projected = glm::cross(n, axis);
                }
            }
            const glm::dvec3 direction = glm::normalize(projected);
            vertex.tangent = {float(direction.x), float(direction.y), float(direction.z), sign};
            ++data.repairedCorners;
        }
    }

    bool BuildImportedMesh(AssetBuildContext& ctx, std::vector<ImportVertex> vertices,
                           std::vector<uint32_t> indices, bool normals, bool uv, bool tangents,
                           MeshAsset& out, std::string& e) {
        if (!ctx.CheckCancelled(e))
            return false;
        const auto& limits = ctx.request.settings;
        if (vertices.empty() || vertices.size() > limits.maxVertices || indices.empty() ||
            indices.size() > limits.maxIndices || indices.size() % 3) {
            e = "Geometry exceeds triangle/vertex/index bounds";
            return false;
        }
        for (const auto& v : vertices) {
            for (const auto& field : {std::span<const float>(v.position), std::span<const float>(v.normal),
                                      std::span<const float>(v.uv), std::span<const float>(v.tangent),
                                      std::span<const float>(v.color)})
                for (float f : field)
                    if (!std::isfinite(f)) {
                        e = "Non-finite source vertex";
                        return false;
                    }
            if (normals && v.normal[0] == 0.0f && v.normal[1] == 0.0f && v.normal[2] == 0.0f) {
                e = "Zero source normal";
                return false;
            }
        }
        for (auto index : indices)
            if (index >= vertices.size()) {
                e = "Source index outside vertex stream";
                return false;
            }
        if (!normals && !limits.generateNormals) {
            e = "Missing normals and generation is disabled";
            return false;
        }
        if (tangents)
            for (const auto& v : vertices) {
                const auto& t = v.tangent;
                if ((t[0] == 0.0f && t[1] == 0.0f && t[2] == 0.0f) ||
                    std::abs(std::abs(t[3]) - 1.0f) > 1e-5f) {
                    e = "Source tangent must have a nonzero direction and handedness +/-1";
                    return false;
                }
            }
        const bool makeTangents = uv && !tangents && limits.generateTangents;
        const bool expandCorners = !normals || makeTangents;
        const size_t outputVertices = expandCorners ? indices.size() : vertices.size();
        if (outputVertices > limits.maxVertices) {
            e = "Corner expansion exceeds the vertex limit";
            return false;
        }
        // 最终 MeshResource 的流跨图元保留；中间顶点、MeshData 和包围盒临时数据仅本次占用。
        const size_t streamBytes = outputVertices * sizeof(ImportVertex) + indices.size() * sizeof(uint32_t);
        const size_t meshWorkspace = 2 * streamBytes + outputVertices * sizeof(Math::Vec3);
        const size_t expansionWorkspace = vertices.size() * sizeof(ImportVertex) + streamBytes;
        if (!ctx.ReserveWorking(streamBytes, e) ||
            !ctx.WorkspaceFits(std::max(meshWorkspace, expansionWorkspace), e))
            return false;

        if (expandCorners) {
            std::vector<ImportVertex> corners;
            corners.reserve(indices.size());
            for (auto index : indices)
                corners.push_back(vertices[index]);
            if (!normals) {
                for (size_t i = 0; i < corners.size(); i += 3) {
                    // 先升到双精度再做差和叉积，微小三角形也有可用的面法线。
                    glm::dvec3 p[3];
                    for (size_t k = 0; k < 3; ++k)
                        p[k] = {corners[i + k].position[0], corners[i + k].position[1],
                                corners[i + k].position[2]};
                    auto n = glm::cross(p[1] - p[0], p[2] - p[0]);
                    const auto length = std::hypot(n.x, n.y, n.z);
                    if (!std::isfinite(length) || length == 0.0) {
                        e = "Cannot generate a normal for a degenerate triangle";
                        return false;
                    }
                    n /= length;
                    for (size_t k = 0; k < 3; ++k)
                        corners[i + k].normal = {float(n.x), float(n.y), float(n.z)};
                }
                ctx.Diagnose(AssetDiagnosticLevel::Warning, "geometry",
                             "Missing normals generated per face; no smoothing of source normals");
            }
            if (makeTangents) {
                SMikkTSpaceInterface api{};
                api.m_getNumFaces = Faces;
                api.m_getNumVerticesOfFace = Count;
                api.m_getPosition = Position;
                api.m_getNormal = Normal;
                api.m_getTexCoord = UV;
                api.m_setTSpace = Tangent;
                TangentData data{&corners};
                SMikkTSpaceContext context{&api, &data};
                if (!genTangSpaceDefault(&context)) {
                    e = "MikkTSpace tangent generation failed";
                    return false;
                }
                if (data.nonFiniteOutput) {
                    e = "MikkTSpace produced a non-finite tangent frame";
                    return false;
                }
                if (data.repairedCorners > 0)
                    ctx.Diagnose(AssetDiagnosticLevel::Warning, "geometry",
                                 "Generated tangent used an orthogonal fallback at " +
                                     std::to_string(data.repairedCorners) + " triangle corners");
                tangents = true;
                ctx.Diagnose(AssetDiagnosticLevel::Info, "geometry",
                             "MikkTSpace per-corner tangents; derived vertices retain tangent seams");
            }
            vertices.swap(corners);
            for (size_t i = 0; i < indices.size(); ++i)
                indices[i] = uint32_t(i);
        }
        static_assert(sizeof(ImportVertex) == 64);
        MeshData data;
        data.vertexCount = uint32_t(vertices.size());
        data.vertexStride = sizeof(ImportVertex);
        data.indexType = MeshIndexType::UInt32;
        data.attributes = {
            {0, VertexFormat::Float3, uint32_t(offsetof(ImportVertex, position)), VertexSemantic::Position},
            {1, VertexFormat::Float3, uint32_t(offsetof(ImportVertex, normal)), VertexSemantic::Normal},
            {4, VertexFormat::Float4, uint32_t(offsetof(ImportVertex, color)), VertexSemantic::Color0}};
        if (uv)
            data.attributes.push_back(
                {2, VertexFormat::Float2, uint32_t(offsetof(ImportVertex, uv)), VertexSemantic::TexCoord0});
        if (tangents)
            data.attributes.push_back({3, VertexFormat::Float4, uint32_t(offsetof(ImportVertex, tangent)),
                                       VertexSemantic::Tangent});
        data.vertexData.resize(vertices.size() * sizeof(ImportVertex));
        std::memcpy(data.vertexData.data(), vertices.data(), data.vertexData.size());
        data.indexData.resize(indices.size() * sizeof(uint32_t));
        std::memcpy(data.indexData.data(), indices.data(), data.indexData.size());
        std::vector<Math::Vec3> points;
        points.reserve(vertices.size());
        for (const auto& v : vertices)
            points.emplace_back(v.position[0], v.position[1], v.position[2]);
        if (const auto status = ComputeBounds(points, data.localBounds); !status.IsSuccess()) {
            e = status.message;
            return false;
        }
        data.submeshes.push_back(
            {0, uint32_t(indices.size()), 0, 0, uint32_t(vertices.size()), 0, MeshTopology::TriangleList});
        MeshAsset candidate;
        candidate.materialSlots = {"surface"};
        const auto status = CreateMeshResource(data, candidate.mesh);
        if (!status.IsSuccess()) {
            e = status.message;
            return false;
        }
        if (!ctx.CheckCancelled(e))
            return false;
        out = std::move(candidate);
        return true;
    }

    bool SetImportRoot(AssetBuildContext& ctx, ModelAsset& model, const Math::Mat4& source, std::string& e) {
        if (model.nodes.size() >= ctx.request.settings.maxNodes) {
            e = "Model node limit reached";
            return false;
        }
        Math::Mat4 extra{1};
        const float scale = float(ctx.request.settings.unitScale);
        extra[0][0] = extra[1][1] = extra[2][2] = scale;
        if (ctx.request.settings.sourceUp == "z") {
            const Math::Mat4 rotate =
                glm::mat4_cast(Math::Quat{0.7071067811865475f, -0.7071067811865475f, 0, 0});
            extra = extra * rotate;
        }
        Math::TransformTRS trs;
        const auto status = Math::DecomposeTRS(extra * source, trs);
        if (!status.IsSuccess()) {
            e = status.message;
            return false;
        }
        const auto index = int32_t(model.nodes.size());
        for (auto& node : model.nodes)
            if (node.parent == -1)
                node.parent = index;
        model.nodes.push_back(
            {ctx.Identity("conversion-root").value, "Import conversion (metres / Y-up)", -1, trs});
        return ValidateModelAsset(model, e);
    }
}
