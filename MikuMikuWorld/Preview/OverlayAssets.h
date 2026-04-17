#pragma once
#include "../Rendering/Texture.h"
#include <string>

namespace MikuMikuWorld
{
	// Centralized loader for the optional Project Sekai overlay asset pack that
	// users drop into `res/overlay/` (not tracked in git). When the pack is
	// missing, Overlay falls back to minimal self-drawn rendering.
	//
	// Assets are kept as indices into ResourceManager::textures rather than raw
	// pointers because that vector may reallocate as more textures are pushed.
	class OverlayAssets
	{
	public:
		static constexpr int NO_TEX = -1;

		// Score bar
		int barBg{ NO_TEX };
		int barFg{ NO_TEX };
		int bar{ NO_TEX };
		int bars{ NO_TEX };
		int scoreLabel{ NO_TEX };
		int rankD{ NO_TEX };
		int rankC{ NO_TEX };
		int rankB{ NO_TEX };
		int rankA{ NO_TEX };
		int rankS{ NO_TEX };

		// Score digits — shadow/outline (s-prefix) and fill layers are stacked.
		int scoreDigit[10]{ NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX };
		int scoreDigitFill[10]{ NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX };
		int scoreDigitPercent{ NO_TEX };
		int scoreDigitPlus{ NO_TEX };      // shadow layer of '+'
		int scoreDigitPlusFill{ NO_TEX };  // fill layer of '+'
		int scoreDigitMinus{ NO_TEX };
		int scoreDigitMinusFill{ NO_TEX };

		// Combo digits
		int comboDigit[10]{ NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX };
		int comboLabel{ NO_TEX };

		// Judgment
		int judgePerfect{ NO_TEX };
		int judgeGreat{ NO_TEX };
		int judgeGood{ NO_TEX };

		// Life bar (v3)
		int lifeBg{ NO_TEX };
		int lifeNormal{ NO_TEX };
		int lifeDanger{ NO_TEX };
		int lifeOverflow{ NO_TEX };
		int lifeDigit[10]{ NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX };
		int lifeDigitFill[10]{ NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX, NO_TEX };

		// All Perfect video path
		std::string apVideoPath;

		bool hasCore() const { return bar != NO_TEX && barBg != NO_TEX && comboDigit[0] != NO_TEX && judgePerfect != NO_TEX; }
		bool hasLife() const { return lifeBg != NO_TEX && lifeNormal != NO_TEX && lifeDigitFill[0] != NO_TEX; }
		bool hasAp() const { return !apVideoPath.empty(); }

		void load(const std::string& overlayDir);

		static const Texture* get(int index);

	private:
		static int loadOne(const std::string& path);
	};
}
