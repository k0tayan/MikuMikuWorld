#include "OfflineRenderer.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Application.h"
#include "ApplicationConfiguration.h"
#include "Audio/Sound.h"
#include "Constants.h"
#include "NativeScoreSerializer.h"
#include "Note.h"
#include "NoteSkin.h"
#include "PreviewData.h"
#include "Rendering/Renderer.h"
#include "Score.h"
#include "ScoreContext.h"
#include "ScorePreview.h"
#include "ScoreSerializer.h"
#include "SonolusSerializer.h"
#include "SusSerializer.h"
#include "Tempo.h"
#include "IO.h"

namespace MikuMikuWorld
{
	namespace
	{
		struct RenderOptions
		{
			std::string scorePath;
			std::string audioPath;
			std::string outputPath;
			std::string ffmpegPath = "ffmpeg";

			int fps = 60;
			int width = 1920;
			int height = 1080;
			float tailSeconds = 2.0f;

			bool hasNoteSpeed = false;  float noteSpeed = 0;
			bool hasStageCover = false; float stageCover = 0;
			bool hasBgBrightness = false; float bgBrightness = 0;
			bool drawBackground = true;
			bool hasDrawBackground = false;
			std::string backgroundImage;
			bool hasBackgroundImage = false;
			bool hasNotesSkin = false; int notesSkin = 0;
			bool hasEffectsProfile = false; int effectsProfile = 0;
			bool hasMirror = false; bool mirror = false;
			bool hasAudioOffset = false; float audioOffset = 0;

			bool hasSeProfile = false; int seProfile = 0;
			bool hasSeVolume = false; float seVolume = 1.0f;
			bool disableSE = false;
		};

		void printUsage()
		{
			std::fprintf(stderr,
				"Usage: MikuMikuWorld --render \\\n"
				"  --score <path>       Score file (.mmws/.sus/.json/.json.gz)\n"
				"  --out <path.mp4>     Output video path\n"
				"  [--audio <path>]     Audio file muxed as audio track\n"
				"  [--fps <N>]          Frames per second (default 60)\n"
				"  [--width <W>]        Video width  (default 1920)\n"
				"  [--height <H>]       Video height (default 1080)\n"
				"  [--tail <sec>]       Extra tail after the last note (default 2.0)\n"
				"  [--ffmpeg <path>]    ffmpeg binary path (default: ffmpeg in PATH)\n"
				"  [--note-speed <N>]   Override pvNoteSpeed (1..12)\n"
				"  [--stage-cover <N>]  Override pvStageCover\n"
				"  [--bg-brightness <N>] Override pvBackgroundBrightness\n"
				"  [--no-background]    Disable background\n"
				"  [--background <path>] Override background image\n"
				"  [--notes-skin <N>]   Override notesSkin (0 or 1)\n"
				"  [--effects-profile <N>] Override pvEffectsProfile\n"
				"  [--mirror]           Mirror score horizontally\n"
				"  [--audio-offset <sec>] Shift audio relative to chart (seconds)\n"
				"  [--se-profile <N>]   SE profile (0 or 1, default: app_config)\n"
				"  [--se-volume <X>]    SE volume multiplier (default 1.0)\n"
				"  [--no-se]            Disable note sound effects\n");
		}

