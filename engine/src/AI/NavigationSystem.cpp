#include "GaleshiEngine/AI/NavigationSystem.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>

#include <spdlog/spdlog.h>
#include <cmath>
#include <cstring>
#include <cfloat>
#include <algorithm>

namespace Galeshi {

struct NavigationSystem::Impl {
    dtNavMesh* navMesh = nullptr;
    dtNavMeshQuery* navQuery = nullptr;
    unsigned char* navData = nullptr;
    int navDataSize = 0;
};

NavigationSystem::NavigationSystem() : m_impl(std::make_unique<Impl>()) {}
NavigationSystem::~NavigationSystem() { Shutdown(); }

void NavigationSystem::Init() {
    m_impl->navQuery = dtAllocNavMeshQuery();
}

void NavigationSystem::Shutdown() {
    // Идемпотентный: можно вызывать многократно
    if (m_impl->navQuery) {
        dtFreeNavMeshQuery(m_impl->navQuery);
        m_impl->navQuery = nullptr;
    }
    if (m_impl->navMesh) {
        dtFreeNavMesh(m_impl->navMesh);
        m_impl->navMesh = nullptr;
    }
    // navData владеется navMesh (DT_TILE_FREE_DATA), отдельно не освобождаем
    m_impl->navData = nullptr;
    m_impl->navDataSize = 0;
}

bool NavigationSystem::BuildFromScene(Scene& scene) {
    spdlog::info("[NavMesh] BuildFromScene started");

    // --- 1. Собираем геометрию ---
    std::vector<float> verts;
    std::vector<int>   tris;

    auto view = scene.Registry().view<Galeshi::MeshComponent>();
    for (auto e : view) {
        const auto& mc = view.get<Galeshi::MeshComponent>(e);
        if (!mc.mesh) continue;

        // Skip skeletal meshes (characters)
        if (scene.Registry().all_of<Galeshi::SkeletalComponent>(e)) continue;
        // Skip cloth
        if (mc.primitiveType == "cloth") continue;
        // Skip dynamic physics objects (falling cubes)
        if (scene.Registry().all_of<Galeshi::RigidBodyComponent>(e)) {
            const auto& rb = scene.Registry().get<Galeshi::RigidBodyComponent>(e);
            if (rb.motionType == Galeshi::RigidBodyComponent::MotionType::Dynamic) continue;
        }

        // Build world matrix
        glm::mat4 model(1.0f);
        entt::entity curr = e;
        std::vector<entt::entity> chain;
        int depth = 0;
        while (curr != entt::null && depth++ < 128) {
            chain.push_back(curr);
            if (scene.Registry().all_of<Galeshi::ParentComponent>(curr))
                curr = scene.Registry().get<Galeshi::ParentComponent>(curr).parent;
            else
                break;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (scene.Registry().all_of<Galeshi::TransformComponent>(*it))
                model = model * scene.Registry().get<Galeshi::TransformComponent>(*it).LocalMatrix();
        }

        const auto& v   = mc.mesh->Vertices();
        const auto& idx = mc.mesh->Indices();
        if (v.empty() || idx.empty()) continue;

        int baseIndex = static_cast<int>(verts.size() / 3);
        for (const auto& vert : v) {
            glm::vec4 worldPos = model * glm::vec4(vert.position, 1.0f);
            verts.push_back(worldPos.x);
            verts.push_back(worldPos.y);
            verts.push_back(worldPos.z);
        }
        for (auto i : idx) {
            tris.push_back(baseIndex + static_cast<int>(i));
        }
        spdlog::info("[NavMesh] Added mesh ({} verts, {} tris)", v.size(), idx.size() / 3);
    }

    if (verts.empty() || tris.empty()) {
        spdlog::warn("[NavMesh] No landscape geometry found!");
        return false;
    }

    // --- 2. Вычисляем границы из геометрии ---
    float bmin[3] = { FLT_MAX,  FLT_MAX,  FLT_MAX};
    float bmax[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (size_t i = 0; i < verts.size(); i += 3) {
        bmin[0] = std::min(bmin[0], verts[i+0]);
        bmin[1] = std::min(bmin[1], verts[i+1]);
        bmin[2] = std::min(bmin[2], verts[i+2]);
        bmax[0] = std::max(bmax[0], verts[i+0]);
        bmax[1] = std::max(bmax[1], verts[i+1]);
        bmax[2] = std::max(bmax[2], verts[i+2]);
    }
    spdlog::info("[NavMesh] Bounds: [{:.1f},{:.1f},{:.1f}] - [{:.1f},{:.1f},{:.1f}]",
                 bmin[0], bmin[1], bmin[2], bmax[0], bmax[1], bmax[2]);

    // Небольшой padding
    bmin[0] -= 1.0f; bmin[2] -= 1.0f;
    bmax[0] += 1.0f; bmax[2] += 1.0f;

    // --- 3. Конфигурация Recast ---
    rcConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.cs = 0.5f;
    cfg.ch = 0.25f;
    cfg.walkableSlopeAngle = 45.0f;
    cfg.walkableHeight = static_cast<int>(std::ceil(2.0f / cfg.ch));   // 2m
    cfg.walkableClimb = static_cast<int>(std::floor(0.5f / cfg.ch));   // 0.5m
    cfg.walkableRadius = static_cast<int>(std::ceil(0.5f / cfg.cs));   // 0.5m
    cfg.maxEdgeLen = static_cast<int>(12.0f / cfg.cs);
    cfg.maxSimplificationError = 1.3f;
    cfg.minRegionArea = 8;
    cfg.mergeRegionArea = 20;
    cfg.maxVertsPerPoly = 6;
    cfg.detailSampleDist = 6.0f * cfg.cs;
    cfg.detailSampleMaxError = 1.0f * cfg.ch;

    std::memcpy(cfg.bmin, bmin, sizeof(bmin));
    std::memcpy(cfg.bmax, bmax, sizeof(bmax));
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

    spdlog::info("[NavMesh] Grid: {}x{} cells", cfg.width, cfg.height);

    // Защита от огромных сеток
    if (cfg.width > 4096 || cfg.height > 4096) {
        spdlog::warn("[NavMesh] Grid too large ({}x{}), aborting", cfg.width, cfg.height);
        return false;
    }

    rcContext ctx;
    const int nverts = static_cast<int>(verts.size() / 3);
    const int ntris  = static_cast<int>(tris.size()  / 3);

    // --- 4. Mark walkable triangles ---
    std::vector<unsigned char> triAreas(ntris, 0);
    rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, verts.data(), nverts,
                             tris.data(), ntris, triAreas.data());
    int walkableCount = 0;
    for (auto a : triAreas) if (a) walkableCount++;
    spdlog::info("[NavMesh] Walkable tris: {}/{}", walkableCount, ntris);

    if (walkableCount == 0) {
        spdlog::warn("[NavMesh] No walkable triangles!");
        return false;
    }

    // --- 5. Heightfield ---
    rcHeightfield* hf = rcAllocHeightfield();
    if (!hf) { spdlog::error("[NavMesh] rcAllocHeightfield OOM"); return false; }
    if (!rcCreateHeightfield(&ctx, *hf, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) {
        spdlog::error("[NavMesh] rcCreateHeightfield failed");
        rcFreeHeightField(hf);
        return false;
    }
    if (!rcRasterizeTriangles(&ctx, verts.data(), nverts, tris.data(), triAreas.data(),
                              ntris, *hf, cfg.walkableClimb)) {
        spdlog::error("[NavMesh] rcRasterizeTriangles failed");
        rcFreeHeightField(hf);
        return false;
    }
    spdlog::info("[NavMesh] Heightfield rasterized");

    // --- 6. Compact heightfield ---
    rcCompactHeightfield* chf = rcAllocCompactHeightfield();
    if (!chf || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *hf, *chf)) {
        spdlog::error("[NavMesh] rcBuildCompactHeightfield failed");
        if (chf) rcFreeCompactHeightfield(chf);
        rcFreeHeightField(hf);
        return false;
    }
    rcFreeHeightField(hf);
    spdlog::info("[NavMesh] Compact heightfield built ({} spans)", chf->spanCount);

    // --- 7. Erode, distance, regions ---
    if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf)) {
        spdlog::error("[NavMesh] rcErodeWalkableArea failed");
        rcFreeCompactHeightfield(chf); return false;
    }
    if (!rcBuildDistanceField(&ctx, *chf)) {
        spdlog::error("[NavMesh] rcBuildDistanceField failed");
        rcFreeCompactHeightfield(chf); return false;
    }
    if (!rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea)) {
        spdlog::error("[NavMesh] rcBuildRegions failed");
        rcFreeCompactHeightfield(chf); return false;
    }
    spdlog::info("[NavMesh] Regions built");

    // --- 8. Contours ---
    rcContourSet* cset = rcAllocContourSet();
    if (!cset || !rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset)) {
        spdlog::error("[NavMesh] rcBuildContours failed");
        if (cset) rcFreeContourSet(cset);
        rcFreeCompactHeightfield(chf); return false;
    }
    spdlog::info("[NavMesh] Contours: {} contour sets", cset->nconts);

    // --- 9. PolyMesh ---
    rcPolyMesh* pmesh = rcAllocPolyMesh();
    if (!pmesh || !rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh)) {
        spdlog::error("[NavMesh] rcBuildPolyMesh failed");
        if (pmesh) rcFreePolyMesh(pmesh);
        rcFreeContourSet(cset); rcFreeCompactHeightfield(chf); return false;
    }
    rcFreeContourSet(cset);
    spdlog::info("[NavMesh] PolyMesh: {} polys, {} verts", pmesh->npolys, pmesh->nverts);

    if (pmesh->npolys == 0 || pmesh->nverts == 0) {
        spdlog::warn("[NavMesh] Empty polymesh");
        rcFreePolyMesh(pmesh); rcFreeCompactHeightfield(chf); return false;
    }

    // --- 10. PolyMeshDetail ---
    rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
    if (!dmesh || !rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh)) {
        spdlog::error("[NavMesh] rcBuildPolyMeshDetail failed");
        if (dmesh) rcFreePolyMeshDetail(dmesh);
        rcFreePolyMesh(pmesh); rcFreeCompactHeightfield(chf); return false;
    }
    rcFreeCompactHeightfield(chf);
    spdlog::info("[NavMesh] Detail mesh: {} tris", dmesh->ntris);

    if (dmesh->nverts == 0 || dmesh->ntris == 0) {
        spdlog::warn("[NavMesh] Empty detail mesh");
        rcFreePolyMeshDetail(dmesh); rcFreePolyMesh(pmesh); return false;
    }

    // --- 11. dtCreateNavMeshData ---
    for (int i = 0; i < pmesh->npolys; ++i) pmesh->flags[i] = 1;

    dtNavMeshCreateParams params;
    std::memset(&params, 0, sizeof(params));
    params.verts = pmesh->verts;
    params.vertCount = pmesh->nverts;
    params.polys = pmesh->polys;
    params.polyAreas = pmesh->areas;
    params.polyFlags = pmesh->flags;
    params.polyCount = pmesh->npolys;
    params.nvp = pmesh->nvp;
    params.detailMeshes = dmesh->meshes;
    params.detailVerts = dmesh->verts;
    params.detailVertsCount = dmesh->nverts;
    params.detailTris = dmesh->tris;
    params.detailTriCount = dmesh->ntris;
    params.walkableHeight = static_cast<float>(cfg.walkableHeight) * cfg.ch;
    params.walkableRadius = static_cast<float>(cfg.walkableRadius) * cfg.cs;
    params.walkableClimb  = static_cast<float>(cfg.walkableClimb)  * cfg.ch;
    rcVcopy(params.bmin, pmesh->bmin);
    rcVcopy(params.bmax, pmesh->bmax);
    params.cs = cfg.cs;
    params.ch = cfg.ch;
    params.buildBvTree = true;

    unsigned char* navData = nullptr;
    int navDataSize = 0;
    if (!dtCreateNavMeshData(&params, &navData, &navDataSize)) {
        spdlog::error("[NavMesh] dtCreateNavMeshData failed");
        rcFreePolyMeshDetail(dmesh); rcFreePolyMesh(pmesh);
        return false;
    }
    rcFreePolyMeshDetail(dmesh);
    rcFreePolyMesh(pmesh);

    // --- 12. Инициализация navMesh ---
    if (m_impl->navMesh) { dtFreeNavMesh(m_impl->navMesh); m_impl->navMesh = nullptr; }
    m_impl->navMesh = dtAllocNavMesh();
    if (!m_impl->navMesh) { dtFree(navData); return false; }

    dtStatus status = m_impl->navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA);
    if (dtStatusFailed(status)) {
        spdlog::error("[NavMesh] navMesh->init failed (status {})", static_cast<int>(status));
        dtFreeNavMesh(m_impl->navMesh); m_impl->navMesh = nullptr;
        return false;
    }

    m_impl->navQuery->init(m_impl->navMesh, 2048);

    spdlog::info("[NavMesh] Built successfully! navDataSize={} bytes", navDataSize);
    return true;
}

