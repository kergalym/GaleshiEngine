#include "GaleshiEngine/Assets/MaterialScript.hpp"
#include "GaleshiEngine/Assets/Material.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace Galeshi {
namespace {

using json = nlohmann::json;

bool ReadValues(const json& value, std::vector<float>& out) {
    out.clear();
    if (value.is_number()) {
        out.push_back(value.get<float>());
        return true;
    }
    if (value.is_boolean()) {
        out.push_back(value.get<bool>() ? 1.0f : 0.0f);
        return true;
    }
    if (value.is_array()) {
        out.reserve(value.size());
        for (const auto& component : value) {
            if (!component.is_number()) return false;
            out.push_back(component.get<float>());
        }
        return !out.empty() && out.size() <= 4;
    }
    return false;
}

bool ParseOperation(const std::string& key, MaterialScript::Operation& out) {
    if (key == "set") out = MaterialScript::Operation::Set;
    else if (key == "add") out = MaterialScript::Operation::Add;
    else if (key == "sub" || key == "subtract") {
        out = MaterialScript::Operation::Subtract;
    } else if (key == "mul" || key == "multiply") {
        out = MaterialScript::Operation::Multiply;
    } else if (key == "div" || key == "divide") {
        out = MaterialScript::Operation::Divide;
    } else if (key == "min") out = MaterialScript::Operation::Min;
    else if (key == "max") out = MaterialScript::Operation::Max;
    else if (key == "clamp") out = MaterialScript::Operation::Clamp;
    else return false;
    return true;
}

bool ParseAssignment(const std::string& property, const json& value,
                     MaterialScript::Assignment& out) {
    out = {};
    out.property = property;
    // Plain values are the common case and mean "set".
    if (!value.is_object()) {
        out.operation = MaterialScript::Operation::Set;
        if (value.is_boolean()) {
            out.isBoolean = true;
            out.booleanValue = value.get<bool>();
        } else if (!ReadValues(value, out.values)) {
            return false;
        }
        return true;
    }

    json operand = value;
    if (value.contains("op") && value["op"].is_string()) {
        if (!ParseOperation(value["op"].get<std::string>(), out.operation)) {
            return false;
        }
        if (!value.contains("value")) return false;
        operand = value["value"];
    } else {
        bool found = false;
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (!ParseOperation(it.key(), out.operation)) continue;
            operand = it.value();
            found = true;
            break;
        }
        if (!found) return false;
    }

    if (out.operation == MaterialScript::Operation::Clamp) {
        if (!ReadValues(operand, out.values) || out.values.size() != 2) {
            return false;
        }
        return true;
    }
    if (operand.is_boolean()) {
        if (out.operation != MaterialScript::Operation::Set) return false;
        out.isBoolean = true;
        out.booleanValue = operand.get<bool>();
        return true;
    }
    return ReadValues(operand, out.values);
}

float At(const std::vector<float>& values, size_t index) {
    if (values.empty()) return 0.0f;
    if (values.size() == 1) return values[0];
    return values[std::min(index, values.size() - 1)];
}

float ApplyComponent(float current, MaterialScript::Operation operation,
                     const std::vector<float>& values, size_t index) {
    const float value = At(values, index);
    switch (operation) {
    case MaterialScript::Operation::Set: return value;
    case MaterialScript::Operation::Add: return current + value;
    case MaterialScript::Operation::Subtract: return current - value;
    case MaterialScript::Operation::Multiply: return current * value;
    case MaterialScript::Operation::Divide:
        return std::abs(value) > 1e-8f ? current / value : current;
    case MaterialScript::Operation::Min: return std::min(current, value);
    case MaterialScript::Operation::Max: return std::max(current, value);
    case MaterialScript::Operation::Clamp:
        return std::clamp(current, At(values, 0), At(values, 1));
    }
    return current;
}

bool ApplyScalar(float& target, const MaterialScript::Assignment& assignment) {
    if (assignment.isBoolean) {
        if (assignment.operation != MaterialScript::Operation::Set) return false;
        target = assignment.booleanValue ? 1.0f : 0.0f;
        return true;
    }
    if (assignment.values.empty()) return false;
    target = ApplyComponent(target, assignment.operation, assignment.values, 0);
    return true;
}

bool ApplyVector3(glm::vec3& target,
                  const MaterialScript::Assignment& assignment) {
    if (assignment.isBoolean || assignment.values.empty()) return false;
    glm::vec3 result = target;
    for (size_t i = 0; i < 3; ++i) {
        result[i] = ApplyComponent(target[i], assignment.operation,
                                   assignment.values, i);
    }
    target = result;
    return true;
}

bool ApplyVector4(glm::vec4& target,
                  const MaterialScript::Assignment& assignment) {
    if (assignment.isBoolean || assignment.values.empty()) return false;
    glm::vec4 result = target;
    for (size_t i = 0; i < 3; ++i) {
        result[i] = ApplyComponent(target[i], assignment.operation,
                                   assignment.values, i);
    }
    if (assignment.operation == MaterialScript::Operation::Set) {
        result.w = assignment.values.size() >= 4
            ? assignment.values[3]
            : 1.0f;
    } else {
        result.w = ApplyComponent(target.w, assignment.operation,
                                  assignment.values, 3);
    }
    target = result;
    return true;
}

bool ApplyBoolean(bool& target,
                  const MaterialScript::Assignment& assignment) {
    if (assignment.operation != MaterialScript::Operation::Set) return false;
    if (assignment.isBoolean) {
        target = assignment.booleanValue;
        return true;
    }
    if (assignment.values.empty()) return false;
    target = assignment.values[0] != 0.0f;
    return true;
}

} // namespace