		bool parseArgs(int argc, char** argv, RenderOptions& opt)
		{
			auto needs = [&](int i, const char* name) {
				if (i + 1 >= argc) {
					std::fprintf(stderr, "Missing value for %s\n", name);
					return false;
				}
				return true;
			};

			for (int i = 1; i < argc; ++i)
			{
				std::string a = argv[i];
				if (a == "--render") continue;
				if (a == "--help" || a == "-h") { printUsage(); std::exit(0); }
				else if (a == "--score") { if (!needs(i, "--score")) return false; opt.scorePath = argv[++i]; }
				else if (a == "--audio") { if (!needs(i, "--audio")) return false; opt.audioPath = argv[++i]; }
				else if (a == "--out") { if (!needs(i, "--out")) return false; opt.outputPath = argv[++i]; }
				else if (a == "--fps") { if (!needs(i, "--fps")) return false; opt.fps = std::atoi(argv[++i]); }
				else if (a == "--width") { if (!needs(i, "--width")) return false; opt.width = std::atoi(argv[++i]); }
				else if (a == "--height") { if (!needs(i, "--height")) return false; opt.height = std::atoi(argv[++i]); }
				else if (a == "--tail") { if (!needs(i, "--tail")) return false; opt.tailSeconds = std::atof(argv[++i]); }
				else if (a == "--ffmpeg") { if (!needs(i, "--ffmpeg")) return false; opt.ffmpegPath = argv[++i]; }
				else if (a == "--note-speed") { if (!needs(i, "--note-speed")) return false; opt.hasNoteSpeed = true; opt.noteSpeed = std::atof(argv[++i]); }
				else if (a == "--stage-cover") { if (!needs(i, "--stage-cover")) return false; opt.hasStageCover = true; opt.stageCover = std::atof(argv[++i]); }
				else if (a == "--bg-brightness") { if (!needs(i, "--bg-brightness")) return false; opt.hasBgBrightness = true; opt.bgBrightness = std::atof(argv[++i]); }
				else if (a == "--no-background") { opt.hasDrawBackground = true; opt.drawBackground = false; }
				else if (a == "--background") { if (!needs(i, "--background")) return false; opt.hasBackgroundImage = true; opt.backgroundImage = argv[++i]; }
				else if (a == "--notes-skin") { if (!needs(i, "--notes-skin")) return false; opt.hasNotesSkin = true; opt.notesSkin = std::atoi(argv[++i]); }
				else if (a == "--effects-profile") { if (!needs(i, "--effects-profile")) return false; opt.hasEffectsProfile = true; opt.effectsProfile = std::atoi(argv[++i]); }
				else if (a == "--mirror") { opt.hasMirror = true; opt.mirror = true; }
				else if (a == "--audio-offset") { if (!needs(i, "--audio-offset")) return false; opt.hasAudioOffset = true; opt.audioOffset = std::atof(argv[++i]); }
				else if (a == "--se-profile") { if (!needs(i, "--se-profile")) return false; opt.hasSeProfile = true; opt.seProfile = std::atoi(argv[++i]); }
				else if (a == "--se-volume") { if (!needs(i, "--se-volume")) return false; opt.hasSeVolume = true; opt.seVolume = std::atof(argv[++i]); }
				else if (a == "--no-se") { opt.disableSE = true; }
				else {
					std::fprintf(stderr, "Unknown argument: %s\n", a.c_str());
					printUsage();
					return false;
				}
			}

			if (opt.scorePath.empty() || opt.outputPath.empty())
			{
				std::fprintf(stderr, "Both --score and --out are required.\n");
				printUsage();
				return false;
			}
			if (opt.fps <= 0 || opt.width <= 0 || opt.height <= 0)
			{
				std::fprintf(stderr, "--fps/--width/--height must be positive.\n");
				return false;
			}
			return true;
		}

		std::unique_ptr<ScoreSerializer> makeSerializer(const std::string& filename)
		{
			SerializeFormat fmt = ScoreSerializeController::toSerializeFormat(filename);
			switch (fmt)
			{
			case SerializeFormat::NativeFormat:
				return std::make_unique<NativeScoreSerializer>();
			case SerializeFormat::SusFormat:
				return std::make_unique<SusSerializer>();
			case SerializeFormat::LvlDataFormat:
				return std::make_unique<SonolusSerializer>(
					std::make_unique<PySekaiEngine>(),
					IO::endsWith(filename, GZ_JSON_EXTENSION));
			default:
				return nullptr;
			}
		}

		int scoreMaxTick(const Score& score)
		{
			int maxTick = 0;
			for (const auto& [id, note] : score.notes)
				maxTick = std::max(maxTick, note.tick);
			return maxTick;
		}

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

		// -----------------------------------------------------------------
		// Sound effects mixdown
		// -----------------------------------------------------------------

		constexpr int SE_MIX_SAMPLE_RATE = 44100;
		constexpr int SE_MIX_CHANNELS = 2;

