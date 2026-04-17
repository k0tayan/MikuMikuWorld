#include "Overlay.h"
#include "../Score.h"
#include "../ScoreContext.h"
#include "../Jacket.h"
#include "../ResourceManager.h"
#include "../Rendering/Texture.h"
#include "../Constants.h"
#include "../Tempo.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace MikuMikuWorld
{
	namespace
	{
		constexpr float LAYOUT_WIDTH  = 1920.f;
		constexpr float LAYOUT_HEIGHT = 1080.f;

		constexpr float BAR_CENTER_X = 960.f;
		constexpr float BAR_Y        = 32.f;
		constexpr float BAR_WIDTH    = 1100.f;
		constexpr float BAR_HEIGHT   = 24.f;

		constexpr float COMBO_X      = 1780.f;
		constexpr float COMBO_Y      = 520.f;
		constexpr float COMBO_LABEL_Y = 620.f;

		constexpr float JACKET_X     = 36.f;
		constexpr float JACKET_Y     = 36.f;
		constexpr float JACKET_SIZE  = 180.f;
		constexpr float TITLE_X      = 232.f;
		constexpr float TITLE_Y      = 60.f;
		constexpr float ARTIST_Y     = 120.f;

		constexpr float JUDGE_X      = 960.f;
		constexpr float JUDGE_Y      = 720.f;
		constexpr float JUDGE_DURATION = 0.35f;

		constexpr float AP_X         = 960.f;
		constexpr float AP_Y         = 360.f;

		// Rank boundaries borrowed from pjsekai-overlay-APPEND (v3 gauge), normalized.
		constexpr float RANK_C = 746.f  / 1650.f;
		constexpr float RANK_B = 990.f  / 1650.f;
		constexpr float RANK_A = 1234.f / 1650.f;
		constexpr float RANK_S = 1478.f / 1650.f;
	}

	Overlay::Overlay() = default;

	bool Overlay::init(const std::string& fontPath)
	{
		return text.init(fontPath);
	}

	void Overlay::onScoreChanged(const Score& score)
	{
		buildTimeline(score);
		reset();
	}

	void Overlay::reset()
	{
		currentCombo = 0;
		currentScore = 0.f;
		judgmentFlashTimer = 0.f;
		allPerfectTimer = 0.f;
		allPerfectTriggered = false;
		nextIdx = 0;
	}

	void Overlay::buildTimeline(const Score& score)
	{
		timeline.clear();
		totalWeight = 0.f;
		totalCombo = 0;

		auto noteTime = [&](int tick) -> float {
			return accumulateDuration(tick, TICKS_PER_BEAT, score.tempoChanges);
		};

		for (const auto& [id, note] : score.notes)
		{
			float w = 0.f;
			const bool crit = note.critical;
			switch (note.getType())
			{
			case NoteType::Tap:
				if (note.friction) w = crit ? 0.2f : 0.1f;
				else if (note.isFlick()) w = crit ? 2.6f : 1.3f;
				else w = crit ? 2.f : 1.f;
				break;
			case NoteType::Hold:
			case NoteType::HoldEnd:
				if (note.isFlick()) w = crit ? 2.6f : 1.3f;
				else w = crit ? 2.f : 1.f;
				break;
			case NoteType::HoldMid:
				w = crit ? 0.2f : 0.1f;
				break;
			}

			if (w <= 0.f) continue;

			timeline.push_back({ noteTime(note.tick), w });
			totalWeight += w;
			++totalCombo;
		}

		std::sort(timeline.begin(), timeline.end(),
		          [](const ScoredNote& a, const ScoredNote& b) { return a.time < b.time; });
	}

	void Overlay::update(const Score& score, float currentTime, bool isPlaying)
	{
		// If the playhead jumped backward (user scrubbed or looped), rebuild progress.
		if (currentTime + 1e-3f < lastScoreEpoch)
		{
			currentCombo = 0;
			currentScore = 0.f;
			judgmentFlashTimer = 0.f;
			allPerfectTimer = 0.f;
			allPerfectTriggered = false;
			nextIdx = 0;
		}

		float prevTime = lastScoreEpoch;
		lastScoreEpoch = currentTime;

		if (!isPlaying)
			return;

		float dt = std::max(0.f, currentTime - std::max(prevTime, 0.f));

		while (nextIdx < timeline.size() && timeline[nextIdx].time <= currentTime)
		{
			const auto& n = timeline[nextIdx];
			++currentCombo;
			if (totalWeight > 0.f)
				currentScore = std::min(1.f, currentScore + n.weight / totalWeight);
			judgmentFlashTimer = 0.35f;
			++nextIdx;

			if (totalCombo > 0 && currentCombo >= totalCombo && !allPerfectTriggered)
			{
				allPerfectTriggered = true;
				allPerfectTimer = 0.f;
			}
		}

		if (judgmentFlashTimer > 0.f) judgmentFlashTimer = std::max(0.f, judgmentFlashTimer - dt);
		if (allPerfectTriggered) allPerfectTimer += dt;
	}

	void Overlay::drawScoreBar(Renderer* renderer, float sx, float sy)
	{
		const float barLeft = (BAR_CENTER_X - BAR_WIDTH * 0.5f) * sx;
		const float barTop  = BAR_Y * sy;
		const float barW    = BAR_WIDTH * sx;
		const float barH    = BAR_HEIGHT * sy;

		// Background
		text.drawSolidRect(renderer, barLeft, barTop, barW, barH,
		                   Color(0.f, 0.f, 0.f, 0.55f), 100);

		// Foreground fill
		const float ratio = std::clamp(currentScore, 0.f, 1.f);
		if (ratio > 0.f)
		{
			text.drawSolidRect(renderer, barLeft, barTop, barW * ratio, barH,
			                   Color(1.f, 0.95f, 0.35f, 0.9f), 101);
		}

		// Rank markers
		auto marker = [&](float pos) {
			const float x = barLeft + barW * pos;
			const float lineW = 2.f * sx;
			text.drawSolidRect(renderer, x - lineW * 0.5f, barTop - 2.f * sy,
			                   lineW, barH + 4.f * sy,
			                   Color(1.f, 1.f, 1.f, 0.6f), 102);
		};
		marker(RANK_C);
		marker(RANK_B);
		marker(RANK_A);
		marker(RANK_S);

		// Bar outline on top + bottom
		text.drawSolidRect(renderer, barLeft, barTop - 1.f * sy, barW, 1.f * sy,
		                   Color(1.f, 1.f, 1.f, 0.35f), 103);
		text.drawSolidRect(renderer, barLeft, barTop + barH, barW, 1.f * sy,
		                   Color(1.f, 1.f, 1.f, 0.35f), 103);
	}

	void Overlay::drawComboShapes(Renderer* /*renderer*/, float /*sx*/, float /*sy*/)
	{
		// No shape components for combo; the digit text does all the work.
	}

	void Overlay::drawComboTexts(Renderer* renderer, float sx, float sy)
	{
		if (currentCombo <= 0) return;

		char buf[16];
		std::snprintf(buf, sizeof(buf), "%d", currentCombo);

		const float digitScale = 108.f / 64.f * std::min(sx, sy);
		const Color digitColor(1.f, 1.f, 1.f, 0.95f);
		text.drawText(renderer, buf, COMBO_X * sx, COMBO_Y * sy,
		              digitScale, digitColor, 120, TextAlign::Right);

		const float labelScale = 36.f / 64.f * std::min(sx, sy);
		const Color labelColor(1.f, 1.f, 1.f, 0.7f);
		text.drawText(renderer, "COMBO", COMBO_X * sx, COMBO_LABEL_Y * sy,
		              labelScale, labelColor, 120, TextAlign::Right);
	}

	void Overlay::drawShapes(Renderer* renderer, float vpWidth, float vpHeight)
	{
		if (!isInitialized() || vpWidth <= 0.f || vpHeight <= 0.f) return;

		const float sx = vpWidth / LAYOUT_WIDTH;
		const float sy = vpHeight / LAYOUT_HEIGHT;

		// Jacket frame drawn as a shape so it appears even when no image is attached.
		const float fx = JACKET_X * sx;
		const float fy = JACKET_Y * sy;
		const float fw = JACKET_SIZE * sx;
		const float fh = JACKET_SIZE * sy;
		text.drawSolidRect(renderer, fx - 3.f * sx, fy - 3.f * sy,
		                   fw + 6.f * sx, fh + 6.f * sy,
		                   Color(1.f, 1.f, 1.f, 0.35f), 90);
		text.drawSolidRect(renderer, fx, fy, fw, fh,
		                   Color(0.f, 0.f, 0.f, 0.5f), 91);

		drawScoreBar(renderer, sx, sy);
		drawComboShapes(renderer, sx, sy);
	}

	void Overlay::drawTexts(Renderer* renderer, float vpWidth, float vpHeight,
	                        const ScoreContext& context)
	{
		if (!isInitialized() || vpWidth <= 0.f || vpHeight <= 0.f) return;

		const float sx = vpWidth / LAYOUT_WIDTH;
		const float sy = vpHeight / LAYOUT_HEIGHT;

		// Title / artist, drawn with clipped layout — long strings simply overflow;
		// the jacket area gives a visual bound.
		const float unit = std::min(sx, sy);
		const Color titleColor(1.f, 1.f, 1.f, 0.95f);
		const Color artistColor(1.f, 1.f, 1.f, 0.75f);

		if (!context.workingData.title.empty())
		{
			const float titleScale = 48.f / 64.f * unit;
			text.drawText(renderer, context.workingData.title,
			              TITLE_X * sx, TITLE_Y * sy,
			              titleScale, titleColor, 120, TextAlign::Left);
		}
		if (!context.workingData.artist.empty())
		{
			const float artistScale = 30.f / 64.f * unit;
			text.drawText(renderer, context.workingData.artist,
			              TITLE_X * sx, ARTIST_Y * sy,
			              artistScale, artistColor, 120, TextAlign::Left);
		}

		drawComboTexts(renderer, sx, sy);

		// Judgment flash: PERFECT fading out after each hit.
		if (judgmentFlashTimer > 0.f)
		{
			float alpha = judgmentFlashTimer / JUDGE_DURATION;
			alpha = std::clamp(alpha, 0.f, 1.f);
			const float jScale = 72.f / 64.f * unit;
			text.drawText(renderer, "PERFECT",
			              JUDGE_X * sx, JUDGE_Y * sy,
			              jScale, Color(1.f, 0.95f, 0.35f, alpha),
			              125, TextAlign::Center);
		}

		// ALL PERFECT: blinking after the last note cleared.
		if (allPerfectTriggered)
		{
			const float pulse = 0.5f + 0.5f * std::sin(allPerfectTimer * 6.283185f);
			const float hue = std::fmod(allPerfectTimer * 0.4f, 1.f);
			const float r = 0.5f + 0.5f * std::sin(hue * 6.283185f);
			const float g = 0.5f + 0.5f * std::sin(hue * 6.283185f + 2.094395f);
			const float b = 0.5f + 0.5f * std::sin(hue * 6.283185f + 4.188790f);
			const float apScale = 120.f / 64.f * unit;
			text.drawText(renderer, "ALL PERFECT",
			              AP_X * sx, AP_Y * sy,
			              apScale, Color(r, g, b, 0.55f + 0.45f * pulse),
			              130, TextAlign::Center);
		}
	}

	void Overlay::drawJacketPass(Renderer* renderer, float vpWidth, float vpHeight,
	                             const Jacket& jacket)
	{
		if (vpWidth <= 0.f || vpHeight <= 0.f) return;

		const int texId = jacket.getTexID();
		if (texId <= 0) return;

		int texIndex = -1;
		for (int i = 0; i < (int)ResourceManager::textures.size(); ++i)
		{
			if ((int)ResourceManager::textures[i].getID() == texId)
			{
				texIndex = i;
				break;
			}
		}
		if (texIndex < 0) return;
		const Texture& t = ResourceManager::textures[texIndex];

		const float sx = vpWidth / LAYOUT_WIDTH;
		const float sy = vpHeight / LAYOUT_HEIGHT;

		const float x = JACKET_X * sx;
		const float y = JACKET_Y * sy;
		const float w = JACKET_SIZE * sx;
		const float h = JACKET_SIZE * sy;

		renderer->drawRectangle({ x, y }, { w, h }, t,
		                        0.f, (float)t.getWidth(),
		                        0.f, (float)t.getHeight(),
		                        Color(1.f, 1.f, 1.f, 1.f), 92);
	}
}
