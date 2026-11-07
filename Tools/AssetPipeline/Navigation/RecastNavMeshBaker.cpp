#include "AssetPipeline/Navigation/RecastNavMeshBaker.h"
#include <Recast.h>
#include <RecastAlloc.h>
#include <DetourNavMeshBuilder.h>
#include <DetourAlloc.h>
#include <cstdlib>
#include <mutex>

namespace Catalpa::Tools {
    namespace {
        struct Budget {
            size_t used = 0, limit = 256 * 1024 * 1024;
            const CancellationToken* cancellation = nullptr;
        };
        thread_local Budget* currentBudget = nullptr;
        struct alignas(std::max_align_t) Allocation {
            size_t size;
            Budget* owner;
        };
        void* Allocate(size_t size, rcAllocHint) noexcept {
            auto* budget = currentBudget;
            if (size > SIZE_MAX - sizeof(Allocation) ||
                (budget &&
                 (budget->cancellation->IsCancellationRequested() || size > budget->limit - budget->used)))
                return nullptr;
            auto* memory = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));
            if (!memory)
                return nullptr;
            memory->size = size;
            memory->owner = budget;
            if (budget)
                budget->used += size;
            return memory + 1;
        }
        void Free(void* memory) noexcept {
            if (!memory)
                return;
            auto* allocation = static_cast<Allocation*>(memory) - 1;
            if (allocation->owner)
                allocation->owner->used -= allocation->size;
            std::free(allocation);
        }
        struct AllocationScope {
            Budget budget;
            Budget* previous = nullptr;
            explicit AllocationScope(const CancellationToken& token) {
                static std::once_flag once;
                std::call_once(once, [] { rcAllocSetCustom(Allocate, Free); });
                budget.cancellation = &token;
                previous = currentBudget;
                currentBudget = &budget;
            }
            ~AllocationScope() {
                currentBudget = previous;
            }
        };
        class Context final : public rcContext {
          public:
            std::string lastError;
            void doLog(const rcLogCategory category, const char* text, const int length) override {
                if (category == RC_LOG_ERROR && lastError.size() < 4096)
                    lastError.append(text, std::min(length, 4096 - int(lastError.size())));
            }
        };
    } // namespace
    bool RecastNavMeshBaker::Bake(const NavigationBuildInput& input, const CancellationToken& token,
                                  NavMeshAsset& output, std::string& error) try {
        if (!ValidateNavigationInput(input, error))
            return false;
        AllocationScope allocation(token);
        Context context;
        const auto check = [&](bool result, const char* stage) {
            if (token.IsCancellationRequested()) {
                error = "Navigation bake cancelled";
                return false;
            }
            if (!result) {
                error = std::string("Recast ") + stage +
                        " failed within 256 MiB working budget: " + context.lastError;
                return false;
            }
            return true;
        };
        if (!check(true, "start"))
            return false;
        std::vector<float> vertices;
        vertices.reserve(input.vertices.size() * 3);
        for (auto v : input.vertices) {
            vertices.push_back(v.x);
            vertices.push_back(v.y);
            vertices.push_back(v.z);
        }
        std::vector<int> triangles(input.indices.begin(), input.indices.end());
        const auto& s = input.settings;
        rcConfig cfg{};
        cfg.cs = s.cellSize;
        cfg.ch = s.cellHeight;
        cfg.walkableSlopeAngle = s.maxSlopeDegrees;
        cfg.walkableHeight = int(std::ceil(s.agentHeight / cfg.ch));
        cfg.walkableClimb = int(std::floor(s.maxClimb / cfg.ch));
        cfg.walkableRadius = int(std::ceil(s.agentRadius / cfg.cs));
        cfg.maxEdgeLen = int(12 / cfg.cs);
        cfg.maxSimplificationError = 1.3f;
        cfg.minRegionArea = 8 * 8;
        cfg.mergeRegionArea = 20 * 20;
        cfg.maxVertsPerPoly = 6;
        cfg.detailSampleDist = 6 * cfg.cs;
        cfg.detailSampleMaxError = cfg.ch;
        rcCalcBounds(vertices.data(), int(input.vertices.size()), cfg.bmin, cfg.bmax);
        // 平面输入也留出完整角色高度，避免高度场的零厚度和顶部截断。
        cfg.bmin[1] -= cfg.ch;
        cfg.bmax[1] += s.agentHeight + cfg.ch;
        const double width = (double(cfg.bmax[0]) - cfg.bmin[0]) / cfg.cs + 1,
                     depth = (double(cfg.bmax[2]) - cfg.bmin[2]) / cfg.cs + 1;
        if (width <= 0 || depth <= 0 || width > 65535 || depth > 65535 || width * depth > s.maxCells ||
            (double(cfg.bmax[1]) - cfg.bmin[1]) / cfg.ch > 8190) {
            error = "Navigation heightfield exceeds grid/span budget";
            return false;
        }
        rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);
        using Heightfield = std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)>;
        using Compact = std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)>;
        using Contours = std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)>;
        using PolyMesh = std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)>;
        using Detail = std::unique_ptr<rcPolyMeshDetail, decltype(&rcFreePolyMeshDetail)>;
        Heightfield height(rcAllocHeightfield(), rcFreeHeightField);
        if (!check(bool(height), "allocate heightfield") ||
            !check(rcCreateHeightfield(&context, *height, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs,
                                       cfg.ch),
                   "create heightfield"))
            return false;
        std::vector<unsigned char> areas(input.indices.size() / 3, RC_NULL_AREA);
        rcMarkWalkableTriangles(&context, cfg.walkableSlopeAngle, vertices.data(), int(input.vertices.size()),
                                triangles.data(), int(areas.size()), areas.data());
        if (!input.areas.empty())
            for (size_t i = 0; i < areas.size(); ++i)
                if (areas[i] != RC_NULL_AREA && input.areas[i])
                    areas[i] = input.areas[i];
        if (!check(rcRasterizeTriangles(&context, vertices.data(), int(input.vertices.size()),
                                        triangles.data(), areas.data(), int(areas.size()), *height,
                                        cfg.walkableClimb),
                   "rasterize"))
            return false;
        rcFilterLowHangingWalkableObstacles(&context, cfg.walkableClimb, *height);
        rcFilterLedgeSpans(&context, cfg.walkableHeight, cfg.walkableClimb, *height);
        rcFilterWalkableLowHeightSpans(&context, cfg.walkableHeight, *height);
        Compact compact(rcAllocCompactHeightfield(), rcFreeCompactHeightfield);
        if (!check(bool(compact), "allocate compact") ||
            !check(
                rcBuildCompactHeightfield(&context, cfg.walkableHeight, cfg.walkableClimb, *height, *compact),
                "compact"))
            return false;
        height.reset();
        if (!check(rcErodeWalkableArea(&context, cfg.walkableRadius, *compact), "erode") ||
            !check(rcBuildDistanceField(&context, *compact), "distance field") ||
            !check(rcBuildRegions(&context, *compact, 0, cfg.minRegionArea, cfg.mergeRegionArea), "regions"))
            return false;
        Contours contours(rcAllocContourSet(), rcFreeContourSet);
        PolyMesh mesh(rcAllocPolyMesh(), rcFreePolyMesh);
        if (!check(bool(contours) && bool(mesh), "allocate contours/mesh") ||
            !check(rcBuildContours(&context, *compact, cfg.maxSimplificationError, cfg.maxEdgeLen, *contours),
                   "contours") ||
            !check(rcBuildPolyMesh(&context, *contours, cfg.maxVertsPerPoly, *mesh), "polygon mesh"))
            return false;
        Detail detail(rcAllocPolyMeshDetail(), rcFreePolyMeshDetail);
        if (!check(bool(detail), "allocate detail") ||
            !check(rcBuildPolyMeshDetail(&context, *mesh, *compact, cfg.detailSampleDist,
                                         cfg.detailSampleMaxError, *detail),
                   "detail"))
            return false;
        if (mesh->npolys <= 0 || mesh->npolys > 32768 || mesh->nverts > 65535) {
            error = "Navigation bake produced no walkable polygons or exceeds tile limits";
            return false;
        }
        for (int i = 0; i < mesh->npolys; ++i) {
            if (mesh->areas[i] == RC_WALKABLE_AREA)
                mesh->areas[i] = 0;
            mesh->flags[i] = 1;
        }
        dtNavMeshCreateParams p{};
        p.verts = mesh->verts;
        p.vertCount = mesh->nverts;
        p.polys = mesh->polys;
        p.polyAreas = mesh->areas;
        p.polyFlags = mesh->flags;
        p.polyCount = mesh->npolys;
        p.nvp = mesh->nvp;
        p.detailMeshes = detail->meshes;
        p.detailVerts = detail->verts;
        p.detailVertsCount = detail->nverts;
        p.detailTris = detail->tris;
        p.detailTriCount = detail->ntris;
        p.walkableHeight = s.agentHeight;
        p.walkableRadius = s.agentRadius;
        p.walkableClimb = s.maxClimb;
        p.cs = cfg.cs;
        p.ch = cfg.ch;
        p.buildBvTree = true;
        std::copy_n(mesh->bmin, 3, p.bmin);
        std::copy_n(mesh->bmax, 3, p.bmax);
        unsigned char* data = nullptr;
        int bytes = 0;
        if (!check(dtCreateNavMeshData(&p, &data, &bytes), "Detour tile")) {
            dtFree(data);
            return false;
        }
        std::unique_ptr<unsigned char, decltype(&dtFree)> owned(data, dtFree);
        if (bytes <= 0 || bytes > 8 * 1024 * 1024) {
            error = "Navigation tile exceeds 8 MiB";
            return false;
        }
        NavMeshAsset value;
        value.settings = s;
        value.tile.resize(bytes);
        std::memcpy(value.tile.data(), data, bytes);
        if (!check(true, "publish") || !ValidateNavMeshAsset(value, error))
            return false;
        output = std::move(value);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
} // namespace Catalpa::Tools