		// AudioManager::loadSoundEffects mirrors this layout/order/values.
		constexpr std::array<float, 10> kSEVolumes = {
			0.75f, 0.75f, 0.90f, 0.80f, 0.70f,
			0.75f, 0.80f, 0.92f, 0.82f, 0.70f
		};

		float getSEVolume(std::string_view name)
		{
			for (size_t i = 0; i < arrayLength(SE_NAMES); ++i)
				if (name == SE_NAMES[i])
					return kSEVolumes[i];
			return 1.0f;
		}

		struct SEClip
		{
			std::vector<int16_t> samples;  // interleaved stereo s16
			size_t frames = 0;
			// Hold body loop region (frames). Only used for SE_CONNECT / SE_CRITICAL_CONNECT.
			size_t loopStart = 0;
			size_t loopEnd = 0;
			bool loopable = false;
		};

		bool loadSEClip(const std::string& path, float volumeScalar, bool loopable, SEClip& out)
		{
			Audio::SoundBuffer buf{};
			Result r = Audio::decodeAudioFile(path, buf);
			if (!r.isOk())
			{
				std::fprintf(stderr, "[se] decode failed: %s (%s)\n",
					path.c_str(), r.getMessage().c_str());
				return false;
			}
			if (buf.sampleRate != SE_MIX_SAMPLE_RATE)
			{
				std::fprintf(stderr,
					"[se] unexpected sample rate %u in %s (want %d)\n",
					buf.sampleRate, path.c_str(), SE_MIX_SAMPLE_RATE);
				buf.dispose();
				return false;
			}

			out.frames = static_cast<size_t>(buf.frameCount);
			out.samples.resize(out.frames * SE_MIX_CHANNELS);

			// Upmix mono->stereo, apply volume
			if (buf.channelCount == 1)
			{
				for (size_t i = 0; i < out.frames; ++i)
				{
					int32_t v = static_cast<int32_t>(buf.samples[i] * volumeScalar);
					v = std::clamp(v, -32768, 32767);
					out.samples[i * 2]     = static_cast<int16_t>(v);
					out.samples[i * 2 + 1] = static_cast<int16_t>(v);
				}
			}
			else if (buf.channelCount == 2)
			{
				for (size_t i = 0; i < out.frames * 2; ++i)
				{
					int32_t v = static_cast<int32_t>(buf.samples[i] * volumeScalar);
					out.samples[i] = static_cast<int16_t>(std::clamp(v, -32768, 32767));
				}
			}
			else
			{
				std::fprintf(stderr, "[se] unexpected channel count %u in %s\n",
					buf.channelCount, path.c_str());
				buf.dispose();
				return false;
			}

			out.loopable = loopable;
			if (loopable && out.frames > 6000)
			{
				out.loopStart = 3000;
				out.loopEnd = out.frames - 3000;
			}
			buf.dispose();
			return true;
		}

		struct SEOneShot { float time; std::string name; };
		struct SEHoldLoop { float startTime; float endTime; std::string name; };

		// Mirrors ScoreEditorTimeline::updateNoteSE for an entire playthrough.
		void collectSEEvents(const ScoreContext& context,
			std::vector<SEOneShot>& oneshots,
			std::vector<SEHoldLoop>& holdLoops)
		{
			const auto& notesList = context.scorePreviewDrawData.notesList.getView();
			for (const auto& dn : notesList)
			{
				auto it = context.score.notes.find(dn.refID);
				if (it == context.score.notes.end()) continue;
				const Note& note = it->second;

				bool playOneShot = true;
				if (note.getType() == NoteType::Hold)
				{
					auto hit = context.score.holdNotes.find(note.ID);
					playOneShot = (hit != context.score.holdNotes.end())
						&& hit->second.startType == HoldNoteType::Normal;
				}
				else if (note.getType() == NoteType::HoldEnd)
				{
					auto hit = context.score.holdNotes.find(note.parentID);
					playOneShot = (hit != context.score.holdNotes.end())
						&& hit->second.endType == HoldNoteType::Normal;
				}

				if (playOneShot)
				{
					std::string_view se = getNoteSE(note, context.score);
					if (!se.empty())
						oneshots.push_back({ dn.time, std::string(se) });
				}

				if (note.getType() == NoteType::Hold)
				{
					auto hit = context.score.holdNotes.find(note.ID);
					if (hit != context.score.holdNotes.end() && !hit->second.isGuide())
					{
						int endTick = context.score.notes.at(hit->second.end).tick;
						float endTime = accumulateDuration(endTick, TICKS_PER_BEAT, context.score.tempoChanges);
						const char* loopSE = note.critical ? SE_CRITICAL_CONNECT : SE_CONNECT;
						holdLoops.push_back({ dn.time, endTime, std::string(loopSE) });
					}
				}
			}
		}

