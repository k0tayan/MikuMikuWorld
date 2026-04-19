#pragma once
#include "OverlayAssets.h"
#include "OverlayText.h"
#include "VideoPlayer.h"
#include "../Rendering/Renderer.h"
#include "../MathUtils.h"
#include <string>
#include <vector>

namespace MikuMikuWorld
{
	struct Score;
	struct ScoreContext;
	class Jacket;

	struct OverlayIntroData
	{
		std::string difficulty{ "master" }; // easy/normal/hard/expert/master/append
		std::string extra;                   // optional level/tag text (e.g. "Lv.30")
		std::string title;
		std::string lyricist;
		std::string composer;
		std::string arranger;
		std::string vocal;
		std::string chartAuthor;
		bool useEnglish{ false };
	};

	class Overlay
	{
	public:
		Overlay();

		bool init(const std::string& fontPath,
		          const std::string& overlayDir,
		          const std::string& videoCacheDir);
		bool isInitialized() const { return text.isInitialized(); }
		bool hasAssetPack() const { return assets.hasCore(); }

		void onScoreChanged(const Score& score);
		void update(const Score& score, float currentTime, bool isPlaying);
		void reset();

		// Enable the pre-chart intro. `offsetSeconds` must match the delay applied
		// to music and notes (video time = chart time + offset).
		void beginIntro(float offsetSeconds, const OverlayIntroData& data);
		bool isIntroEnabled() const { return introOffset > 0.f; }
		bool isIntroShowing(float chartTime) const;

		// Draw passes split by shader / blend mode.
		void drawIntroPass(Renderer* renderer, float vpWidth, float vpHeight,
		                   const Jacket& jacket, float chartTime);
		void drawAssetPass(Renderer* renderer, float vpWidth, float vpHeight, float chartTime);
		void drawAdditivePass(Renderer* renderer, float vpWidth, float vpHeight);
		void drawTextPass(Renderer* renderer, float vpWidth, float vpHeight,
		                  const ScoreContext& context, float chartTime);

		bool isApPlaying() const { return allPerfectTriggered && apVideo.isOpen(); }

	private:
		struct ScoredNote
		{
			float time;
			float weight;
		};

		OverlayText text;
		OverlayAssets assets;
		VideoPlayer apVideo;

		std::vector<ScoredNote> timeline;
		float totalWeight{ 0.f };
		int   totalCombo{ 0 };
		int   currentCombo{ 0 };
		float currentScore{ 0.f };
		float judgmentFlashTimer{ 0.f };
		float allPerfectTimer{ 0.f };
		bool  allPerfectTriggered{ false };
		// Time of the last note hit. Used to delay the AP takeover so the
		// post-play moment breathes before the video kicks in.
		float fullComboTime{ -1.f };
		size_t nextIdx{ 0 };
		float lastScoreEpoch{ -1.f };

		// "+xxxx" score gain animation
		int   scoreDelta{ 0 };
		float scoreDeltaAge{ 1000.f };

		// Combo digit pop animation (resets to 0 whenever the count increments).
		float comboPopAge{ 1000.f };

		void buildTimeline(const Score& score);

		// Asset-based draw helpers
		void drawScoreBarAssets(Renderer* renderer, float sx, float sy);
		void drawComboAssets(Renderer* renderer, float sx, float sy);
		void drawJudgmentAsset(Renderer* renderer, float sx, float sy);
		void drawLifeAssets(Renderer* renderer, float sx, float sy);
		void drawApVideo(Renderer* renderer, float sx, float sy, float vpW, float vpH);

		// Intro (pre-chart) draw helpers
		float introOffset{ 0.f };
		OverlayIntroData introData;
		// Opacity multiplier applied to every HUD draw during the post-intro
		// fade-in (main .object [2]: transparency 100→0 over frames 300..395).
		// Updated at the start of drawAssetPass.
		float hudAlpha{ 1.f };
		void drawIntroBackground(Renderer* renderer, float sx, float sy, float videoTime);
		void drawIntroStartGrad(Renderer* renderer, float sx, float sy, float videoTime);
		void drawIntroCard(Renderer* renderer, float sx, float sy, float videoTime,
		                   const Jacket& jacket);
		void drawIntroText(Renderer* renderer, float sx, float sy, float videoTime);
	};
}
