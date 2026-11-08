#include "GaleshiEditorApp.hpp"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include <stb_image.h>
#include <png.h>



    void GaleshiEditorApp::PaintVegetationAt(Galeshi::Entity landscapeEntity,
                           const Galeshi::LandscapeComponent& landscape,
                           const glm::vec3& localPosition,
                           bool paintGrass) {
        if (!m_assets || !landscapeEntity ||
            !landscapeEntity.HasComponent<Galeshi::VegetationComponent>()) {
            return;
        }
        auto& vegetation =
            landscapeEntity.GetComponent<Galeshi::VegetationComponent>();
        if (!vegetation.enabled || vegetation.paintDensity <= 0.0f) return;

        const float radius = std::max(0.05f, m_editor.brushRadius);
        const float halfX = landscape.sizeX * 0.5f * landscape.scale;
        const float halfZ = landscape.sizeZ * 0.5f * landscape.scale;
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        std::uniform_real_distribution<float> angle(0.0f, 6.2831853f);

        Galeshi::VegetationTreeType* treeType = nullptr;
        if (!paintGrass && m_paintTreeType >= 0 &&
            m_paintTreeType < static_cast<int>(vegetation.treeTypes.size())) {
            treeType = &vegetation.treeTypes[
                static_cast<size_t>(m_paintTreeType)];
        }

        const int stamps = std::clamp(
            static_cast<int>(std::round(vegetation.paintDensity * 6.0f)),
            0, 96);
        if (stamps == 0) return;

        auto& painted = paintGrass
            ? vegetation.paintedGrassInstances
            : (treeType ? treeType->paintedInstances
                        : vegetation.paintedTreeInstances);
        painted.reserve(painted.size() + static_cast<size_t>(stamps));

        for (int i = 0; i < stamps; ++i) {
            const float a = angle(m_paintRng);
            const float r = std::sqrt(unit(m_paintRng)) * radius;
            const float x = localPosition.x + std::cos(a) * r;
            const float z = localPosition.z + std::sin(a) * r;
            if (std::abs(x) > halfX || std::abs(z) > halfZ) continue;

            const float y = Galeshi::LandscapeMesh::SampleHeight(
                landscape, x, z);
            const float baseSize = paintGrass
                ? vegetation.paintSize
                : vegetation.paintSize * (treeType ? treeType->scale : 1.0f);
            const float scale = baseSize * (0.82f + unit(m_paintRng) * 0.36f);
            if (scale <= 0.0f) continue;
            const float yaw = angle(m_paintRng);

            glm::mat4 transform = glm::translate(
                glm::mat4(1.0f), glm::vec3(x, y, z));
            transform = glm::rotate(transform, yaw,
                                    glm::vec3(0.0f, 1.0f, 0.0f));
            transform = glm::scale(transform, glm::vec3(scale));
            painted.push_back(transform);
        }
    }



    // shortcut: layer sources are decoded with stb (PNG/JPEG/BMP/TGA).
    // DDS/KTX layer maps fall back to the base texture. Upgrade this to the
    // engine's bimg container decoder if compressed layer sources are needed.
    bool GaleshiEditorApp::LoadLandscapeImage(const std::string& path,  LandscapeImage& out) {
        out = {};
        if (path.empty()) return false;
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_set_flip_vertically_on_load(false);
        unsigned char* pixels = stbi_load(
            path.c_str(), &width, &height, &channels, 4);
        if (!pixels) return false;
        out.width = width;
        out.height = height;
        out.rgba.assign(pixels,
                        pixels + static_cast<size_t>(width) * height * 4);
        stbi_image_free(pixels);
        return out.Valid();
    }



    glm::vec4 GaleshiEditorApp::SampleLandscapeImage(
        const LandscapeImage& image,  float u,  float v,
        const glm::vec4& fallback) {
        if (!image.Valid()) return fallback;
        u = glm::clamp(u, 0.0f, 1.0f);
        v = glm::clamp(v, 0.0f, 1.0f);
        const float x = u * static_cast<float>(image.width - 1);
        const float y = v * static_cast<float>(image.height - 1);
        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const int x1 = std::min(x0 + 1, image.width - 1);
        const int y1 = std::min(y0 + 1, image.height - 1);
        const float fx = x - static_cast<float>(x0);
        const float fy = y - static_cast<float>(y0);
        const auto sample = [&](int px, int py) {
            const size_t index =
                (static_cast<size_t>(py) * image.width + px) * 4;
            return glm::vec4(
                static_cast<float>(image.rgba[index + 0]) / 255.0f,
                static_cast<float>(image.rgba[index + 1]) / 255.0f,
                static_cast<float>(image.rgba[index + 2]) / 255.0f,
                static_cast<float>(image.rgba[index + 3]) / 255.0f);
        };
        return glm::mix(
            glm::mix(sample(x0, y0), sample(x1, y0), fx),
            glm::mix(sample(x0, y1), sample(x1, y1), fx), fy);
    }



    float GaleshiEditorApp::SampleLandscapeWeight(
        const std::vector<float>& weights,  int sizeX,  int sizeZ,
        float x,  float z) {
        if (weights.empty() || sizeX < 2 || sizeZ < 2) return 0.0f;
        x = glm::clamp(x, 0.0f, static_cast<float>(sizeX - 1));
        z = glm::clamp(z, 0.0f, static_cast<float>(sizeZ - 1));
        const int x0 = static_cast<int>(std::floor(x));
        const int z0 = static_cast<int>(std::floor(z));
        const int x1 = std::min(x0 + 1, sizeX - 1);
        const int z1 = std::min(z0 + 1, sizeZ - 1);
        const float fx = x - static_cast<float>(x0);
        const float fz = z - static_cast<float>(z0);
        const auto sample = [&](int px, int pz) {
            return weights[static_cast<size_t>(pz) * sizeX + px];
        };
        return glm::mix(
            glm::mix(sample(x0, z0), sample(x1, z0), fx),
            glm::mix(sample(x0, z1), sample(x1, z1), fx), fz);
    }



    bool GaleshiEditorApp::SaveLandscapePng(const std::filesystem::path& path,
                                 const LandscapeImage& image) {
        if (!image.Valid()) return false;
        png_image png{};
        png.version = PNG_IMAGE_VERSION;
        png.width = static_cast<png_uint_32>(image.width);
        png.height = static_cast<png_uint_32>(image.height);
        png.format = PNG_FORMAT_RGBA;
        return png_image_write_to_file(
                   &png, path.string().c_str(), 0, image.rgba.data(), 0,
                   nullptr) != 0;
    }



    std::string GaleshiEditorApp::SafeLandscapeTextureName(const std::string& name) {
        std::string safe;
        for (char c : name) {
            safe.push_back(
                std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
        }
        if (safe.empty()) safe = "Landscape";
        return safe;
    }



    bool GaleshiEditorApp::BakeLandscapeTextures(Galeshi::Entity entity) {
        if (!entity || !entity.HasComponent<Galeshi::LandscapeComponent>()) {
            return false;
        }
        auto& landscape =
            entity.GetComponent<Galeshi::LandscapeComponent>();
        auto& registry = m_scene.Registry();
        auto* materialComponent =
            registry.try_get<Galeshi::MaterialComponent>(entity.Handle());
        if (!materialComponent) {
            materialComponent =
                &entity.AddComponent<Galeshi::MaterialComponent>();
        }
        if (!materialComponent->material) {
            materialComponent->material = std::make_shared<Galeshi::Material>();
        }
        auto& material = materialComponent->material;

        // Capture the untouched base maps once, before the generated maps
        // become the material's visible texture slots.
        if (!landscape.baseCaptured &&
            !landscape.bakedAlbedoPath.empty()) {
            // An older save may already point the material at baked maps.
            // Do not feed those back as the base.
            landscape.baseCaptured = true;
        }
        if (!landscape.baseCaptured) {
            const auto usable = [](const std::shared_ptr<Galeshi::Texture>& t) {
                return t && t->Path() != "<white>";
            };
            if (usable(material->albedoMap)) {
                landscape.baseAlbedoPath = material->albedoMap->Path();
            }
            if (usable(material->metallicRoughnessMap)) {
                landscape.baseOrmPath =
                    material->metallicRoughnessMap->Path();
            }
            if (usable(material->metallicMap)) {
                landscape.baseMetallicPath =
                    material->metallicMap->Path();
            }
            if (usable(material->roughnessMap)) {
                landscape.baseRoughnessPath =
                    material->roughnessMap->Path();
            }
            if (usable(material->occlusionMap) &&
                (!material->metallicRoughnessMap ||
                 material->occlusionMap !=
                     material->metallicRoughnessMap)) {
                landscape.baseOcclusionPath =
                    material->occlusionMap->Path();
            }
            if (usable(material->normalMap)) {
                landscape.baseNormalPath = material->normalMap->Path();
            }
            landscape.baseCaptured = true;
        }

        LandscapeImage baseAlbedo;
        LandscapeImage baseOrm;
        LandscapeImage baseMetallic;
        LandscapeImage baseRoughness;
        LandscapeImage baseOcclusion;
        LandscapeImage baseNormal;
        LoadLandscapeImage(landscape.baseAlbedoPath, baseAlbedo);
        LoadLandscapeImage(landscape.baseOrmPath, baseOrm);
        LoadLandscapeImage(landscape.baseMetallicPath, baseMetallic);
        LoadLandscapeImage(landscape.baseRoughnessPath, baseRoughness);
        LoadLandscapeImage(landscape.baseOcclusionPath, baseOcclusion);
        LoadLandscapeImage(landscape.baseNormalPath, baseNormal);

        std::vector<LandscapeImage> layerAlbedo(landscape.layers.size());
        std::vector<LandscapeImage> layerOrm(landscape.layers.size());
        std::vector<LandscapeImage> layerNormal(landscape.layers.size());
        for (size_t i = 0; i < landscape.layers.size(); ++i) {
            const auto& layer = landscape.layers[i];
            if (layer.albedoMap) {
                LoadLandscapeImage(layer.albedoMap->Path(), layerAlbedo[i]);
            }
            if (layer.ormMap) {
                LoadLandscapeImage(layer.ormMap->Path(), layerOrm[i]);
            }
            if (layer.normalMap) {
                LoadLandscapeImage(layer.normalMap->Path(), layerNormal[i]);
            }
        }
        if (landscape.layerWeights.size() != landscape.layers.size()) {
            landscape.layerWeights.resize(landscape.layers.size());
        }
        for (auto& weights : landscape.layerWeights) {
            if (weights.size() !=
                static_cast<size_t>(landscape.sizeX) *
                    static_cast<size_t>(landscape.sizeZ)) {
                weights.resize(
                    static_cast<size_t>(landscape.sizeX) *
                        static_cast<size_t>(landscape.sizeZ), 0.0f);
            }
        }

        const int outputSize = std::clamp(
            landscape.bakedTextureSize, 256, 8192);
        LandscapeImage outputAlbedo;
        LandscapeImage outputOrm;
        LandscapeImage outputNormal;
        outputAlbedo.width = outputOrm.width = outputNormal.width =
            outputSize;
        outputAlbedo.height = outputOrm.height = outputNormal.height =
            outputSize;
        outputAlbedo.rgba.resize(
            static_cast<size_t>(outputSize) * outputSize * 4);
        outputOrm.rgba.resize(outputAlbedo.rgba.size());
        outputNormal.rgba.resize(outputAlbedo.rgba.size());

        const glm::vec4 white(1.0f);
        const glm::vec4 flatNormal(0.5f, 0.5f, 1.0f, 1.0f);
        // Fold the material tint into the baked output so unpainted base
        // areas keep their authored color after albedoFactor is reset.
        const glm::vec4 albedoTint = material->albedoFactor;
        const size_t expectedWeights =
            static_cast<size_t>(landscape.sizeX) * landscape.sizeZ;
        std::vector<float> weights(landscape.layers.size(), 0.0f);
        for (int y = 0; y < outputSize; ++y) {
            // The engine uploads source textures flipped (bottom-left V), so
            // write the baked rows in the same orientation.
            const float v = 1.0f -
                            (static_cast<float>(y) + 0.5f) /
                                static_cast<float>(outputSize);
            const float gz = v * static_cast<float>(landscape.sizeZ);
            for (int x = 0; x < outputSize; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) /
                                static_cast<float>(outputSize);
                const float gx = u * static_cast<float>(landscape.sizeX);
                float layerWeightSum = 0.0f;
                std::fill(weights.begin(), weights.end(), 0.0f);
                for (size_t i = 0; i < weights.size(); ++i) {
                    if (i >= landscape.layerWeights.size() ||
                        landscape.layerWeights[i].size() !=
                            expectedWeights) {
                        continue;
                    }
                    weights[i] = SampleLandscapeWeight(
                        landscape.layerWeights[i], landscape.sizeX,
                        landscape.sizeZ, gx, gz);
                    layerWeightSum += weights[i];
                }
                float baseWeight =
                    std::max(0.0f, 1.0f - layerWeightSum);
                float totalWeight = baseWeight + layerWeightSum;
                if (totalWeight < 1e-5f) {
                    baseWeight = 1.0f;
                    totalWeight = 1.0f;
                }
                const float invTotal = 1.0f / totalWeight;

                const glm::vec4 baseAlbedoColor =
                    SampleLandscapeImage(baseAlbedo, u, v, white) *
                    albedoTint;
                glm::vec4 baseOrmColor =
                    SampleLandscapeImage(baseOrm, u, v, white);
                if (baseMetallic.Valid()) {
                    baseOrmColor.b = SampleLandscapeImage(
                        baseMetallic, u, v, white).r;
                }
                if (baseRoughness.Valid()) {
                    baseOrmColor.g = SampleLandscapeImage(
                        baseRoughness, u, v, white).r;
                }
                if (baseOcclusion.Valid()) {
                    baseOrmColor.r = SampleLandscapeImage(
                        baseOcclusion, u, v, white).r;
                }
                const glm::vec4 baseNormalColor =
                    SampleLandscapeImage(baseNormal, u, v, flatNormal);
                glm::vec4 albedo = baseAlbedoColor * baseWeight;
                glm::vec4 orm = baseOrmColor * baseWeight;
                glm::vec3 normal =
                    glm::vec3(baseNormalColor * 2.0f - 1.0f) *
                    baseWeight;
                for (size_t i = 0; i < weights.size(); ++i) {
                    if (weights[i] <= 1e-6f) continue;
                    const glm::vec4 layerAlbedoColor =
                        SampleLandscapeImage(
                            layerAlbedo[i], u, v, baseAlbedoColor);
                    const glm::vec4 layerOrmColor =
                        SampleLandscapeImage(
                            layerOrm[i], u, v, baseOrmColor);
                    const glm::vec4 layerNormalColor =
                        SampleLandscapeImage(
                            layerNormal[i], u, v, baseNormalColor);
                    albedo += layerAlbedoColor * weights[i];
                    orm += layerOrmColor * weights[i];
                    normal += glm::vec3(layerNormalColor * 2.0f - 1.0f) *
                              weights[i];
                }
                albedo *= invTotal;
                orm *= invTotal;
                normal *= invTotal;
                const float normalLength = glm::length(normal);
                normal = normalLength > 1e-5f
                    ? normal / normalLength
                    : glm::vec3(0.0f, 0.0f, 1.0f);
                const glm::vec4 encodedNormal =
                    glm::vec4(normal * 0.5f + 0.5f, 1.0f);
                const size_t index =
                    (static_cast<size_t>(y) * outputSize + x) * 4;
                const auto writeColor =
                    [&](LandscapeImage& target, const glm::vec4& color) {
                        target.rgba[index + 0] = static_cast<uint8_t>(
                            glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
                        target.rgba[index + 1] = static_cast<uint8_t>(
                            glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
                        target.rgba[index + 2] = static_cast<uint8_t>(
                            glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
                        target.rgba[index + 3] = static_cast<uint8_t>(
                            glm::clamp(color.a, 0.0f, 1.0f) * 255.0f);
                    };
                writeColor(outputAlbedo,
                           glm::vec4(albedo.r, albedo.g, albedo.b, 1.0f));
                writeColor(outputOrm,
                           glm::vec4(orm.r, orm.g, orm.b, 1.0f));
                writeColor(outputNormal, encodedNormal);
            }
        }

        std::filesystem::path texturesDir =
            std::filesystem::path(Galeshi::ProjectManager::GetProjectPath()) /
            "assets/textures";
        if (Galeshi::ProjectManager::GetProjectPath().empty()) {
            texturesDir =
                std::filesystem::current_path() / "assets/textures";
        }
        std::error_code ec;
        std::filesystem::create_directories(texturesDir, ec);
        const std::string entityName =
            entity.HasComponent<Galeshi::NameComponent>()
                ? entity.GetComponent<Galeshi::NameComponent>().name
                : std::string("Landscape");
        std::string baseName;
        std::string existingBaked =
            !landscape.bakedAlbedoPath.empty()
                ? landscape.bakedAlbedoPath
                : landscape.bakedOrmPath;
        // A duplicated landscape copies its baked paths. Do not let both
        // entities write the same files; give the copy its own suffix.
        bool bakedPathsShared = false;
        if (!existingBaked.empty()) {
            for (auto other :
                 registry.view<Galeshi::LandscapeComponent>()) {
                if (other == entity.Handle()) continue;
                const auto& otherLandscape =
                    registry.get<Galeshi::LandscapeComponent>(other);
                if ((!otherLandscape.bakedAlbedoPath.empty() &&
                     otherLandscape.bakedAlbedoPath ==
                         landscape.bakedAlbedoPath) ||
                    (!otherLandscape.bakedOrmPath.empty() &&
                     otherLandscape.bakedOrmPath ==
                         landscape.bakedOrmPath) ||
                    (!otherLandscape.bakedNormalPath.empty() &&
                     otherLandscape.bakedNormalPath ==
                         landscape.bakedNormalPath)) {
                    bakedPathsShared = true;
                    break;
                }
            }
        }
        if (!existingBaked.empty() && !bakedPathsShared) {
            std::string stem =
                std::filesystem::path(existingBaked).stem().string();
            for (const std::string& suffix :
                 {std::string("_albedo"), std::string("_orm"),
                  std::string("_normal")}) {
                if (stem.size() > suffix.size() &&
                    stem.compare(stem.size() - suffix.size(),
                                 suffix.size(), suffix) == 0) {
                    stem.resize(stem.size() - suffix.size());
                    break;
                }
            }
            baseName = SafeLandscapeTextureName(stem);
        }
        if (baseName.empty()) {
            std::string scenePrefix;
            if (!m_currentScenePath.empty()) {
                scenePrefix = SafeLandscapeTextureName(
                    std::filesystem::path(m_currentScenePath)
                        .stem()
                        .string()) +
                    "_";
            }
            baseName =
                SafeLandscapeTextureName(scenePrefix + entityName);
            std::error_code existsEc;
            bool nameTaken =
                std::filesystem::exists(
                    texturesDir / (baseName + "_albedo.png"), existsEc) ||
                std::filesystem::exists(
                    texturesDir / (baseName + "_orm.png"), existsEc) ||
                std::filesystem::exists(
                    texturesDir / (baseName + "_normal.png"), existsEc);
            if (bakedPathsShared || nameTaken) {
                baseName += "_" +
                    std::to_string(entt::to_integral(entity.Handle()));
            }
        }
        const std::filesystem::path albedoPath =
            texturesDir / (baseName + "_albedo.png");
        const std::filesystem::path ormPath =
            texturesDir / (baseName + "_orm.png");
        const std::filesystem::path normalPath =
            texturesDir / (baseName + "_normal.png");
        if (!SaveLandscapePng(albedoPath, outputAlbedo) ||
            !SaveLandscapePng(ormPath, outputOrm) ||
            !SaveLandscapePng(normalPath, outputNormal)) {
            spdlog::error("[Landscape] Failed to write baked textures to {}",
                          texturesDir.string());
            return false;
        }
        auto loadGenerated = [](const std::filesystem::path& path,
                                bool srgb) {
            return std::shared_ptr<Galeshi::Texture>(
                Galeshi::Texture::LoadFromFile(path.string(), srgb, true));
        };
        auto generatedAlbedo = loadGenerated(albedoPath, true);
        auto generatedOrm = loadGenerated(ormPath, false);
        auto generatedNormal = loadGenerated(normalPath, false);
        if (!generatedAlbedo || !generatedOrm || !generatedNormal) {
            spdlog::error(
                "[Landscape] Baked files were written but could not be "
                "loaded from {}",
                texturesDir.string());
            return false;
        }
        material->albedoMap = std::move(generatedAlbedo);
        material->metallicRoughnessMap = std::move(generatedOrm);
        material->normalMap = std::move(generatedNormal);
        material->occlusionMap = material->metallicRoughnessMap;
        // The generated map already contains the blended base/layer colors.
        // A leftover flat albedo factor (the default Landscape is green)
        // would multiply and tint the baked result.
        material->albedoFactor = glm::vec4(1.0f);
        // The generated ORM map owns metallic/roughness now; leaving the
        // original split maps bound would make the shader ignore it.
        material->metallicMap.reset();
        material->roughnessMap.reset();
        material->albedoSrgb = true;
        material->metallicRoughnessSrgb = false;
        material->normalSrgb = false;
        material->occlusionSrgb = false;
        material->landscapeBlend = false;
        material->landscapeAlbedoMap.reset();
        material->landscapeOrmMap.reset();
        material->landscapeNormalMap.reset();
        material->sceneOverride = true;
        landscape.bakedAlbedoPath = albedoPath.string();
        landscape.bakedOrmPath = ormPath.string();
        landscape.bakedNormalPath = normalPath.string();
        spdlog::info("[Landscape] Baked layer textures to {}",
                     texturesDir.string());
        return true;
    }

