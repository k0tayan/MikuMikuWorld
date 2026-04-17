#pragma once
#include <string>

namespace MikuMikuWorld
{
	// Headless command-line entry point that renders a score to a video via ffmpeg.
	// Returns a process exit code (0 on success).
	int runOfflineRender(int argc, char** argv,
	                     const std::string& resourceDir,
	                     const std::string& userDataDir);
}
