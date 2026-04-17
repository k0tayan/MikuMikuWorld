#pragma once
#include <string>

namespace MikuMikuWorld
{
	// Streams an AP video (e.g. res/overlay/ap.mp4) by extracting it to a
	// disposable PNG sequence via ffmpeg at first use, then uploading one frame
	// at a time into a reusable GL texture as playback time advances.
	class VideoPlayer
	{
	public:
		VideoPlayer();
		~VideoPlayer();

		VideoPlayer(const VideoPlayer&) = delete;
		VideoPlayer& operator=(const VideoPlayer&) = delete;

		// Prepares the frame cache (if missing) and opens the sequence.
		// `cacheDir` persists between runs, keyed by the source mp4 mtime.
		bool open(const std::string& mp4Path, const std::string& cacheDir,
		          int targetFps = 30, int scaleWidth = 960, int scaleHeight = 540);

		bool isOpen() const { return frameCount > 0; }

		// Advance/rewind the playhead. Uploads the corresponding frame if
		// different from the one currently on the GPU.
		void setTime(float seconds);

		float getDuration() const { return frameCount > 0 ? (float)frameCount / targetFps : 0.f; }
		int getWidth() const { return width; }
		int getHeight() const { return height; }
		unsigned int getGLTexture() const { return glTexture; }

	private:
		bool ensureCache(const std::string& mp4Path);
		void uploadFrame(int idx);

		std::string cacheDir;
		int targetFps{ 30 };
		int scaleW{ 960 };
		int scaleH{ 540 };
		int frameCount{ 0 };
		int currentFrame{ -1 };
		int width{ 0 };
		int height{ 0 };
		unsigned int glTexture{ 0 };
	};
}
