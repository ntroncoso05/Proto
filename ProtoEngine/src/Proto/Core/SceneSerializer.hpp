#pragma once
#include <string>

namespace Proto
{
    class SceneSerializer
    {
    public:
        static bool Serialize(const std::string& filepath);
        static bool Deserialize(const std::string& filepath);
        static const std::string& GetActiveScenePath() { return s_ActiveScenePath; }

    private:
        static inline std::string s_ActiveScenePath = "../../../assets/scenes/MainScene.json";
    };
}