		inline void mixSample(int16_t& dst, int16_t src)
		{
			int32_t v = static_cast<int32_t>(dst) + static_cast<int32_t>(src);
			dst = static_cast<int16_t>(std::clamp(v, -32768, 32767));
		}

		void mixClip(std::vector<int16_t>& out, size_t outFrames,
			const SEClip& clip, size_t writeFrame, size_t clipStart, size_t clipFrames)
		{
			if (writeFrame >= outFrames || clip.frames == 0) return;
			size_t available = std::min(clipFrames, clip.frames - clipStart);
			size_t room = outFrames - writeFrame;
			size_t n = std::min(available, room);
			for (size_t i = 0; i < n; ++i)
			{
				mixSample(out[(writeFrame + i) * 2],     clip.samples[(clipStart + i) * 2]);
				mixSample(out[(writeFrame + i) * 2 + 1], clip.samples[(clipStart + i) * 2 + 1]);
			}
		}

		void mixHoldLoop(std::vector<int16_t>& out, size_t outFrames,
			const SEClip& clip, size_t writeFrame, size_t durFrames)
		{
			if (writeFrame >= outFrames || durFrames == 0 || clip.frames == 0) return;

			size_t introLen = clip.loopable ? clip.loopStart : 0;
			size_t releaseLen = clip.loopable ? (clip.frames - clip.loopEnd) : 0;

			if (!clip.loopable || durFrames <= introLen + releaseLen)
			{
				// Short hold: play from start, truncated.
				mixClip(out, outFrames, clip, writeFrame, 0, durFrames);
				return;
			}

			mixClip(out, outFrames, clip, writeFrame, 0, introLen);
			size_t pos = writeFrame + introLen;
			size_t bodyLen = durFrames - introLen - releaseLen;
			size_t loopSpan = clip.loopEnd - clip.loopStart;
			while (bodyLen > 0 && pos < outFrames)
			{
				size_t chunk = std::min(bodyLen, loopSpan);
				mixClip(out, outFrames, clip, pos, clip.loopStart, chunk);
				pos += chunk;
				bodyLen -= chunk;
			}
			if (pos < outFrames)
				mixClip(out, outFrames, clip, pos, clip.loopEnd, releaseLen);
		}

		bool writeWavS16Stereo(const std::string& path,
			const std::vector<int16_t>& samples, int sampleRate)
		{
			FILE* f = std::fopen(path.c_str(), "wb");
			if (!f) return false;

			auto writeLE16 = [&](uint16_t v) {
				uint8_t b[2] = { uint8_t(v & 0xFF), uint8_t((v >> 8) & 0xFF) };
				std::fwrite(b, 1, 2, f);
			};
			auto writeLE32 = [&](uint32_t v) {
				uint8_t b[4] = {
					uint8_t(v & 0xFF), uint8_t((v >> 8) & 0xFF),
					uint8_t((v >> 16) & 0xFF), uint8_t((v >> 24) & 0xFF)
				};
				std::fwrite(b, 1, 4, f);
			};

			uint32_t byteRate = sampleRate * SE_MIX_CHANNELS * 2;
			uint32_t dataSize = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
			uint32_t riffSize = 36 + dataSize;

			std::fwrite("RIFF", 1, 4, f);
			writeLE32(riffSize);
			std::fwrite("WAVE", 1, 4, f);
			std::fwrite("fmt ", 1, 4, f);
			writeLE32(16);
			writeLE16(1);                      // PCM
			writeLE16(SE_MIX_CHANNELS);
			writeLE32(sampleRate);
			writeLE32(byteRate);
			writeLE16(SE_MIX_CHANNELS * 2);    // block align
			writeLE16(16);                     // bits per sample
			std::fwrite("data", 1, 4, f);
			writeLE32(dataSize);
			std::fwrite(samples.data(), 1, dataSize, f);

			std::fclose(f);
			return true;
		}

