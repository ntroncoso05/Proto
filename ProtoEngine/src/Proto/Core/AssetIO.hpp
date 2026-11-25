#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>

#ifdef PROTO_PLATFORM_ANDROID
    #include <android/asset_manager.h>
    #include "Platform/Android/AndroidWindow.hpp"
#else
    #include <filesystem>
#endif

namespace Proto
{
    class AssetIO
    {
    public:
        // Normalizes "../../../assets/foo/bar" -> "foo/bar" for Android AAssetManager
        static std::string NormalizeAndroidAssetPath(const std::string& rawPath)
        {
            std::string p = rawPath;
            while (p.rfind("../", 0) == 0) {
                p.erase(0, 3);
            }
            if (p.rfind("assets/", 0) == 0) {
                p.erase(0, 7);
            }
            return p;
        }

        static std::vector<char> ReadBinaryFile(const std::string& filepath)
        {
#ifdef PROTO_PLATFORM_ANDROID
            auto* app = AndroidWindow::GetAndroidApp();
            if (!app || !app->activity || !app->activity->assetManager) {
                throw std::runtime_error("Android AAssetManager is null!");
            }

            std::string relPath = NormalizeAndroidAssetPath(filepath);
            AAsset* asset = AAssetManager_open(app->activity->assetManager, relPath.c_str(), AASSET_MODE_BUFFER);
            if (!asset) {
                return {};
            }

            size_t size = static_cast<size_t>(AAsset_getLength(asset));
            std::vector<char> buffer(size);
            if (size > 0) {
                AAsset_read(asset, buffer.data(), size);
            }
            AAsset_close(asset);
            return buffer;
#else
            std::ifstream file(filepath, std::ios::ate | std::ios::binary);
            if (!file.is_open()) {
                return {};
            }
            size_t fileSize = static_cast<size_t>(file.tellg());
            std::vector<char> buffer(fileSize);
            file.seekg(0);
            file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
            return buffer;
#endif
        }

        static std::string ReadTextFile(const std::string& filepath)
        {
            auto bytes = ReadBinaryFile(filepath);
            return std::string(bytes.begin(), bytes.end());
        }

        static bool FileExists(const std::string& filepath)
        {
#ifdef PROTO_PLATFORM_ANDROID
            auto* app = AndroidWindow::GetAndroidApp();
            if (!app || !app->activity || !app->activity->assetManager) return false;
            std::string relPath = NormalizeAndroidAssetPath(filepath);
            AAsset* asset = AAssetManager_open(app->activity->assetManager, relPath.c_str(), AASSET_MODE_UNKNOWN);
            if (asset) {
                AAsset_close(asset);
                return true;
            }
            return false;
#else
            return std::filesystem::exists(filepath);
#endif
        }
    };
}