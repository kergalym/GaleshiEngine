#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Galeshi {

class Material;

// Declarative arithmetic recipe for material instances. A .mat file is JSON:
//
//   {
//     "name": "Rough Metal",
//     "properties": {
//       "metallicFactor": { "mul": 0.5 },
//       "roughnessFactor": { "add": 0.1 },
//       "albedoFactor": { "mul": [0.8, 0.8, 0.9, 1.0] }
//     }
//   }
//
// A plain number/array/bool is shorthand for "set". Supported operators are
// set, add, sub(subtract), mul(multiply), div(divide), min, max and clamp.
class MaterialScript {
public:
    // Public so the translation unit can use these in assignment helpers.
    enum class Operation {
        Set,
        Add,
        Subtract,
        Multiply,
        Divide,
        Min,
        Max,
        Clamp
    };
    struct Assignment {
        std::string property;
        Operation operation = Operation::Set;
        std::vector<float> values;
        bool booleanValue = false;
        bool isBoolean = false;
    };

    bool Load(const std::string& path);
    bool Apply(Material& material) const;
    bool Valid() const { return !m_path.empty(); }
    const std::string& Path() const { return m_path; }
    const std::string& Name() const { return m_name; }

private:
    std::string m_path;
    std::string m_name;
    std::vector<Assignment> m_assignments;
};

} // namespace Galeshi