		bool generateSESoundtrack(const ScoreContext& context,
			float totalSeconds, int seProfileIndex, float userSeVolume,
			const std::string& resourceDir, const std::string& outWavPath)
		{
			if (seProfileIndex < 0) seProfileIndex = 0;
			if (seProfileIndex >= static_cast<int>(Audio::soundEffectsProfileCount))
				seProfileIndex = Audio::soundEffectsProfileCount - 1;

			char profileDir[32];
			std::snprintf(profileDir, sizeof(profileDir), "res/sound/%02d/", seProfileIndex + 1);
			std::string basePath = resourceDir + profileDir;

			std::unordered_map<std::string, SEClip> clips;
			for (size_t i = 0; i < arrayLength(SE_NAMES); ++i)
			{
				const char* name = mmw::SE_NAMES[i];
				bool loopable = (std::string_view(name) == SE_CONNECT)
					|| (std::string_view(name) == SE_CRITICAL_CONNECT);
				float vol = kSEVolumes[i] * userSeVolume;
				SEClip clip{};
				if (!loadSEClip(basePath + name + ".mp3", vol, loopable, clip))
					return false;
				clips.emplace(name, std::move(clip));
			}

			std::vector<SEOneShot> oneshots;
			std::vector<SEHoldLoop> holds;
			oneshots.reserve(1024);
			holds.reserve(256);
			collectSEEvents(context, oneshots, holds);

			size_t totalFrames = static_cast<size_t>(std::ceil(totalSeconds * SE_MIX_SAMPLE_RATE));
			std::vector<int16_t> mix(totalFrames * SE_MIX_CHANNELS, 0);

			for (const auto& ev : oneshots)
			{
				if (ev.time < 0 || ev.time > totalSeconds) continue;
				auto it = clips.find(ev.name);
				if (it == clips.end()) continue;
				size_t writeFrame = static_cast<size_t>(ev.time * SE_MIX_SAMPLE_RATE);
				mixClip(mix, totalFrames, it->second, writeFrame, 0, it->second.frames);
			}
			for (const auto& h : holds)
			{
				if (h.startTime < 0 || h.startTime > totalSeconds) continue;
				auto it = clips.find(h.name);
				if (it == clips.end()) continue;
				float durSec = std::min(h.endTime, totalSeconds) - h.startTime;
				if (durSec <= 0) continue;
				size_t writeFrame = static_cast<size_t>(h.startTime * SE_MIX_SAMPLE_RATE);
				size_t durFrames = static_cast<size_t>(durSec * SE_MIX_SAMPLE_RATE);
				mixHoldLoop(mix, totalFrames, it->second, writeFrame, durFrames);
			}

			std::fprintf(stderr, "[se] %zu one-shots, %zu holds mixed\n",
				oneshots.size(), holds.size());

			return writeWavS16Stereo(outWavPath, mix, SE_MIX_SAMPLE_RATE);
		}

