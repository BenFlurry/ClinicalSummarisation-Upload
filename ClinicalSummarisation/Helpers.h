#pragma once
#include <filesystem>

class Helpers {
public:
	static std::filesystem::path GetModelPath(std::string folderName);

	// Writable path for user-added guideline data (PDFs, SQLite, HNSW index).
	// Persists across app restarts. Uses ApplicationData LocalFolder.
	static std::filesystem::path GetGuidelinesDataPath();

	// override base path for unit testing (bypasses Package::Current())
	static void SetBasePathOverride(const std::filesystem::path& path);
	static void ClearBasePathOverride();

private:
	static std::filesystem::path s_basePathOverride;
};


