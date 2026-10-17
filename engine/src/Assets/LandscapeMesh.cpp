#include "GaleshiEngine/Assets/LandscapeMesh.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Galeshi {

void LandscapeMesh::Rebuild(LandscapeComponent& lc, Mesh& mesh) {
    if (lc.heights.empty()) {
        lc.heights.resize(lc.sizeX * lc.sizeZ, 0.0f);
    }

    std::vector<Vertex> verts;
    verts.reserve(lc.sizeX * lc.sizeZ);
    
    // Создаем вершины
    for (int z = 0; z < lc.sizeZ; ++z) {
        for (int x = 0; x < lc.sizeX; ++x) {
            Vertex v{};
            v.position = glm::vec3(
                (x - lc.sizeX / 2.0f) * lc.scale,
                lc.heights[z * lc.sizeX + x],
                (z - lc.sizeZ / 2.0f) * lc.scale
            );
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f); // Упрощенно
            v.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            v.uv = glm::vec2((float)x / lc.sizeX, (float)z / lc.sizeZ);
            v.boneIDs = glm::vec4(0.0f);
            v.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            verts.push_back(v);
        }
    }

    // Создаем индексы
    std::vector<uint32_t> indices;
    indices.reserve((lc.sizeX - 1) * (lc.sizeZ - 1) * 6);
    for (int z = 0; z < lc.sizeZ - 1; ++z) {
        for (int x = 0; x < lc.sizeX - 1; ++x) {
            uint32_t tl = z * lc.sizeX + x;
            uint32_t tr = tl + 1;
            uint32_t bl = (z + 1) * lc.sizeX + x;
            uint32_t br = bl + 1;
            
            // Два треугольника на ячейку
            indices.push_back(tl); indices.push_back(bl); indices.push_back(tr);
            indices.push_back(tr); indices.push_back(bl); indices.push_back(br);
        }
    }

    // Расчет нормалей (упрощенный)
    for (size_t i = 0; i < indices.size(); i += 3) {
        glm::vec3& p0 = verts[indices[i]].position;
        glm::vec3& p1 = verts[indices[i+1]].position;
        glm::vec3& p2 = verts[indices[i+2]].position;
        glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        verts[indices[i]].normal += n;
        verts[indices[i+1]].normal += n;
        verts[indices[i+2]].normal += n;
    }
    for (auto& v : verts) {
        v.normal = glm::normalize(v.normal);
    }

    mesh.Upload(verts, indices);
}

}