		FILE* spawnFfmpeg(const RenderOptions& opt, float audioOffsetSeconds,
			const std::string& sePath)
		{
			const bool haveBgm = !opt.audioPath.empty();
			const bool haveSe = !sePath.empty();

			std::string cmd = shellQuote(opt.ffmpegPath);
			cmd += " -y";
			cmd += " -f rawvideo -pix_fmt rgba";
			cmd += " -s " + std::to_string(opt.width) + "x" + std::to_string(opt.height);
			cmd += " -r " + std::to_string(opt.fps);
			cmd += " -i -";

			int bgmIndex = -1;
			int seIndex = -1;
			int nextIndex = 1;

			if (haveBgm)
			{
				if (audioOffsetSeconds > 0.f)
				{
					char buf[64];
					std::snprintf(buf, sizeof(buf), " -itsoffset %.6f", audioOffsetSeconds);
					cmd += buf;
				}
				else if (audioOffsetSeconds < 0.f)
				{
					char buf[64];
					std::snprintf(buf, sizeof(buf), " -ss %.6f", -audioOffsetSeconds);
					cmd += buf;
				}
				cmd += " -i " + shellQuote(opt.audioPath);
				bgmIndex = nextIndex++;
			}
			if (haveSe)
			{
				cmd += " -i " + shellQuote(sePath);
				seIndex = nextIndex++;
			}

			cmd += " -vf vflip";
			cmd += " -c:v libx264 -pix_fmt yuv420p -preset medium -crf 18";

			if (haveBgm && haveSe)
			{
				char buf[128];
				std::snprintf(buf, sizeof(buf),
					" -filter_complex [%d:a][%d:a]amix=inputs=2:duration=longest:normalize=0[aout]",
					bgmIndex, seIndex);
				cmd += buf;
				cmd += " -map 0:v -map [aout]";
				cmd += " -c:a aac -b:a 192k";
			}
			else if (haveBgm)
			{
				cmd += " -map 0:v -map " + std::to_string(bgmIndex) + ":a";
				cmd += " -c:a aac -b:a 192k";
			}
			else if (haveSe)
			{
				cmd += " -map 0:v -map " + std::to_string(seIndex) + ":a";
				cmd += " -c:a aac -b:a 192k";
			}

			cmd += " -shortest";
			cmd += " " + shellQuote(opt.outputPath);
			cmd += " 2>&1";

			std::fprintf(stderr, "[render] ffmpeg cmd: %s\n", cmd.c_str());
			return popen(cmd.c_str(), "w");
		}
	}

