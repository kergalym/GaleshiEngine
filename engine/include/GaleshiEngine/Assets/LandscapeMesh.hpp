#pragma once
#include <memory>
#include <vector>
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Scene/Components.hpp"

namespace Galeshi {
class LandscapeMesh {
public:
    static void Rebuild(LandscapeComponent& landscape, Mesh& mesh);
};
}
