#include "catpch.h"
#include "Navigation/NavigationWorld.h"
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourAlloc.h>
#include <DetourStatus.h>
#include <bit>

namespace Catalpa
{
    namespace
    {
        static_assert(sizeof(dtPolyRef) == 4 && DT_NAVMESH_VERSION == 7 &&
                      std::endian::native == std::endian::little);
        size_t Aligned(size_t n)
        {
            return (n + 3) & ~size_t(3);
        }
        bool Finite(const float* a)
        {
            return std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]);
        }
        bool Bounded(const float* a, float limit)
        {
            return Finite(a) && std::max({std::abs(a[0]), std::abs(a[1]), std::abs(a[2])}) <= limit;
        }
        template <class T> T Read(const std::vector<std::byte>& bytes, size_t offset)
        {
            T out;
            std::memcpy(&out, bytes.data() + offset, sizeof out);
            return out;
        }
    } // namespace
    bool ValidateNavMeshAsset(const NavMeshAsset& asset, std::string& error)
    {
        if (!ValidateNavigationSettings(asset.settings, error))
            return false;
        const auto& bytes = asset.tile;
        if (bytes.size() < sizeof(dtMeshHeader) || bytes.size() > 8 * 1024 * 1024) {
            error = "Navigation tile size is outside bounds";
            return false;
        }
        const auto h = Read<dtMeshHeader>(bytes, 0);
        if (h.magic != DT_NAVMESH_MAGIC || h.version != DT_NAVMESH_VERSION || h.x || h.y || h.layer ||
            h.polyCount <= 0 || h.polyCount > 32768 || h.vertCount < 3 || h.vertCount > 65535 ||
            h.maxLinkCount < 1 || h.maxLinkCount > h.polyCount * 8 || h.detailMeshCount != h.polyCount ||
            h.detailVertCount < 0 || h.detailVertCount > 1000000 || h.detailTriCount < 0 ||
            h.detailTriCount > 1000000 || h.bvNodeCount < 0 || h.bvNodeCount > h.polyCount * 2 ||
            h.offMeshConCount || h.offMeshBase != h.polyCount || !Bounded(h.bmin, 1e6f) ||
            !Bounded(h.bmax, 1e6f) || h.bmin[0] > h.bmax[0] || h.bmin[1] > h.bmax[1] ||
            h.bmin[2] > h.bmax[2] || !std::isfinite(h.bvQuantFactor) || h.bvQuantFactor <= 0 ||
            !std::isfinite(h.walkableHeight) || h.walkableHeight <= 0 || !std::isfinite(h.walkableRadius) ||
            h.walkableRadius < 0 || !std::isfinite(h.walkableClimb) || h.walkableClimb < 0) {
            error = "Navigation tile header/ABI/counts rejected";
            return false;
        }
        const auto close = [](float a, float b) {
            return std::abs(double(a) - double(b)) <=
                   1e-5 * std::max({1.0, std::abs(double(a)), std::abs(double(b))});
        };
        if (!close(h.walkableHeight, asset.settings.agentHeight) ||
            !close(h.walkableRadius, asset.settings.agentRadius) ||
            !close(h.walkableClimb, asset.settings.maxClimb) ||
            !close(h.bvQuantFactor, 1.0f / asset.settings.cellSize)) {
            error = "Navigation tile profile differs from its declared build settings";
            return false;
        }
        size_t offset = Aligned(sizeof h);
        const size_t vertices = offset;
        offset += Aligned(size_t(h.vertCount) * 3 * sizeof(float));
        const size_t polys = offset;
        offset += Aligned(size_t(h.polyCount) * sizeof(dtPoly));
        offset += Aligned(size_t(h.maxLinkCount) * sizeof(dtLink));
        const size_t detail = offset;
        offset += Aligned(size_t(h.detailMeshCount) * sizeof(dtPolyDetail));
        const size_t detailVertices = offset;
        offset += Aligned(size_t(h.detailVertCount) * 3 * sizeof(float));
        const size_t detailTriangles = offset;
        offset += Aligned(size_t(h.detailTriCount) * 4);
        const size_t bvTree = offset;
        offset += Aligned(size_t(h.bvNodeCount) * sizeof(dtBVNode));
        if (offset != bytes.size()) {
            error = "Navigation tile section ranges do not match payload";
            return false;
        }
        for (int i = 0; i < h.vertCount + h.detailVertCount; ++i) {
            const auto base =
                i < h.vertCount ? vertices + size_t(i) * 12 : detailVertices + size_t(i - h.vertCount) * 12;
            for (size_t k = 0; k < 3; ++k)
                if (const auto value = Read<float>(bytes, base + k * 4);
                    !std::isfinite(value) || std::abs(value) > 1e6f) {
                    error = "Navigation vertex is nonfinite or outside the bounded metre world";
                    return false;
                }
        }
        for (int i = 0; i < h.polyCount; ++i) {
            const auto p = Read<dtPoly>(bytes, polys + size_t(i) * sizeof(dtPoly));
            if (p.vertCount < 3 || p.vertCount > DT_VERTS_PER_POLYGON || p.getType() != DT_POLYTYPE_GROUND) {
                error = "Invalid navigation polygon";
                return false;
            }
            for (int j = 0; j < p.vertCount; ++j)
                if (p.verts[j] >= h.vertCount ||
                    (p.neis[j] && (p.neis[j] & DT_EXT_LINK || p.neis[j] > h.polyCount))) {
                    error = "Navigation polygon vertex or adjacency is invalid";
                    return false;
                }
            const auto d = Read<dtPolyDetail>(bytes, detail + size_t(i) * sizeof(dtPolyDetail));
            if (size_t(d.vertBase) + d.vertCount > size_t(h.detailVertCount) ||
                size_t(d.triBase) + d.triCount > size_t(h.detailTriCount)) {
                error = "Navigation detail range is invalid";
                return false;
            }
            for (unsigned j = 0; j < d.triCount; ++j)
                for (size_t k = 0; k < 3; ++k)
                    if (std::to_integer<unsigned>(bytes[detailTriangles + (d.triBase + j) * 4 + k]) >=
                        unsigned(p.vertCount) + d.vertCount) {
                        error = "Navigation detail triangle index is invalid";
                        return false;
                    }
        }
        for (int i = 0; i < h.bvNodeCount; ++i) {
            const auto node = Read<dtBVNode>(bytes, bvTree + size_t(i) * sizeof(dtBVNode));
            if (node.i >= h.polyCount || (node.i < 0 && int64_t(i) - int64_t(node.i) > h.bvNodeCount)) {
                error = "Navigation BV escape/index is invalid";
                return false;
            }
            for (size_t k = 0; k < 3; ++k)
                if (node.bmin[k] > node.bmax[k]) {
                    error = "Navigation BV bounds invalid";
                    return false;
                }
        }
        error.clear();
        return true;
    }
    struct NavigationWorld::Impl
    {
        struct FreeNav
        {
            void operator()(dtNavMesh* p) const noexcept
            {
                dtFreeNavMesh(p);
            }
        };
        std::unique_ptr<dtNavMesh, FreeNav> nav;
        std::shared_ptr<const NavMeshAsset> asset;
        uint64_t version = 0;
    };
    NavigationWorld::NavigationWorld() = default;
    NavigationWorld::~NavigationWorld() = default;
    void NavigationWorld::Unload() noexcept
    {
        m_Impl.reset();
    }
    bool NavigationWorld::Load(std::shared_ptr<const NavMeshAsset> asset, uint64_t version,
                               std::string& error)
    try {
        if (!asset || !version || !ValidateNavMeshAsset(*asset, error))
            return false;
        auto next = std::make_unique<Impl>();
        next->asset = std::move(asset);
        next->version = version;
        next->nav.reset(dtAllocNavMesh());
        if (!next->nav) {
            error = "Detour navmesh allocation failed";
            return false;
        }
        auto* data = static_cast<unsigned char*>(dtAlloc(next->asset->tile.size(), DT_ALLOC_PERM));
        if (!data) {
            error = "Detour tile allocation failed";
            return false;
        }
        std::memcpy(data, next->asset->tile.data(), next->asset->tile.size());
        const auto result = next->nav->init(data, int(next->asset->tile.size()), DT_TILE_FREE_DATA);
        if (dtStatusFailed(result)) {
            dtFree(data);
            error = "Detour tile initialization rejected";
            return false;
        }
        m_Impl = std::move(next);
        error.clear();
        return true;
    }
    catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    bool NavigationWorld::FindPath(const NavigationPathRequest& request,
                                   const CancellationToken& cancellation, NavigationPath& output,
                                   std::string& error) const
    try {
        if (!m_Impl || cancellation.IsCancellationRequested()) {
            error = "Navigation world unavailable or query cancelled";
            return false;
        }
        const float start[]{request.start.x, request.start.y, request.start.z},
            end[]{request.end.x, request.end.y, request.end.z},
            ext[]{request.nearestExtent.x, request.nearestExtent.y, request.nearestExtent.z};
        if (!Bounded(start, 1e6f) || !Bounded(end, 1e6f) || !Bounded(ext, 2e6f) || ext[0] <= 0 ||
            ext[1] <= 0 || ext[2] <= 0 || request.maxNodes < 16 || request.maxNodes > 65535 ||
            !request.maxPolygons || request.maxPolygons > 4096 || !request.maxPoints ||
            request.maxPoints > 4096) {
            error = "Navigation query parameters exceed bounds";
            return false;
        }
        dtQueryFilter filter;
        filter.setIncludeFlags(request.includeFlags);
        filter.setExcludeFlags(request.excludeFlags);
        for (int i = 0; i < 64; ++i) {
            const auto cost = request.areaCosts[i];
            if (!std::isfinite(cost) || cost < 1 || cost > 100000) {
                error = "Navigation area costs must be finite [1,100000]";
                return false;
            }
            filter.setAreaCost(i, cost);
        }
        struct FreeQuery
        {
            void operator()(dtNavMeshQuery* p) const
            {
                dtFreeNavMeshQuery(p);
            }
        };
        std::unique_ptr<dtNavMeshQuery, FreeQuery> query(dtAllocNavMeshQuery());
        if (!query || dtStatusFailed(query->init(m_Impl->nav.get(), int(request.maxNodes)))) {
            error = "Navigation query allocation failed";
            return false;
        }
        dtPolyRef a = 0, b = 0;
        float nearestA[3]{}, nearestB[3]{};
        if (dtStatusFailed(query->findNearestPoly(start, ext, &filter, &a, nearestA)) ||
            dtStatusFailed(query->findNearestPoly(end, ext, &filter, &b, nearestB)) || !a || !b) {
            error = "Navigation start or destination has no reachable polygon within nearest extent";
            return false;
        }
        std::vector<dtPolyRef> corridor(request.maxPolygons);
        int count = 0;
        auto status =
            query->findPath(a, b, nearestA, nearestB, &filter, corridor.data(), &count, int(corridor.size()));
        if (cancellation.IsCancellationRequested()) {
            error = "Navigation query cancelled";
            return false;
        }
        if (dtStatusFailed(status) || dtStatusDetail(status, DT_BUFFER_TOO_SMALL) ||
            dtStatusDetail(status, DT_OUT_OF_NODES) || dtStatusDetail(status, DT_PARTIAL_RESULT) ||
            count <= 0 || corridor[count - 1] != b) {
            error = "Navigation destination unreachable or path/node budget exhausted";
            return false;
        }
        std::vector<float> points(size_t(request.maxPoints) * 3);
        int pointCount = 0;
        status = query->findStraightPath(nearestA, nearestB, corridor.data(), count, points.data(), nullptr,
                                         nullptr, &pointCount, int(request.maxPoints));
        if (dtStatusFailed(status) || dtStatusDetail(status, DT_BUFFER_TOO_SMALL) ||
            cancellation.IsCancellationRequested()) {
            error = "Navigation straight-path budget exhausted or query cancelled";
            return false;
        }
        NavigationPath value;
        value.version = m_Impl->version;
        value.points.reserve(pointCount);
        for (int i = 0; i < pointCount; ++i)
            value.points.push_back({points[i * 3], points[i * 3 + 1], points[i * 3 + 2]});
        output = std::move(value);
        error.clear();
        return true;
    }
    catch (const std::exception& e) {
        error = e.what();
        return false;
    }
} // namespace Catalpa
