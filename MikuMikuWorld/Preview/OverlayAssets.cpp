#include "OverlayAssets.h"
#include "../ResourceManager.h"
#include "../IO.h"
#include "../File.h"
#include <cstdio>

namespace MikuMikuWorld
{
	Texture* OverlayAssets::loadOne(const std::string& path)
	{
		if (!IO::File::exists(path)) return nullptr;
		ResourceManager::loadTexture(path, TextureFilterMode::Linear, TextureFilterMode::Linear);
		int idx = ResourceManager::getTextureByFilename(path);
		if (idx < 0) return nullptr;
		return &ResourceManager::textures[idx];
	}

	void OverlayAssets::load(const std::string& overlayDir)
	{
		if (!IO::File::exists(overlayDir + "score/bar.png"))
		{
			// Asset pack absent — the rest of the overlay still works with
			// minimal self-drawn shapes.
			return;
		}

		// Score bar components
		bar        = loadOne(overlayDir + "score/bar.png");
		bars       = loadOne(overlayDir + "score/bars.png");
		barBg      = loadOne(overlayDir + "score/bg.png");
		barFg      = loadOne(overlayDir + "score/fg.png");
		scoreLabel = loadOne(overlayDir + "score/score.png");

		// Rank glyphs
		rankD = loadOne(overlayDir + "score/rank/chr/d.png");
		rankC = loadOne(overlayDir + "score/rank/chr/c.png");
		rankB = loadOne(overlayDir + "score/rank/chr/b.png");
		rankA = loadOne(overlayDir + "score/rank/chr/a.png");
		rankS = loadOne(overlayDir + "score/rank/chr/s.png");

		// Score digits — use the "s" set (score bar medium)
		const std::string scoreDigitDir = overlayDir + "score/digit/";
		for (int i = 0; i < 10; ++i)
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "s%d.png", i);
			scoreDigit[i] = loadOne(scoreDigitDir + buf);
		}
		scoreDigitPercent = loadOne(scoreDigitDir + "s%.png");

		// Combo digits — "p_" prefix is the principal large rainbow set
		const std::string comboDir = overlayDir + "combo/";
		for (int i = 0; i < 10; ++i)
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "p_%d.png", i);
			comboDigit[i] = loadOne(comboDir + buf);
		}
		comboLabel = loadOne(comboDir + "p_c.png");

		// Judgment images
		judgePerfect = loadOne(overlayDir + "judgement/judge_perfect.png");
		judgeGreat   = loadOne(overlayDir + "judgement/judge_great.png");
		judgeGood    = loadOne(overlayDir + "judgement/judge_good.png");

		// AP video — just remember the path; playback is decoded lazily.
		const std::string apMp4 = overlayDir + "ap.mp4";
		if (IO::File::exists(apMp4)) apVideoPath = apMp4;
	}
}
