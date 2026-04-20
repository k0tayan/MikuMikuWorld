#include "../HeadlessGL.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>

namespace MikuMikuWorld::Platform
{
	namespace
	{
		GLFWwindow* g_window = nullptr;
	}

	bool initHeadlessGL(int width, int height)
	{
		if (!glfwInit())
		{
			std::fprintf(stderr, "glfwInit failed\n");
			return false;
		}

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

		g_window = glfwCreateWindow(width, height, "mmw-render", nullptr, nullptr);
		if (!g_window)
		{
			std::fprintf(stderr, "glfwCreateWindow failed\n");
			glfwTerminate();
			return false;
		}
		glfwMakeContextCurrent(g_window);

		if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
		{
			std::fprintf(stderr, "gladLoadGLLoader failed\n");
			glfwDestroyWindow(g_window);
			g_window = nullptr;
			glfwTerminate();
			return false;
		}

		return true;
	}

	void shutdownHeadlessGL()
	{
		if (g_window)
		{
			glfwDestroyWindow(g_window);
			g_window = nullptr;
		}
		glfwTerminate();
	}
}
