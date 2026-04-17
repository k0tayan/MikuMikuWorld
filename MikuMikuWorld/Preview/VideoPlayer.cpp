#include "VideoPlayer.h"
#include "../IO.h"
#include "../File.h"

#include <glad/glad.h>
#include <stb_image.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <filesystem>

namespace MikuMikuWorld
{
	namespace
	{
		std::string shellQuote(const std::string& s)
		{
			std::string out = "'";
			for (char c : s)
			{
				if (c == '\'') out += "'\\''";
				else out += c;
			}
			out += "'";
			return out;
		}

		bool fileNewerThan(const std::string& a, const std::string& b)
		{
			struct stat sa{}, sb{};
			if (::stat(a.c_str(), &sa) != 0) return false;
			if (::stat(b.c_str(), &sb) != 0) return true;
			return sa.st_mtime > sb.st_mtime;
		}

		std::string framePath(const std::string& dir, int idx)
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%04d.png", idx + 1);
			return dir + "/" + buf;
		}
	}

	VideoPlayer::VideoPlayer() = default;

	VideoPlayer::~VideoPlayer()
	{
		if (glTexture)
		{
			glDeleteTextures(1, &glTexture);
			glTexture = 0;
		}
	}

	bool VideoPlayer::open(const std::string& mp4Path, const std::string& cacheDirArg,
	                       int targetFpsArg, int scaleWidthArg, int scaleHeightArg)
	{
		if (!IO::File::exists(mp4Path)) return false;

		cacheDir = cacheDirArg;
		targetFps = targetFpsArg;
		scaleW = scaleWidthArg;
		scaleH = scaleHeightArg;

		if (!ensureCache(mp4Path))
		{
			fprintf(stderr, "VideoPlayer: failed to prepare frame cache for %s\n", mp4Path.c_str());
			return false;
		}

		// Count extracted frames
		frameCount = 0;
		while (IO::File::exists(framePath(cacheDir, frameCount)))
			++frameCount;

		if (frameCount == 0) return false;

		// Create a reusable GPU texture at the scaled dimensions.
		if (!glTexture)
		{
			glGenTextures(1, &glTexture);
			glBindTexture(GL_TEXTURE_2D, glTexture);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, scaleW, scaleH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glBindTexture(GL_TEXTURE_2D, 0);
		}

		width = scaleW;
		height = scaleH;
		currentFrame = -1;
		uploadFrame(0);
		return true;
	}

	bool VideoPlayer::ensureCache(const std::string& mp4Path)
	{
		std::error_code ec;
		std::filesystem::create_directories(cacheDir, ec);
		if (ec) return false;

		const std::string first = framePath(cacheDir, 0);
		if (IO::File::exists(first) && !fileNewerThan(mp4Path, first))
			return true;

		// Clean stale frames
		for (auto& entry : std::filesystem::directory_iterator(cacheDir, ec))
		{
			if (entry.path().extension() == ".png")
				std::filesystem::remove(entry.path(), ec);
		}

		char filter[128];
		std::snprintf(filter, sizeof(filter),
		              "fps=%d,scale=%d:%d:flags=bicubic",
		              targetFps, scaleW, scaleH);

		std::string cmd = "ffmpeg -hide_banner -loglevel error -y ";
		cmd += "-i " + shellQuote(mp4Path);
		cmd += " -vf " + shellQuote(filter);
		cmd += " " + shellQuote(cacheDir + "/%04d.png");

		int rc = std::system(cmd.c_str());
		return rc == 0;
	}

	void VideoPlayer::uploadFrame(int idx)
	{
		if (idx == currentFrame || idx < 0 || idx >= frameCount) return;

		const std::string p = framePath(cacheDir, idx);
		int w = 0, h = 0, comp = 0;
		unsigned char* data = stbi_load(p.c_str(), &w, &h, &comp, 4);
		if (!data) return;

		glBindTexture(GL_TEXTURE_2D, glTexture);
		if (w == scaleW && h == scaleH)
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
		else
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		glBindTexture(GL_TEXTURE_2D, 0);

		stbi_image_free(data);
		currentFrame = idx;
		width = w;
		height = h;
	}

	void VideoPlayer::setTime(float seconds)
	{
		if (frameCount <= 0) return;
		int idx = (int)(seconds * targetFps);
		if (idx < 0) idx = 0;
		if (idx >= frameCount) idx = frameCount - 1;
		uploadFrame(idx);
	}
}
