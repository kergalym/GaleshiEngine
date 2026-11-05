#include "EditorCommon.hpp"

std::shared_ptr<EditorLogSink> g_editorLogSink;

const char* LoadingStageText(LoadingState::Stage stage) {
    switch (stage) {
        case LoadingState::Stage::Preparing:  return "Preparing...";
        case LoadingState::Stage::Parsing:    return "Parsing scene...";
        case LoadingState::Stage::Loading:    return "Loading meshes & textures...";
        case LoadingState::Stage::Finalizing: return "Finalizing...";
        case LoadingState::Stage::Cancelling: return "Cancelling...";
        case LoadingState::Stage::Idle:       break;
    }
    return "";
}

std::string AssetExtensionLower(const std::string& path) {
    std::string extension =
        std::filesystem::path(path).extension().string();
    for (char& c : extension)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension;
}

bool IsModelAssetPath(const std::string& path) {
    const std::string extension = AssetExtensionLower(path);
    return extension == ".fbx" || extension == ".gltf" ||
           extension == ".glb" || extension == ".obj" ||
           extension == ".dae";
}

bool IsGltfModelAssetPath(const std::string& path) {
    const std::string extension = AssetExtensionLower(path);
    return extension == ".gltf" || extension == ".glb";
}

// FBX remains loadable for existing scenes, but asset drag & drop fields now
// accept the runtime-oriented glTF/GLB path (plus OBJ/DAE for static props).
// Keeping this separate from IsModelAssetPath lets legacy file pickers and
// scene loading continue to work while Drop Model fields stay FBX-free.
bool IsDragDropModelAssetPath(const std::string& path) {
    const std::string extension = AssetExtensionLower(path);
    return extension == ".gltf" || extension == ".glb" ||
           extension == ".obj" || extension == ".dae";
}

bool IsImageAssetPath(const std::string& path) {
    const std::string extension = AssetExtensionLower(path);
    return extension == ".png" || extension == ".jpg" ||
           extension == ".jpeg" || extension == ".bmp" ||
           extension == ".tga" || extension == ".dds" ||
           extension == ".ktx";
}

bool IsAudioAssetPath(const std::string& path) {
    const std::string extension = AssetExtensionLower(path);
    return extension == ".wav" || extension == ".mp3";
}

bool IsFontAssetPath(const std::string& path) {
    const std::string extension = AssetExtensionLower(path);
    return extension == ".ttf" || extension == ".otf";
}

bool IsCppCodeAssetPath(const std::string& path) {
    const std::string extension = AssetExtensionLower(path);
    return extension == ".cpp" || extension == ".cc" ||
           extension == ".cxx" || extension == ".hpp" ||
           extension == ".h";
}

bool IsShaderAssetPath(const std::string& path) {
    const std::string extension = AssetExtensionLower(path);
    return extension == ".glsl" || extension == ".hlsl";
}

bool IsVertexShaderSource(const std::string& source) {
    return source.find("gl_Position") != std::string::npos &&
           source.find("gl_FragColor") == std::string::npos;
}

bool IsCodeAssetPath(const std::string& path) {
    return AssetExtensionLower(path) == ".lua" || IsCppCodeAssetPath(path) ||
           IsShaderAssetPath(path);
}
