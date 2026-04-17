#pragma once
#include "OverlayText.h"
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

		bool init(const std::string& fontPath);
		bool isInitialized() const { return text.isInitialized(); }

		void onScoreChanged(const Score& score);
		void update(const Score& score, float currentTime, bool isPlaying);
		void reset();

		void drawJacketPass(Renderer* renderer, float vpWidth, float vpHeight,
		                    const Jacket& jacket);
		void drawShapes(Renderer* renderer, float vpWidth, float vpHeight);
		void drawTexts(Renderer* renderer, float vpWidth, float vpHeight,
		               const ScoreContext& context);

	private:
		struct ScoredNote
		{
			float time;
			float weight;
		};

		OverlayText text;
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

		static void addNoteScore(ScoredNote n, std::vector<ScoredNote>& dst);
		void buildTimeline(const Score& score);

		void drawScoreBar(Renderer* renderer, float sx, float sy);
		void drawComboShapes(Renderer* renderer, float sx, float sy);
		void drawComboTexts(Renderer* renderer, float sx, float sy);
	};
}
