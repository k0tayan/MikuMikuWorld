#pragma once
#include "../Rendering/Texture.h"
#include <string>

namespace MikuMikuWorld
{
	// Centralized loader for the optional Project Sekai overlay asset pack that
	// users drop into `res/overlay/` (not tracked in git). When the pack is
	// missing, Overlay falls back to minimal self-drawn rendering.
	class OverlayAssets
	{
	public:
		// Score bar
		Texture* barBg{ nullptr };
		Texture* barFg{ nullptr };
		Texture* bar{ nullptr };
		Texture* bars{ nullptr };
		Texture* scoreLabel{ nullptr };
		Texture* rankD{ nullptr };
		Texture* rankC{ nullptr };
		Texture* rankB{ nullptr };
		Texture* rankA{ nullptr };
		Texture* rankS{ nullptr };

		// Score digits (medium: s-prefixed set under score/rank/)
		Texture* scoreDigit[10]{};
		Texture* scoreDigitPercent{ nullptr };

		// Combo digits (large rainbow set)
		Texture* comboDigit[10]{};
		Texture* comboLabel{ nullptr };

		// Judgment
		Texture* judgePerfect{ nullptr };
		Texture* judgeGreat{ nullptr };
		Texture* judgeGood{ nullptr };

		// All Perfect video (path kept here; frame playback handled elsewhere)
		std::string apVideoPath;

		bool hasCore() const { return bar && bars && comboDigit[0] && judgePerfect; }
		bool hasAp() const { return !apVideoPath.empty(); }

		void load(const std::string& overlayDir);

	private:
		Texture* loadOne(const std::string& path);
	};
}