	int runOfflineRender(int argc, char** argv,
	                     const std::string& resourceDir,
	                     const std::string& userDataDir)
	{
		RenderOptions opt;
		if (!parseArgs(argc, argv, opt))
			return 2;

		// GLFW hidden-window context setup
		if (!glfwInit())
		{
			std::fprintf(stderr, "Failed to initialize GLFW\n");
			return 3;
		}
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

		GLFWwindow* window = glfwCreateWindow(opt.width, opt.height, "mmw-render", NULL, NULL);
		if (!window)
		{
			std::fprintf(stderr, "Failed to create offscreen GLFW window\n");
			glfwTerminate();
			return 3;
		}
		glfwMakeContextCurrent(window);

		if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
		{
			std::fprintf(stderr, "Failed to load OpenGL procs\n");
			glfwDestroyWindow(window);
			glfwTerminate();
			return 3;
		}

		glEnable(GL_BLEND);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		glViewport(0, 0, opt.width, opt.height);

		// Configuration
		Application::setPaths(resourceDir, userDataDir);
		config.read(userDataDir + APP_CONFIG_FILENAME);

		if (opt.hasNoteSpeed)       config.pvNoteSpeed = opt.noteSpeed;
		if (opt.hasStageCover)      config.pvStageCover = opt.stageCover;
		if (opt.hasBgBrightness)    config.pvBackgroundBrightness = opt.bgBrightness;
		if (opt.hasDrawBackground)  config.drawBackground = opt.drawBackground;
		if (opt.hasBackgroundImage) config.backgroundImage = opt.backgroundImage;
		if (opt.hasNotesSkin)       config.notesSkin = opt.notesSkin;
		if (opt.hasEffectsProfile)  config.pvEffectsProfile = opt.effectsProfile;
		if (opt.hasMirror)          config.pvMirrorScore = opt.mirror;

		Application::loadResources();

		// Load score
		auto serializer = makeSerializer(opt.scorePath);
		if (!serializer)
		{
			std::fprintf(stderr, "Unsupported score format: %s\n", opt.scorePath.c_str());
			glfwDestroyWindow(window);
			glfwTerminate();
			return 4;
		}

		Score score;
		try
		{
			score = serializer->deserialize(opt.scorePath);
		}
		catch (const PartialScoreDeserializeError& ex)
		{
			std::fprintf(stderr, "Partial load: %s\n", ex.what());
			score = ex.getScore();
		}
		catch (const std::exception& ex)
		{
			std::fprintf(stderr, "Failed to load score: %s\n", ex.what());
			glfwDestroyWindow(window);
			glfwTerminate();
			return 4;
		}

		// Context & preview
		ScoreContext context{};
		context.score = std::move(score);
		context.workingData = EditorScoreData(context.score.metadata, opt.scorePath);
		if (!opt.audioPath.empty())
			context.workingData.musicFilename = opt.audioPath;
		context.scorePreviewDrawData.calculateDrawData(context.score);

		auto renderer = std::make_unique<Renderer>();
		ScorePreviewWindow preview;
		preview.loadNoteEffects(context.scorePreviewDrawData.effectView);

		// Duration
		const int maxTick = scoreMaxTick(context.score);
		const float scoreDuration = accumulateDuration(maxTick, TICKS_PER_BEAT, context.score.tempoChanges);
		const float totalSeconds = scoreDuration + opt.tailSeconds;
		const int totalFrames = static_cast<int>(std::ceil(totalSeconds * opt.fps));

		std::fprintf(stderr, "[render] score duration %.3fs, total %.3fs (%d frames @ %d fps)\n",
			scoreDuration, totalSeconds, totalFrames, opt.fps);

		// Audio offset: score metadata value unless overridden.
		float audioOffset = context.workingData.musicOffset;
		if (opt.hasAudioOffset) audioOffset = opt.audioOffset;

		// Sound effects mixdown -> intermediate WAV
		std::string sePath;
		if (!opt.disableSE)
		{
			int seProfile = opt.hasSeProfile ? opt.seProfile : config.seProfileIndex;
			float seVol = opt.hasSeVolume ? opt.seVolume : config.seVolume;
			sePath = opt.outputPath + ".se.wav";
			std::fprintf(stderr, "[se] generating SE track (profile %d, vol %.2f) -> %s\n",
				seProfile, seVol, sePath.c_str());
			if (!generateSESoundtrack(context, totalSeconds, seProfile, seVol, resourceDir, sePath))
			{
				std::fprintf(stderr, "[se] failed; continuing without SE\n");
				sePath.clear();
			}
		}

		// ffmpeg
		FILE* pipe = spawnFfmpeg(opt, audioOffset, sePath);
		if (!pipe)
		{
			std::fprintf(stderr, "Failed to spawn ffmpeg\n");
			glfwDestroyWindow(window);
			glfwTerminate();
			return 5;
		}

		std::vector<uint8_t> pixels(static_cast<size_t>(opt.width) * opt.height * 4);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);

		int lastReported = -1;
		for (int frame = 0; frame < totalFrames; ++frame)
		{
			float t = static_cast<float>(frame) / static_cast<float>(opt.fps);
			context.currentTick = t > 0.f
				? accumulateTicks(t, TICKS_PER_BEAT, context.score.tempoChanges)
				: 0;

			preview.renderToFramebuffer(context, renderer.get(),
				static_cast<float>(opt.width), static_cast<float>(opt.height), t, true);

			Framebuffer& fb = preview.getPreviewBuffer();
			fb.bind();
			glReadPixels(0, 0, opt.width, opt.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			fb.unblind();

			if (std::fwrite(pixels.data(), 1, pixels.size(), pipe) != pixels.size())
			{
				std::fprintf(stderr, "ffmpeg stdin write failed at frame %d\n", frame);
				pclose(pipe);
				glfwDestroyWindow(window);
				glfwTerminate();
				return 6;
			}

			int pct = static_cast<int>(100.0f * (frame + 1) / totalFrames);
			if (pct != lastReported && pct % 5 == 0)
			{
				std::fprintf(stderr, "[render] %d%% (%d/%d)\n", pct, frame + 1, totalFrames);
				lastReported = pct;
			}
		}

		int ffStatus = pclose(pipe);
		glfwDestroyWindow(window);
		glfwTerminate();

		if (ffStatus != 0)
		{
			std::fprintf(stderr, "ffmpeg exited with status %d\n", ffStatus);
			return 7;
		}

		std::fprintf(stderr, "[render] wrote %s\n", opt.outputPath.c_str());
		return 0;
	}
}
