#include "pch.h"
#include "Helpers.h"
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Storage.h>
#include <filesystem>

std::filesystem::path Helpers::s_basePathOverride;

void Helpers::SetBasePathOverride(const std::filesystem::path& path) { s_basePathOverride = path; }
void Helpers::ClearBasePathOverride() { s_basePathOverride.clear(); }

// provides path to runtime location, then finds the model folder at said location
std::filesystem::path Helpers::GetModelPath(std::string folderName) {
    if (!s_basePathOverride.empty()) {
        return s_basePathOverride / folderName;
    }
    winrt::Windows::Storage::StorageFolder packageFolder = winrt::Windows::ApplicationModel::Package::Current().InstalledLocation();
    std::filesystem::path packagePath = packageFolder.Path().c_str();
    std::filesystem::path fullModelPath = packagePath / folderName;
    return fullModelPath;
}

// similar to above but for guidelines creating a directory to store them in
std::filesystem::path Helpers::GetGuidelinesDataPath() {
    if (!s_basePathOverride.empty()) {
        auto p = s_basePathOverride / "guidelines_data";
        std::filesystem::create_directories(p);
        return p;
    }
    auto localFolder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder();
    std::filesystem::path localPath = localFolder.Path().c_str();
    std::filesystem::path dataPath = localPath / "guidelines_data";
    std::filesystem::create_directories(dataPath);
    return dataPath;
}

