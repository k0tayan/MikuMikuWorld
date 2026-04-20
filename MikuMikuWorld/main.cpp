#include "Application.h"
#include "IO.h"
#include "OfflineRenderer.h"
#include "Platform/Paths.h"
#include "UI.h"
#include <GLFW/glfw3.h>
#include <cstring>

namespace mmw = MikuMikuWorld;
mmw::Application app;

static void dropCallback(GLFWwindow*, int count, const char** paths)
{
	for (int i = 0; i < count; ++i)
		app.appendOpenFile(paths[i]);
}

int main(int argc, char** argv)
{
	const std::string resourceDir = mmw::Platform::getResourceDir();
	const std::string userDataDir = mmw::Platform::getUserDataDir();

	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--render") == 0)
			return mmw::runOfflineRender(argc, argv, resourceDir, userDataDir);
	}

	try
	{
		mmw::Result result = app.initialize(resourceDir, userDataDir);

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
