#include "OfflineRenderer.h"
#include "Platform/Paths.h"

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv)
{
	bool renderRequested = false;
	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--render") == 0)
		{
			renderRequested = true;
			break;
		}
	}

	if (!renderRequested)
	{
		std::fprintf(stderr,
			"mmw-render: headless offline renderer for MikuMikuWorld.\n"
			"Run with --render --score <path> --out <path.mp4>.\n"
			"Use --render --help to see the full option list.\n");
		return 2;
	}

	const std::string resourceDir = MikuMikuWorld::Platform::getResourceDir();
	const std::string userDataDir = MikuMikuWorld::Platform::getUserDataDir();
	return MikuMikuWorld::runOfflineRender(argc, argv, resourceDir, userDataDir);
}
