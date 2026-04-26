#include "OfflineRenderer.h"

#include <glad/glad.h>

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
#include "Platform/HeadlessGL.h"
#include "PreviewData.h"
#include "Rendering/Renderer.h"
#include "Score.h"
#include "ScoreContext.h"
#include "ScorePreview.h"
#include "ScoreSerializer.h"
#include "SonolusSerializer.h"
#include "SusSerializer.h"
#include "UscSerializer.h"
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
			// 3s pause + ~10s AP.mp4 + 2s cushion so the AP takeover finishes.
			float tailSeconds = 15.0f;

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
			bool dryRun = false;
			bool hasOverlay = false; bool overlayEnabled = true;
			bool hasIntroFont = false; std::string introFontPath;

			// Encoder overrides. Empty / unset → fall back to legacy defaults
			// (libx264 -preset medium -crf 18). Useful for HW encoders such
			// as h264_videotoolbox on Apple Silicon.
			std::string vcodec;
			std::string preset;
			bool hasCrf = false; int crf = 0;
			std::string bitrate;

			// Intro (pre-chart) animation
			bool intro = false;
			std::string introDifficulty = "master";
			std::string introExtra;
			std::string introTitle;
			std::string introJacket;
			std::string introLyricist;
			std::string introComposer;
			std::string introArranger;
			std::string introVocal;
			std::string introChartAuthor;
			std::string introLang = "jp";
		};

		constexpr float INTRO_OFFSET_SECONDS = Overlay::INTRO_OFFSET_SECONDS;

		void printUsage()
		{
			std::fprintf(stderr,
				"Usage: MikuMikuWorld --render \\\n"
				"  --score <path>       Score file (.mmws/.sus/.json/.json.gz/.usc)\n"
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
				"  [--no-se]            Disable note sound effects\n"
				"  [--no-overlay]       Disable in-frame score/combo overlay\n"
				"  [--overlay]          Force-enable the overlay (default)\n"
				"  [--intro-font <path>] Override intro animation font (.ttf/.otf/.ttc/.otc)\n"
				"  [--intro]            Prepend a 9s pjsekai-style intro (chart/music shift by 9s)\n"
				"  [--intro-difficulty <easy|normal|hard|expert|master|append>]\n"
				"  [--intro-extra <text>]       Extra/level text (e.g. Lv.30)\n"
				"  [--intro-title <text>]       Override title text (defaults to score title)\n"
				"  [--intro-jacket <path>]      Jacket image shown in the intro card\n"
				"  [--intro-lyricist <text>]\n"
				"  [--intro-composer <text>]\n"
				"  [--intro-arranger <text>]\n"
				"  [--intro-vocal <text>]\n"
				"  [--intro-chart-author <text>] (defaults to score author)\n"
				"  [--intro-lang <jp|en>]       Description language (default jp)\n"
				"  [--vcodec <name>]    Video codec (default libx264; e.g. h264_videotoolbox)\n"
				"  [--preset <name>]    Encoder preset (default medium for libx264)\n"
				"  [--crf <N>]          CRF quality (default 18 for libx264; ignored by HW codecs)\n"
				"  [--bitrate <rate>]   Target bitrate (e.g. 12M); takes precedence over --crf\n"
				"  [--dry-run]          Load & verify the score, then exit (no rendering)\n");
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
				else if (a == "--no-overlay") { opt.hasOverlay = true; opt.overlayEnabled = false; }
				else if (a == "--overlay") { opt.hasOverlay = true; opt.overlayEnabled = true; }
				else if (a == "--intro-font") { if (!needs(i, "--intro-font")) return false; opt.hasIntroFont = true; opt.introFontPath = argv[++i]; }
				else if (a == "--intro") { opt.intro = true; }
				else if (a == "--intro-difficulty") { if (!needs(i, "--intro-difficulty")) return false; opt.introDifficulty = argv[++i]; }
				else if (a == "--intro-extra") { if (!needs(i, "--intro-extra")) return false; opt.introExtra = argv[++i]; }
				else if (a == "--intro-title") { if (!needs(i, "--intro-title")) return false; opt.introTitle = argv[++i]; }
				else if (a == "--intro-jacket") { if (!needs(i, "--intro-jacket")) return false; opt.introJacket = argv[++i]; }
				else if (a == "--intro-lyricist") { if (!needs(i, "--intro-lyricist")) return false; opt.introLyricist = argv[++i]; }
				else if (a == "--intro-composer") { if (!needs(i, "--intro-composer")) return false; opt.introComposer = argv[++i]; }
				else if (a == "--intro-arranger") { if (!needs(i, "--intro-arranger")) return false; opt.introArranger = argv[++i]; }
				else if (a == "--intro-vocal") { if (!needs(i, "--intro-vocal")) return false; opt.introVocal = argv[++i]; }
				else if (a == "--intro-chart-author") { if (!needs(i, "--intro-chart-author")) return false; opt.introChartAuthor = argv[++i]; }
				else if (a == "--intro-lang") { if (!needs(i, "--intro-lang")) return false; opt.introLang = argv[++i]; }
				else if (a == "--vcodec") { if (!needs(i, "--vcodec")) return false; opt.vcodec = argv[++i]; }
				else if (a == "--preset") { if (!needs(i, "--preset")) return false; opt.preset = argv[++i]; }
				else if (a == "--crf") { if (!needs(i, "--crf")) return false; opt.hasCrf = true; opt.crf = std::atoi(argv[++i]); }
				else if (a == "--bitrate") { if (!needs(i, "--bitrate")) return false; opt.bitrate = argv[++i]; }
				else if (a == "--dry-run") { opt.dryRun = true; }
				else {
					std::fprintf(stderr, "Unknown argument: %s\n", a.c_str());
					printUsage();
					return false;
				}
			}

			if (opt.scorePath.empty())
			{
				std::fprintf(stderr, "--score is required.\n");
				printUsage();
				return false;
			}
			if (!opt.dryRun && opt.outputPath.empty())
			{
				std::fprintf(stderr, "--out is required (unless --dry-run).\n");
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
			case SerializeFormat::UscFormat:
				return std::make_unique<UscSerializer>();
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
			float totalSeconds, float eventOffsetSeconds,
			int seProfileIndex, float userSeVolume,
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
				float t = ev.time + eventOffsetSeconds;
				if (t < 0 || t > totalSeconds) continue;
				auto it = clips.find(ev.name);
				if (it == clips.end()) continue;
				size_t writeFrame = static_cast<size_t>(t * SE_MIX_SAMPLE_RATE);
				mixClip(mix, totalFrames, it->second, writeFrame, 0, it->second.frames);
			}
			for (const auto& h : holds)
			{
				float startT = h.startTime + eventOffsetSeconds;
				float endT   = h.endTime   + eventOffsetSeconds;
				if (startT < 0 || startT > totalSeconds) continue;
				auto it = clips.find(h.name);
				if (it == clips.end()) continue;
				float durSec = std::min(endT, totalSeconds) - startT;
				if (durSec <= 0) continue;
				size_t writeFrame = static_cast<size_t>(startT * SE_MIX_SAMPLE_RATE);
				size_t durFrames = static_cast<size_t>(durSec * SE_MIX_SAMPLE_RATE);
				mixHoldLoop(mix, totalFrames, it->second, writeFrame, durFrames);
			}

			std::fprintf(stderr, "[se] %zu one-shots, %zu holds mixed\n",
				oneshots.size(), holds.size());

			return writeWavS16Stereo(outWavPath, mix, SE_MIX_SAMPLE_RATE);
		}

		float queryAudioLengthSeconds(const std::string& audioPath)
		{
			if (audioPath.empty() || !IO::File::exists(audioPath))
				return 0.f;
			Audio::SoundBuffer buf{};
			Result r = Audio::decodeAudioFile(audioPath, buf);
			if (!r.isOk() || buf.sampleRate == 0)
			{
				std::fprintf(stderr,
					"[render] could not query audio length for %s (%s)\n",
					audioPath.c_str(), r.getMessage().c_str());
				if (buf.isValid()) buf.dispose();
				return 0.f;
			}
			float seconds = static_cast<float>(buf.frameCount) / static_cast<float>(buf.sampleRate);
			buf.dispose();
			return seconds;
		}

		FILE* spawnFfmpeg(const RenderOptions& opt, float audioOffsetSeconds,
			const std::string& sePath,
			const std::string& apAudioPath, float apStartSeconds)
		{
			const bool haveBgm = !opt.audioPath.empty();
			const bool haveSe = !sePath.empty();
			const bool haveAp = !apAudioPath.empty();

			std::string cmd = shellQuote(opt.ffmpegPath);
			cmd += " -y";
			cmd += " -f rawvideo -pix_fmt rgba";
			cmd += " -s " + std::to_string(opt.width) + "x" + std::to_string(opt.height);
			cmd += " -r " + std::to_string(opt.fps);
			cmd += " -i -";

			int bgmIndex = -1;
			int seIndex = -1;
			int apIndex = -1;
			int nextIndex = 1;

			if (haveBgm)
			{
				// Negative offsets seek into the input; positive offsets are applied
				// via adelay inside the filter graph (itsoffset is unreliable when
				// fed through amix).
				if (audioOffsetSeconds < 0.f)
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
			if (haveAp)
			{
				cmd += " -i " + shellQuote(apAudioPath);
				apIndex = nextIndex++;
			}

			cmd += " -vf vflip";

			const std::string vcodec = opt.vcodec.empty() ? "libx264" : opt.vcodec;
			cmd += " -c:v " + vcodec;
			cmd += " -pix_fmt yuv420p";

			if (!opt.preset.empty())
				cmd += " -preset " + opt.preset;
			else if (vcodec == "libx264")
				cmd += " -preset medium";

			if (!opt.bitrate.empty())
			{
				cmd += " -b:v " + opt.bitrate;
			}
			else if (opt.hasCrf)
			{
				cmd += " -crf " + std::to_string(opt.crf);
			}
			else if (vcodec == "libx264")
			{
				cmd += " -crf 18";
			}

			// VideoToolbox: lift the real-time ceiling for offline batch renders.
			if (vcodec.find("videotoolbox") != std::string::npos)
				cmd += " -realtime 0";

			// Build audio graph. Both BGM and AP use adelay to land at the right
			// moment (itsoffset is unreliable when feeding through amix).
			std::vector<std::string> amixInputs;
			std::string filter;
			if (haveBgm)
			{
				if (audioOffsetSeconds > 0.f)
				{
					char buf[128];
					const int delayMs = (int)(audioOffsetSeconds * 1000.f);
					std::snprintf(buf, sizeof(buf),
						"[%d:a]adelay=%d|%d[bgm];",
						bgmIndex, delayMs, delayMs);
					filter += buf;
					amixInputs.push_back("bgm");
				}
				else
				{
					amixInputs.push_back(std::to_string(bgmIndex) + ":a");
				}
			}
			if (haveSe)
			{
				amixInputs.push_back(std::to_string(seIndex) + ":a");
			}
			if (haveAp)
			{
				char buf[128];
				const int delayMs = (int)std::max(0.f, apStartSeconds * 1000.f);
				std::snprintf(buf, sizeof(buf),
					"[%d:a]adelay=%d|%d,apad=pad_dur=0.1[ap];",
					apIndex, delayMs, delayMs);
				filter += buf;
				amixInputs.push_back("ap");
			}

			if (amixInputs.size() >= 2)
			{
				filter += "";
				for (const auto& lbl : amixInputs)
				{
					filter += "[" + lbl + "]";
				}
				filter += "amix=inputs=" + std::to_string(amixInputs.size())
				       + ":duration=longest:normalize=0[aout]";
				cmd += " -filter_complex " + shellQuote(filter);
				cmd += " -map 0:v -map [aout]";
				cmd += " -c:a aac -b:a 192k";
			}
			else if (amixInputs.size() == 1)
			{
				if (!filter.empty())
				{
					// Single AP input still needs the delay applied.
					filter += "[" + amixInputs[0] + "]anull[aout]";
					cmd += " -filter_complex " + shellQuote(filter);
					cmd += " -map 0:v -map [aout]";
				}
				else
				{
					cmd += " -map 0:v -map " + amixInputs[0];
				}
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

		if (opt.dryRun)
		{
			auto serializer = makeSerializer(opt.scorePath);
			if (!serializer)
			{
				std::fprintf(stderr, "Unsupported score format: %s\n", opt.scorePath.c_str());
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
				return 4;
			}
			const int maxTick = scoreMaxTick(score);
			const float duration = accumulateDuration(maxTick, TICKS_PER_BEAT, score.tempoChanges);
			std::fprintf(stderr,
				"[dry-run] notes=%zu holds=%zu tempos=%zu hiSpeeds=%zu maxTick=%d duration=%.3fs musicOffset=%.3fms\n",
				score.notes.size(), score.holdNotes.size(),
				score.tempoChanges.size(), score.hiSpeedChanges.size(),
				maxTick, duration, score.metadata.musicOffset);
			return 0;
		}

		if (!Platform::initHeadlessGL(opt.width, opt.height))
			return 3;

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
		if (opt.hasOverlay)         config.pvOverlayEnabled = opt.overlayEnabled;
		if (opt.hasIntroFont)       config.pvIntroFontPath = opt.introFontPath;

		Application::loadResources();

		// Load score
		auto serializer = makeSerializer(opt.scorePath);
		if (!serializer)
		{
			std::fprintf(stderr, "Unsupported score format: %s\n", opt.scorePath.c_str());
			Platform::shutdownHeadlessGL();
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
			Platform::shutdownHeadlessGL();
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

		// Intro pre-roll pushes music/notes/SE/AP by INTRO_OFFSET_SECONDS seconds.
		const float introOffset = opt.intro ? INTRO_OFFSET_SECONDS : 0.f;
		if (opt.intro)
		{
			if (!opt.introJacket.empty())
				context.workingData.jacket.load(opt.introJacket);

			OverlayIntroData introData;
			introData.difficulty  = opt.introDifficulty;
			introData.extra       = opt.introExtra;
			introData.title       = opt.introTitle.empty()
				? context.workingData.title : opt.introTitle;
			introData.lyricist    = opt.introLyricist;
			introData.composer    = opt.introComposer;
			introData.arranger    = opt.introArranger;
			introData.vocal       = opt.introVocal;
			introData.chartAuthor = opt.introChartAuthor.empty()
				? context.workingData.designer : opt.introChartAuthor;
			introData.useEnglish  = (opt.introLang == "en");
			preview.configureIntro(introOffset, introData);
			std::fprintf(stderr, "[render] intro enabled (difficulty=%s, lang=%s, +%.1fs)\n",
				introData.difficulty.c_str(), opt.introLang.c_str(), introOffset);
		}

		// Duration
		const int maxTick = scoreMaxTick(context.score);
		const float scoreDuration = accumulateDuration(maxTick, TICKS_PER_BEAT, context.score.tempoChanges);

		const float musicChartStartTime = opt.hasAudioOffset
			? opt.audioOffset
			: context.workingData.musicOffset / 1000.0f;
		const float musicLengthSeconds = queryAudioLengthSeconds(opt.audioPath);
		const float musicChartEndTime = musicLengthSeconds > 0.f
			? (musicChartStartTime + musicLengthSeconds)
			: 0.f;
		const float effectiveDuration = std::max(scoreDuration, musicChartEndTime);

		const float totalSeconds = effectiveDuration + opt.tailSeconds + introOffset;
		const int totalFrames = static_cast<int>(std::ceil(totalSeconds * opt.fps));

		std::fprintf(stderr,
			"[render] score duration %.3fs, music end %.3fs (chart time), total %.3fs (%d frames @ %d fps)\n",
			scoreDuration, musicChartEndTime, totalSeconds, totalFrames, opt.fps);

		// Audio offset: score metadata value (ms) converted to seconds unless
		// overridden via --audio-offset (already in seconds). Intro pushes audio
		// by an additional introOffset seconds so the song lines up with frame 540.
		float audioOffset = musicChartStartTime + introOffset;

		// Sound effects mixdown -> intermediate WAV. Events collected relative to
		// the chart are shifted into video time by introOffset.
		std::string sePath;
		if (!opt.disableSE)
		{
			int seProfile = opt.hasSeProfile ? opt.seProfile : config.seProfileIndex;
			float seVol = opt.hasSeVolume ? opt.seVolume : config.seVolume;
			sePath = opt.outputPath + ".se.wav";
			std::fprintf(stderr, "[se] generating SE track (profile %d, vol %.2f) -> %s\n",
				seProfile, seVol, sePath.c_str());
			if (!generateSESoundtrack(context, totalSeconds, introOffset, seProfile, seVol, resourceDir, sePath))
			{
				std::fprintf(stderr, "[se] failed; continuing without SE\n");
				sePath.clear();
			}
		}

		// Keep in sync with Overlay::AP_TRIGGER_DELAY.
		constexpr float AP_TRIGGER_DELAY = 2.0f;
		std::string apAudioPath = resourceDir + "res/overlay/ap.mp4";
		if (!IO::File::exists(apAudioPath)) apAudioPath.clear();
		const float apStartSec = apAudioPath.empty() ? 0.f
			: effectiveDuration + AP_TRIGGER_DELAY + introOffset;

		preview.setMusicEndTimeOverride(musicChartEndTime);

		// ffmpeg
		FILE* pipe = spawnFfmpeg(opt, audioOffset, sePath, apAudioPath, apStartSec);
		if (!pipe)
		{
			std::fprintf(stderr, "Failed to spawn ffmpeg\n");
			Platform::shutdownHeadlessGL();
			return 5;
		}

		std::vector<uint8_t> pixels(static_cast<size_t>(opt.width) * opt.height * 4);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);

		int lastReported = -1;
		for (int frame = 0; frame < totalFrames; ++frame)
		{
			float videoTime = static_cast<float>(frame) / static_cast<float>(opt.fps);
			float chartTime = videoTime - introOffset;
			// accumulateTicks returns a negative tick for negative chartTime,
			// which the rendering pipeline uses to place notes above the lane
			// during the intro pre-roll.
			context.currentTick = accumulateTicks(chartTime, TICKS_PER_BEAT, context.score.tempoChanges);

			preview.renderToFramebuffer(context, renderer.get(),
				static_cast<float>(opt.width), static_cast<float>(opt.height), chartTime, true);

			Framebuffer& fb = preview.getPreviewBuffer();
			fb.bind();
			glReadPixels(0, 0, opt.width, opt.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			fb.unblind();

			if (std::fwrite(pixels.data(), 1, pixels.size(), pipe) != pixels.size())
			{
				std::fprintf(stderr, "ffmpeg stdin write failed at frame %d\n", frame);
				pclose(pipe);
				Platform::shutdownHeadlessGL();
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
		Platform::shutdownHeadlessGL();

		if (ffStatus != 0)
		{
			std::fprintf(stderr, "ffmpeg exited with status %d\n", ffStatus);
			return 7;
		}

		std::fprintf(stderr, "[render] wrote %s\n", opt.outputPath.c_str());
		return 0;
	}
}
