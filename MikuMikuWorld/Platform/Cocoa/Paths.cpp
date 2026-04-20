#include "../Paths.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <mach-o/dyld.h>
#include <string>
#include <system_error>

namespace MikuMikuWorld::Platform
{
	std::string getResourceDir()
	{
		char buf[4096];
		uint32_t size = sizeof(buf);
		if (_NSGetExecutablePath(buf, &size) != 0)
			return {};

		std::error_code ec;
		std::filesystem::path exePath = std::filesystem::canonical(buf, ec);
		if (ec)
			exePath = buf;

		// Inside a .app bundle the executable lives at Contents/MacOS/<exe>;
		// Resources/ is a sibling of MacOS/ inside Contents/.
		std::filesystem::path contentsDir = exePath.parent_path().parent_path();
		std::filesystem::path resourcesDir = contentsDir / "Resources";
		if (std::filesystem::exists(resourcesDir))
			return resourcesDir.string() + "/";

		// Fallback: CLI binary sitting next to res/ directly.
		std::filesystem::path sibling = exePath.parent_path() / "res";
		if (std::filesystem::exists(sibling))
			return exePath.parent_path().string() + "/";

		return exePath.parent_path().string() + "/";
	}

	std::string getUserDataDir()
	{
		const char* home = std::getenv("HOME");
		if (!home || !*home)
			return {};

		std::filesystem::path dir = std::filesystem::path(home)
			/ "Library" / "Application Support" / "MikuMikuWorld";
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		return dir.string() + "/";
	}
}
