#include "OverlayAssets.h"
#include "../ResourceManager.h"
#include "../IO.h"
#include "../File.h"
#include <cstdio>

namespace MikuMikuWorld
{
	int OverlayAssets::loadOne(const std::string& path)
	{
		if (!IO::File::exists(path)) return NO_TEX;
		ResourceManager::loadTexture(path, TextureFilterMode::Linear, TextureFilterMode::Linear);
		return ResourceManager::getTextureByFilename(path);
	}

	const Texture* OverlayAssets::get(int index)
	{
		if (index < 0 || index >= (int)ResourceManager::textures.size()) return nullptr;
		return &ResourceManager::textures[index];
	}

	void OverlayAssets::load(const std::string& overlayDir)
	{
		if (!IO::File::exists(overlayDir + "score/bar.png"))
			return;

		barBg      = loadOne(overlayDir + "score/bg.png");
		bars       = loadOne(overlayDir + "score/bars.png");
		barFg      = loadOne(overlayDir + "score/fg.png");
		bar        = loadOne(overlayDir + "score/bar.png");
		scoreLabel = loadOne(overlayDir + "score/score.png");

		rankD = loadOne(overlayDir + "score/rank/chr/d.png");
		rankC = loadOne(overlayDir + "score/rank/chr/c.png");
		rankB = loadOne(overlayDir + "score/rank/chr/b.png");
		rankA = loadOne(overlayDir + "score/rank/chr/a.png");
		rankS = loadOne(overlayDir + "score/rank/chr/s.png");

		const std::string scoreDigitDir = overlayDir + "score/digit/";
		for (int i = 0; i < 10; ++i)
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "s%d.png", i);
			scoreDigit[i] = loadOne(scoreDigitDir + buf);

			std::snprintf(buf, sizeof(buf), "%d.png", i);
			scoreDigitFill[i] = loadOne(scoreDigitDir + buf);
		}
		scoreDigitPercent = loadOne(scoreDigitDir + "s%.png");

		const std::string comboDir = overlayDir + "combo/";
		for (int i = 0; i < 10; ++i)
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "p_%d.png", i);
			comboDigit[i] = loadOne(comboDir + buf);
		}
		comboLabel = loadOne(comboDir + "p_c.png");

		judgePerfect = loadOne(overlayDir + "judgement/judge_perfect.png");
		judgeGreat   = loadOne(overlayDir + "judgement/judge_great.png");
		judgeGood    = loadOne(overlayDir + "judgement/judge_good.png");

		const std::string apMp4 = overlayDir + "ap.mp4";
		if (IO::File::exists(apMp4)) apVideoPath = apMp4;
	}
}