bool NavigationSystem::FindPath(const glm::vec3& start, const glm::vec3& end,
                                std::vector<glm::vec3>& outPath)
{
    outPath.clear();
    if (!m_impl->navMesh || !m_impl->navQuery) return false;

    dtQueryFilter filter;
    filter.setIncludeFlags(1);

    float startPos[3] = {start.x, start.y + 0.5f, start.z};
    float endPos[3]   = {end.x,   end.y   + 0.5f, end.z};
    float extents[3]  = {2.0f, 4.0f, 2.0f};

    dtPolyRef startRef = 0, endRef = 0;
    m_impl->navQuery->findNearestPoly(startPos, extents, &filter, &startRef, nullptr);
    m_impl->navQuery->findNearestPoly(endPos,   extents, &filter, &endRef,   nullptr);

    if (!startRef || !endRef) {
        spdlog::warn("[NavMesh] findNearestPoly failed");
        return false;
    }

    dtPolyRef path[256];
    int pathCount = 0;
    m_impl->navQuery->findPath(startRef, endRef, startPos, endPos, &filter,
                                path, &pathCount, 256);
    if (pathCount == 0) return false;

    float straightPath[256 * 3];
    unsigned char straightPathFlags[256];
    dtPolyRef straightPathPolys[256];
    int straightPathCount = 0;

    m_impl->navQuery->findStraightPath(startPos, endPos, path, pathCount,
        straightPath, straightPathFlags, straightPathPolys, &straightPathCount, 256);

    for (int i = 0; i < straightPathCount; ++i) {
        outPath.emplace_back(straightPath[i*3+0], straightPath[i*3+1], straightPath[i*3+2]);
    }

    spdlog::info("[NavMesh] Path found: {} waypoints", straightPathCount);
    return !outPath.empty();
}

} // namespace Galeshi