bool MaterialScript::Load(const std::string& path) {
    m_path.clear();
    m_name.clear();
    m_assignments.clear();
    if (path.empty()) return false;

    std::ifstream input(path);
    if (!input.is_open()) {
        spdlog::warn("[MaterialScript] Cannot open '{}'", path);
        return false;
    }
    json root;
    try {
        input >> root;
    } catch (const std::exception& e) {
        spdlog::warn("[MaterialScript] Invalid JSON in '{}': {}", path,
                     e.what());
        return false;
    }
    if (!root.is_object()) {
        spdlog::warn("[MaterialScript] Root must be an object: {}", path);
        return false;
    }
    if (!root.contains("properties") || !root["properties"].is_object()) {
        spdlog::warn("[MaterialScript] Missing properties object: {}", path);
        return false;
    }

    m_path = path;
    m_name = root.value("name", std::filesystem::path(path).stem().string());
    for (auto it = root["properties"].begin();
         it != root["properties"].end(); ++it) {
        Assignment assignment;
        if (!ParseAssignment(it.key(), it.value(), assignment)) {
            spdlog::warn("[MaterialScript] Invalid assignment '{}' in {}",
                         it.key(), path);
            continue;
        }
        m_assignments.push_back(std::move(assignment));
    }
    return true;
}

bool MaterialScript::Apply(Material& material) const {
    bool applied = false;
    for (const Assignment& assignment : m_assignments) {
        const std::string& name = assignment.property;
        if (name == "albedoFactor") {
            applied |= ApplyVector4(material.albedoFactor, assignment);
        } else if (name == "emissiveFactor") {
            applied |= ApplyVector3(material.emissiveFactor, assignment);
        } else if (name == "subsurfaceColor") {
            applied |= ApplyVector3(material.subsurfaceColor, assignment);
        } else if (name == "translucencyColor") {
            applied |= ApplyVector3(material.translucencyColor, assignment);
        } else if (name == "metallicFactor") {
            applied |= ApplyScalar(material.metallicFactor, assignment);
        } else if (name == "specularFactor") {
            applied |= ApplyScalar(material.specularFactor, assignment);
        } else if (name == "roughnessFactor") {
            applied |= ApplyScalar(material.roughnessFactor, assignment);
        } else if (name == "normalStrength") {
            applied |= ApplyScalar(material.normalStrength, assignment);
        } else if (name == "occlusionStrength") {
            applied |= ApplyScalar(material.occlusionStrength, assignment);
        } else if (name == "alphaCutoff") {
            applied |= ApplyScalar(material.alphaCutoff, assignment);
        } else if (name == "glassOpacity") {
            applied |= ApplyScalar(material.glassOpacity, assignment);
        } else if (name == "subsurfaceStrength") {
            applied |= ApplyScalar(material.subsurfaceStrength, assignment);
        } else if (name == "subsurfaceRadius") {
            applied |= ApplyScalar(material.subsurfaceRadius, assignment);
        } else if (name == "translucencyOpacity") {
            applied |= ApplyScalar(material.translucencyOpacity, assignment);
        } else if (name == "translucencyStrength") {
            applied |= ApplyScalar(material.translucencyStrength, assignment);
        } else if (name == "translucencyThickness") {
            applied |= ApplyScalar(material.translucencyThickness,
                                   assignment);
        } else if (name == "translucencyDistortion") {
            applied |= ApplyScalar(material.translucencyDistortion,
                                   assignment);
        } else if (name == "translucencyShadowDepth") {
            applied |= ApplyScalar(material.translucencyShadowDepth,
                                   assignment);
        } else if (name == "shadowDepthOffset") {
            applied |= ApplyScalar(material.shadowDepthOffset, assignment);
        } else if (name == "landscapeUvScale") {
            applied |= ApplyScalar(material.landscapeUvScale, assignment);
        } else if (name == "landscapeDetailScale") {
            applied |= ApplyScalar(material.landscapeDetailScale, assignment);
        } else if (name == "landscapeDetailStrength") {
            applied |= ApplyScalar(material.landscapeDetailStrength,
                                   assignment);
        } else if (name == "landscapeNormalStrength") {
            applied |= ApplyScalar(material.landscapeNormalStrength,
                                   assignment);
        } else if (name == "isGlass") {
            applied |= ApplyBoolean(material.isGlass, assignment);
        } else if (name == "twoSided") {
            applied |= ApplyBoolean(material.twoSided, assignment);
        } else if (name == "backfaceCulling") {
            applied |= ApplyBoolean(material.backfaceCulling, assignment);
        } else if (name == "alphaTest") {
            applied |= ApplyBoolean(material.alphaTest, assignment);
        } else if (name == "alphaBlend") {
            applied |= ApplyBoolean(material.alphaBlend, assignment);
        } else if (name == "subsurfaceEnabled") {
            applied |= ApplyBoolean(material.subsurfaceEnabled, assignment);
        } else if (name == "translucencyEnabled") {
            applied |= ApplyBoolean(material.translucencyEnabled, assignment);
        } else if (name == "castsShadow") {
            applied |= ApplyBoolean(material.castsShadow, assignment);
        } else {
            spdlog::warn("[MaterialScript] Unknown material property '{}'",
                         name);
        }
    }
    return applied;
}

} // namespace Galeshi
