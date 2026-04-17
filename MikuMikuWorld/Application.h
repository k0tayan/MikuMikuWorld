#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "ScoreEditor.h"
#include "ImGuiManager.h"

namespace MikuMikuWorld
{
	class Result;

	struct WindowState
	{
		bool resetting = false;
		bool vsync = true;
		bool showPerformanceMetrics = false;
		bool maximized = false;
		bool closing = false;
		bool shouldPickScore = false;
		bool dragDropHandled = true;
		bool fullScreen = false;
		Vector2 position{};
		Vector2 size{};
	};

	class Application
	{
	private:
		GLFWwindow* window{ nullptr };
		std::unique_ptr<ScoreEditor> editor;
		std::unique_ptr<ImGuiManager> imgui;
		UnsavedChangesDialog unsavedChangesDialog;

		bool initialized{ false };
		bool shouldPickScore{ false };
		std::string language;

		std::vector<std::string> pendingOpenFiles;

		static std::string version;
		static std::string appDir;
		static std::string userDataDir;

		Result initOpenGL();

	public:
		static WindowState windowState;
		static std::string pendingLoadScoreFile;

		Application();

		Result initialize(const std::string& root, const std::string& userData);
		void run();
		void update();
		void appendOpenFile(const std::string& filename);
		void handlePendingOpenFiles();
		void readSettings();
		void writeSettings();
		static void setPaths(const std::string& root, const std::string& userData);
		static void loadResources();
		void dispose();
		void setFullScreen(bool fullScreen);
		bool attemptSave();
		bool isEditorUpToDate() const;

		GLFWwindow* getGlfwWindow() { return window; }

		static const std::string& getAppDir();
		static const std::string& getUserDataDir();
		static const std::string& getAppVersion();
	};
}
