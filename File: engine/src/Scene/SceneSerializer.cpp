namespace Galeshi {
    std::string SceneSerializer::SaveToString() const {
        // Serialize PBR-specific properties
        return json::to_string(m_scene);
    }

    bool SceneSerializer::Save(const std::string& path) const {
        std::ofstream out(path); if (!out.is_open()) return false;
        out << SaveToString(); return true;
    }
}
