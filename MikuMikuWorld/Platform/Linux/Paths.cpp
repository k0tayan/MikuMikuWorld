#include "../Paths.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>

namespace MikuMikuWorld::Platform
{
	namespace
	{
		std::filesystem::path readSelfExe()
		{
			std::error_code ec;
			auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
			if (!ec) return p;
			return {};
		}
	}

	std::string getResourceDir()
	{
		std::filesystem::path exe = readSelfExe();
		if (exe.empty())
			return {};

		// Layout: <prefix>/bin/mmw-render with resources at <prefix>/share/MikuMikuWorld/
		// Also accept a portable layout where res/ sits next to the binary.
		std::filesystem::path binDir = exe.parent_path();
		std::filesystem::path portable = binDir / "res";
		if (std::filesystem::exists(portable))
			return binDir.string() + "/";

		std::filesystem::path shared = binDir.parent_path() / "share" / "MikuMikuWorld";
		if (std::filesystem::exists(shared))
			return shared.string() + "/";

		return binDir.string() + "/";
	}

	std::string getUserDataDir()
	{
		std::filesystem::path base;

		const char* xdg = std::getenv("XDG_DATA_HOME");
		if (xdg && *xdg)
		{
			base = xdg;
		}
		else
		{
			const char* home = std::getenv("HOME");
			if (!home || !*home)
				return {};
			base = std::filesystem::path(home) / ".local" / "share";
		}

		std::filesystem::path dir = base / "MikuMikuWorld";
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		return dir.string() + "/";
	}
}
