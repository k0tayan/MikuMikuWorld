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

		// Draw passes split by shader / blend mode.
		void drawJacketPass(Renderer* renderer, float vpWidth, float vpHeight,
		                    const Jacket& jacket);
		void drawAssetPass(Renderer* renderer, float vpWidth, float vpHeight);
		void drawAdditivePass(Renderer* renderer, float vpWidth, float vpHeight);
		void drawTextPass(Renderer* renderer, float vpWidth, float vpHeight,
		                  const ScoreContext& context);

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
		size_t nextIdx{ 0 };
		float lastScoreEpoch{ -1.f };

		// "+xxxx" score gain animation
		int   scoreDelta{ 0 };
		float scoreDeltaAge{ 1000.f };

		void buildTimeline(const Score& score);

		// Asset-based draw helpers
		void drawScoreBarAssets(Renderer* renderer, float sx, float sy);
		void drawComboAssets(Renderer* renderer, float sx, float sy);
		void drawJudgmentAsset(Renderer* renderer, float sx, float sy);
		void drawLifeAssets(Renderer* renderer, float sx, float sy);
		void drawApVideo(Renderer* renderer, float sx, float sy, float vpW, float vpH);

		// Fallback self-drawn helpers (used when asset pack is missing)
		void drawScoreBarFallback(Renderer* renderer, float sx, float sy);
		void drawComboTextFallback(Renderer* renderer, float sx, float sy);
		void drawJudgmentTextFallback(Renderer* renderer, float sx, float sy);
		void drawAllPerfectTextFallback(Renderer* renderer, float sx, float sy);
	};
}
