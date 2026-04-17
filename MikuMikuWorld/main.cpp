#include "Application.h"
#include "IO.h"
#include "OfflineRenderer.h"
#include "UI.h"
#include <GLFW/glfw3.h>
#include <mach-o/dyld.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace mmw = MikuMikuWorld;
mmw::Application app;

static std::string getExecutableDir()
{
	char buf[4096];
	uint32_t size = sizeof(buf);
	if (_NSGetExecutablePath(buf, &size) != 0)
		return {};

	// Resolve symlinks (argv0 may be a symlink into the bundle)
	std::error_code ec;
	std::filesystem::path exePath = std::filesystem::canonical(buf, ec);
	if (ec)
		exePath = buf;

	// Inside a .app bundle the executable lives at Contents/MacOS/<exe>.
	// The companion Resources/ directory is one level up.
	std::filesystem::path contentsDir = exePath.parent_path().parent_path();
	std::filesystem::path resourcesDir = contentsDir / "Resources";
	if (std::filesystem::exists(resourcesDir))
		return resourcesDir.string() + "/";

	return exePath.parent_path().string() + "/";
}

static std::string getUserDataDir()
{
	const char* home = std::getenv("HOME");
	if (!home || !*home)
		return {};

	std::filesystem::path dir = std::filesystem::path(home) / "Library" / "Application Support" / "MikuMikuWorld";
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	return dir.string() + "/";
}

static void dropCallback(GLFWwindow*, int count, const char** paths)
{
	for (int i = 0; i < count; ++i)
		app.appendOpenFile(paths[i]);
}

int main(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--render") == 0)
			return mmw::runOfflineRender(argc, argv, getExecutableDir(), getUserDataDir());
	}

	try
	{
		mmw::Result result = app.initialize(getExecutableDir(), getUserDataDir());

		if (!result.isOk())
			throw std::runtime_error(result.getMessage().c_str());

		for (int i = 1; i < argc; ++i)
			app.appendOpenFile(argv[i]);

		if (auto* window = app.getGlfwWindow())
			glfwSetDropCallback(window, dropCallback);

		app.handlePendingOpenFiles();
		app.run();
	}
	catch (const std::exception& ex)
	{
		IO::MessageBoxButtons actions = IO::MessageBoxButtons::Ok;
		std::string msg = "An unhandled exception has occurred and the application will now close.";
		if (!app.isEditorUpToDate())
		{
			msg.append("\nDo you want to save the current score?");
			actions = IO::MessageBoxButtons::YesNo;
		}

		msg
			.append("\n\nError: ")
			.append(ex.what())
			.append("\nApplication Version: ")
			.append(mmw::Application::getAppVersion());

		IO::MessageBoxResult result = IO::messageBox(APP_NAME, msg, actions, IO::MessageBoxIcon::Error);
		if (!app.isEditorUpToDate() && result == IO::MessageBoxResult::Yes && app.attemptSave())
		{
			IO::messageBox(APP_NAME, "Save successful", IO::MessageBoxButtons::Ok, IO::MessageBoxIcon::Information);
		}

		app.writeSettings();
	}

	app.dispose();
	return 0;
}
