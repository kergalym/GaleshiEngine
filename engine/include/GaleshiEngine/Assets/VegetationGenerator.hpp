#pragma once

#include "GaleshiEngine/Scene/Components.hpp"

namespace Galeshi {

class VegetationGenerator {
public:
    static void Generate(const LandscapeComponent& landscape,
                         VegetationComponent& vegetation);
    static void Clear(VegetationComponent& vegetation);
};

} // namespace Galeshi
