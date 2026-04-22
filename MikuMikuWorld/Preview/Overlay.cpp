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
#include <cctype>
#include <cmath>
#include <cstdio>
#include <utility>

namespace MikuMikuWorld
{
	namespace
	{
		constexpr float LAYOUT_WIDTH  = 1920.f;
		constexpr float LAYOUT_HEIGHT = 1080.f;

		// main2 .object combo parent (X=673.5, Y=-63.5) at 150% on 1920x1080.
		constexpr float COMBO_COMP_CX    = 1633.5f;
		constexpr float COMBO_COMP_CY    =  476.5f;
		constexpr float COMBO_POS_SCALE  = 1.5f;
		constexpr float COMBO_DIGIT_ADV  = 72.f;
		constexpr float COMBO_DIGIT_BASE = 0.70f;
		constexpr float COMBO_LABEL_SCALE = 0.67f;
		constexpr float COMBO_LABEL_TB_Y = -67.f;
		constexpr float COMBO_FPS        = 60.f;

		constexpr float JUDGE_X          = 960.f;
		constexpr float JUDGE_Y          = 667.5f;
		constexpr float JUDGE_FPS        = 60.f;
		constexpr float JUDGE_DURATION   = 20.f / JUDGE_FPS;

		constexpr float AP_VIDEO_DURATION = 10.0f;
		constexpr float AP_TRIGGER_DELAY  = 2.0f;

		// v3 gauge rank thresholds (fractions of the 1650px bar).
		constexpr float RANK_C = 746.f  / 1650.f;
		constexpr float RANK_B = 990.f  / 1650.f;
		constexpr float RANK_A = 1234.f / 1650.f;
		constexpr float RANK_S = 1478.f / 1650.f;

		void drawTexCentered(Renderer* r, const Texture* t, float cx, float cy,
		                     float w, float h, int z, float alpha = 1.f)
		{
			if (!t || w <= 0.f || h <= 0.f) return;
			r->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h },
			                 *t, 0.f, (float)t->getWidth(),
			                 0.f, (float)t->getHeight(),
			                 Color(1.f, 1.f, 1.f, alpha), z);
		}
	}

	Overlay::Overlay() = default;

	bool Overlay::init(const std::string& fontPath,
	                   const std::string& overlayDir,
	                   const std::string& videoCacheDir)
	{
		bool ok = text.init(fontPath);

		assets.load(overlayDir);
		if (assets.hasAp() && !videoCacheDir.empty())
			apVideo.open(assets.apVideoPath, videoCacheDir, 30, 960, 540);

		return ok;
	}

	bool Overlay::reloadFont(const std::string& fontPath)
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
		fullComboTime = -1.f;
		nextIdx = 0;
		scoreDelta = 0;
		scoreDeltaAge = 1000.f;
		comboPopAge = 1000.f;
		prevScore = 0.f;
		scoreAnimAge = 1000.f;
		comboBurstValue = 0;
		comboBurstAge = 1000.f;
	}

	void Overlay::buildTimeline(const Score& score)
	{
		// ScoreStats::calculateCombo と完全に一致するコンボ／スコア総量になるように組み立てる。
		// - ガイドホールドは全ノーツ除外
		// - Hidden 指定の start / end / mid 中継は除外
		// - ノーマルホールドは長さに応じて 1/8 拍刻みの tick コンボを追加
		timeline.clear();
		totalWeight = 0.f;
		totalCombo = 0;

		auto noteTime = [&](int tick) -> float {
			return accumulateDuration(tick, TICKS_PER_BEAT, score.tempoChanges);
		};

		auto weightForTap = [](const Note& note) -> float {
			const bool crit = note.critical;
			if (note.friction) return crit ? 0.2f : 0.1f;
			if (note.isFlick()) return crit ? 2.6f : 1.3f;
			return crit ? 2.f : 1.f;
		};

		auto weightForHoldEnd = [](const Note& note) -> float {
			const bool crit = note.critical;
			if (note.isFlick()) return crit ? 2.6f : 1.3f;
			return crit ? 2.f : 1.f;
		};

		auto pushEntry = [&](float time, float weight) {
			if (weight <= 0.f) return;
			timeline.push_back({ time, weight });
			totalWeight += weight;
			++totalCombo;
		};

		// 単独の Tap / Flick / Trace。
		for (const auto& [id, note] : score.notes)
		{
			if (note.getType() != NoteType::Tap) continue;
			pushEntry(noteTime(note.tick), weightForTap(note));
		}

		// ホールドは ScoreStats::calculateCombo と同じ規則で列挙する。
		constexpr int halfBeat = TICKS_PER_BEAT / 2;
		for (const auto& [id, hold] : score.holdNotes)
		{
			if (hold.isGuide()) continue;

			const Note& startNote = score.notes.at(id);
			const Note& endNote = score.notes.at(hold.end);

			if (hold.startType == HoldNoteType::Normal)
				pushEntry(noteTime(startNote.tick), weightForHoldEnd(startNote));

			for (const auto& step : hold.steps)
			{
				if (step.type == HoldStepType::Hidden) continue;
				const Note& midNote = score.notes.at(step.ID);
				const bool crit = midNote.critical;
				pushEntry(noteTime(midNote.tick), crit ? 0.2f : 0.1f);
			}

			if (hold.endType == HoldNoteType::Normal)
				pushEntry(noteTime(endNote.tick), weightForHoldEnd(endNote));

			// ScoreStats.cpp:78-93 の 1/8 拍刻み tick 加算を再現。
			const int startTick = startNote.tick;
			const int endTick = endNote.tick;
			int eighthTick = startTick + halfBeat;
			if (eighthTick % halfBeat)
				eighthTick -= (eighthTick % halfBeat);

			if (eighthTick == startTick || eighthTick == endTick)
				continue;

			int endCeil = endTick;
			if (endCeil % halfBeat)
				endCeil += halfBeat - (endCeil % halfBeat);

			const bool crit = startNote.critical;
			const float tickWeight = crit ? 0.2f : 0.1f;
			for (int t = eighthTick; t < endCeil; t += halfBeat)
				pushEntry(noteTime(t), tickWeight);
		}

		std::sort(timeline.begin(), timeline.end(),
		          [](const ScoredNote& a, const ScoredNote& b) { return a.time < b.time; });
	}

	void Overlay::update(const Score& score, float currentTime, bool isPlaying)
	{
		if (currentTime + 1e-3f < lastScoreEpoch)
			reset();

		float prevTime = lastScoreEpoch;
		lastScoreEpoch = currentTime;

		if (!isPlaying)
			return;

		float dt = std::max(0.f, currentTime - std::max(prevTime, 0.f));

		auto toDisplayScore = [](float ratio) {
			return (int)std::min(99999999.f, std::max(0.f, ratio * 1000000.f / 0.896f));
		};
		const int scoreBefore = toDisplayScore(currentScore);
		const float scoreRatioBefore = currentScore;
		bool anyNoteHit = false;

		while (nextIdx < timeline.size() && timeline[nextIdx].time <= currentTime)
		{
			const auto& n = timeline[nextIdx];
			++currentCombo;
			comboPopAge = 0.f;
			if (totalWeight > 0.f)
				currentScore = std::min(1.f, currentScore + n.weight / totalWeight);
			judgmentFlashTimer = JUDGE_DURATION;
			anyNoteHit = true;
			if (totalCombo > 0 && currentCombo > 0 && currentCombo % 100 == 0)
			{
				comboBurstValue = currentCombo;
				comboBurstAge = 0.f;
			}
			++nextIdx;
			if (totalCombo > 0 && currentCombo >= totalCombo && fullComboTime < 0.f)
				fullComboTime = n.time;
		}
		if (anyNoteHit)
		{
			prevScore = scoreRatioBefore;
			scoreAnimAge = 0.f;
		}

		// Delay AP takeover so the final combo has a moment to breathe.
		if (fullComboTime >= 0.f && !allPerfectTriggered
		    && currentTime - fullComboTime >= AP_TRIGGER_DELAY)
		{
			allPerfectTriggered = true;
			allPerfectTimer = 0.f;
		}

		const int scoreAfter = toDisplayScore(currentScore);
		if (scoreAfter > scoreBefore)
		{
			scoreDelta = scoreAfter - scoreBefore;
			scoreDeltaAge = 0.f;
		}
		scoreDeltaAge += dt;
		comboPopAge += dt;
		scoreAnimAge += dt;
		comboBurstAge += dt;

		if (judgmentFlashTimer > 0.f) judgmentFlashTimer = std::max(0.f, judgmentFlashTimer - dt);
		if (allPerfectTriggered) allPerfectTimer += dt;
	}

	void Overlay::drawScoreBarAssets(Renderer* renderer, float sx, float sy, float ox, float oy)
	{
		// sekai.obj2 @スコア at .exo parent (-583.5, -471) / 150% → canvas (376.5, 69).
		// Image scale = tempbuffer 0.2145 × parent 1.5 = 0.32175.
		constexpr float COMP_SCALE = 0.2145f * 1.5f;
		constexpr float COMP_CX    = 376.5f;
		constexpr float COMP_CY    = 69.f;

		const float bgCX = ox + COMP_CX * sx;
		const float bgCY = oy + COMP_CY * sy;

		if (const Texture* t = OverlayAssets::get(assets.barBg))
		{
			const float w = (float)t->getWidth()  * COMP_SCALE * sx;
			const float h = (float)t->getHeight() * COMP_SCALE * sy;
			renderer->drawRectangle({ bgCX - w * 0.5f, bgCY - h * 0.5f }, { w, h },
			                        *t, 0.f, (float)t->getWidth(),
			                        0.f, (float)t->getHeight(),
			                        Color(1.f, 1.f, 1.f, hudAlpha), 100);
		}

		// sekai.obj2 anim_score: cubic ease prev → current over 30 frames.
		const float animProgress = std::min(1.f, scoreAnimAge / 0.5f);
		const float animFactor   = (1.f - animProgress) * (1.f - animProgress) * (1.f - animProgress);
		const float animatedScore = currentScore - (currentScore - prevScore) * animFactor;
		const float ratio = std::clamp(animatedScore, 0.f, 1.f);
		if (ratio > 0.f)
		{
			if (const Texture* t = OverlayAssets::get(assets.bar))
			{
				const float barW = (float)t->getWidth()  * COMP_SCALE * sx;
				const float barH = (float)t->getHeight() * COMP_SCALE * sy;
				const float barCX = bgCX + 34.f * 1.5f * sx;
				const float barCY = bgCY - 3.f  * 1.5f * sy;
				const float barLeft = barCX - barW * 0.5f;
				const float barTop  = barCY - barH * 0.5f;

				const float fillW = barW * ratio;
				const float uvX2  = (float)t->getWidth() * ratio;
				renderer->drawRectangle({ barLeft, barTop }, { fillW, barH },
				                        *t, 0.f, uvX2,
				                        0.f, (float)t->getHeight(),
				                        Color(1.f, 1.f, 1.f, hudAlpha), 102);
			}
		}

		if (const Texture* t = OverlayAssets::get(assets.barFg))
		{
			const float w = (float)t->getWidth()  * COMP_SCALE * sx;
			const float h = (float)t->getHeight() * COMP_SCALE * sy;
			renderer->drawRectangle({ bgCX - w * 0.5f, bgCY - h * 0.5f }, { w, h },
			                        *t, 0.f, (float)t->getWidth(),
			                        0.f, (float)t->getHeight(),
			                        Color(1.f, 1.f, 1.f, hudAlpha), 104);
		}

		int rankTxtTex = assets.rankTxtD;
		{
			int rankTex = assets.rankD;
			if (ratio >= RANK_S) { rankTex = assets.rankS; rankTxtTex = assets.rankTxtS; }
			else if (ratio >= RANK_A) { rankTex = assets.rankA; rankTxtTex = assets.rankTxtA; }
			else if (ratio >= RANK_B) { rankTex = assets.rankB; rankTxtTex = assets.rankTxtB; }
			else if (ratio >= RANK_C) { rankTex = assets.rankC; rankTxtTex = assets.rankTxtC; }

			if (const Texture* t = OverlayAssets::get(rankTex))
			{
				constexpr float RANK_POS_SCALE = 1.5f;
				constexpr float RANK_IMAGE_SCALE = 0.22f * 1.5f;
				constexpr float RANK_TB_X = -188.f;
				constexpr float RANK_TB_Y =  -6.f;
				const float cx = bgCX + RANK_TB_X * RANK_POS_SCALE * sx;
				const float cy = bgCY + RANK_TB_Y * RANK_POS_SCALE * sy;
				const float w  = (float)t->getWidth()  * RANK_IMAGE_SCALE * sx;
				const float h  = (float)t->getHeight() * RANK_IMAGE_SCALE * sy;
				renderer->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h },
				                        *t, 0.f, (float)t->getWidth(),
				                        0.f, (float)t->getHeight(),
				                        Color(1.f, 1.f, 1.f, hudAlpha), 107);
			}
		}

		if (const Texture* t = OverlayAssets::get(rankTxtTex))
		{
			constexpr float TXT_POS_SCALE   = 1.5f;
			constexpr float TXT_IMAGE_SCALE = 0.34f * (22.f / 130.f) * 1.5f;
			constexpr float TXT_TB_X = -187.f;
			constexpr float TXT_TB_Y =   35.f;
			const float cx = bgCX + TXT_TB_X * TXT_POS_SCALE * sx;
			const float cy = bgCY + TXT_TB_Y * TXT_POS_SCALE * sy;
			const float w  = (float)t->getWidth()  * TXT_IMAGE_SCALE * sx;
			const float h  = (float)t->getHeight() * TXT_IMAGE_SCALE * sy;
			renderer->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h },
			                        *t, 0.f, (float)t->getWidth(),
			                        0.f, (float)t->getHeight(),
			                        Color(1.f, 1.f, 1.f, hudAlpha), 107);
		}

		// Zero-padded 8-digit score (00528711), normalised so a perfect chart = 1,000,000.
		const int displayScore = (int)std::min(99999999.f, std::max(0.f, animatedScore * 1000000.f / 0.896f));
		char sbuf[16];
		std::snprintf(sbuf, sizeof(sbuf), "%08d", displayScore);
		const int slen = (int)std::strlen(sbuf);

		constexpr float POS_SCALE    = 1.5f;
		constexpr float DIGIT_SCALE  = 0.65f * 1.5f;
		constexpr float DIGIT_ADV    = 22.f;
		constexpr float DIGIT_Y      = 26.f;
		constexpr float DIGIT_START_X = -127.f;

		auto drawDigitImage = [&](int texIdx, int colIndex, int z)
		{
			const Texture* t = OverlayAssets::get(texIdx);
			if (!t) return;
			const float tbX = DIGIT_START_X + DIGIT_ADV * colIndex;
			const float cx = bgCX + tbX * POS_SCALE * sx;
			const float cy = bgCY + DIGIT_Y * POS_SCALE * sy;
			const float w  = (float)t->getWidth()  * DIGIT_SCALE * sx;
			const float h  = (float)t->getHeight() * DIGIT_SCALE * sy;
			renderer->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h },
			                        *t, 0.f, (float)t->getWidth(),
			                        0.f, (float)t->getHeight(),
			                        Color(1.f, 1.f, 1.f, hudAlpha), z);
		};

		// sekai.obj2 draws both shadow and fill twice for density.
		for (int i = 0; i < slen; ++i)
		{
			int d = sbuf[i] - '0';
			if (d < 0 || d > 9) continue;
			drawDigitImage(assets.scoreDigit[d],     i, 105);
			drawDigitImage(assets.scoreDigit[d],     i, 105);
		}
		for (int i = 0; i < slen; ++i)
		{
			int d = sbuf[i] - '0';
			if (d < 0 || d > 9) continue;
			drawDigitImage(assets.scoreDigitFill[d], i, 106);
			drawDigitImage(assets.scoreDigitFill[d], i, 106);
		}

		// "+xxxx" score gain slide-in. Disappears at 30 frames without a fadeout.
		if (scoreDeltaAge < 0.5f && scoreDelta != 0)
		{
			const float progress = std::clamp(scoreDeltaAge / 0.2f, 0.f, 1.f);
			const float easedProgress = 1.f - std::pow(0.9f, progress * 12.f);
			const float alpha = std::clamp(1.3f * easedProgress, 0.f, 1.f);

			const bool negative = scoreDelta < 0;
			int absDelta = negative ? -scoreDelta : scoreDelta;
			char dbuf[16];
			std::snprintf(dbuf, sizeof(dbuf), "%d", absDelta);
			const int dlen = (int)std::strlen(dbuf);

			constexpr float DELTA_SCALE   = 0.42f * 1.5f;
			constexpr float DELTA_ADV     = 13.65f;
			constexpr float DELTA_Y       = 34.f;
			constexpr float DELTA_X_START = 26.25f;
			constexpr float DELTA_X_SLIDE = 47.f;
			const float deltaX = DELTA_X_START + DELTA_X_SLIDE * easedProgress;

			auto drawDeltaGlyph = [&](int texIdx, int colIndex, int z)
			{
				const Texture* t = OverlayAssets::get(texIdx);
				if (!t) return;
				const float tbX = deltaX + DELTA_ADV * colIndex;
				const float cx = bgCX + tbX * 1.5f * sx;
				const float cy = bgCY + DELTA_Y * 1.5f * sy;
				const float w  = (float)t->getWidth()  * DELTA_SCALE * sx;
				const float h  = (float)t->getHeight() * DELTA_SCALE * sy;
				renderer->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h },
				                        *t, 0.f, (float)t->getWidth(),
				                        0.f, (float)t->getHeight(),
				                        Color(1.f, 1.f, 1.f, alpha * hudAlpha), z);
			};

			const int signShadow = negative ? assets.scoreDigitMinus : assets.scoreDigitPlus;
			const int signFill   = negative ? assets.scoreDigitMinusFill : assets.scoreDigitPlusFill;

			drawDeltaGlyph(signShadow, 0, 108);
			drawDeltaGlyph(signShadow, 0, 108);
			for (int i = 0; i < dlen; ++i)
			{
				int d = dbuf[i] - '0';
				if (d < 0 || d > 9) continue;
				drawDeltaGlyph(assets.scoreDigit[d], i + 1, 108);
				drawDeltaGlyph(assets.scoreDigit[d], i + 1, 108);
			}
			drawDeltaGlyph(signFill, 0, 109);
			for (int i = 0; i < dlen; ++i)
			{
				int d = dbuf[i] - '0';
				if (d < 0 || d > 9) continue;
				drawDeltaGlyph(assets.scoreDigitFill[d], i + 1, 109);
			}
		}
	}

	void Overlay::drawComboAssets(Renderer* renderer, float sx, float sy, float ox, float oy)
	{
		if (currentCombo <= 0) return;

		const float compCX = ox + COMBO_COMP_CX * sx;
		const float compCY = oy + COMBO_COMP_CY * sy;

		// AP backglow pulse: period 1.5s, 0→1.
		const float apAlpha = std::min(1.f,
			(std::sin(lastScoreEpoch * 3.14159265f * 4.f / 3.f) + 1.f) * 0.5f);

		auto drawTag = [&](int texIdx, float tbY, int z, float alpha)
		{
			const Texture* t = OverlayAssets::get(texIdx);
			if (!t) return;
			const float w = (float)t->getWidth()  * COMBO_LABEL_SCALE * COMBO_POS_SCALE * sx;
			const float h = (float)t->getHeight() * COMBO_LABEL_SCALE * COMBO_POS_SCALE * sy;
			const float cx = compCX;
			const float cy = compCY + tbY * COMBO_POS_SCALE * sy;
			renderer->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h },
			                        *t, 0.f, (float)t->getWidth(),
			                        0.f, (float)t->getHeight(),
			                        Color(1.f, 1.f, 1.f, alpha * hudAlpha), z);
		};
		drawTag(assets.comboLabelGlow, -70.f, 109, apAlpha);
		drawTag(assets.comboLabel,     -67.f, 110, 1.f);

		char buf[16];
		std::snprintf(buf, sizeof(buf), "%d", currentCombo);
		const int len = (int)std::strlen(buf);

		const float progressFrames = comboPopAge * COMBO_FPS;
		const float shiftFax = std::min(1.f, progressFrames / 8.f * 0.5f + 0.5f);
		const float digitScale = COMBO_DIGIT_BASE * shiftFax;

		auto drawDigit = [&](int texIdx, int i, int z, float alpha)
		{
			const Texture* t = OverlayAssets::get(texIdx);
			if (!t) return;
			const float shift = -(len / 2.f) + (i + 1) - 0.5f;
			const float tbX = shift * COMBO_DIGIT_ADV * shiftFax;
			const float cx = compCX + tbX * COMBO_POS_SCALE * sx;
			const float cy = compCY;
			const float w  = (float)t->getWidth()  * digitScale * COMBO_POS_SCALE * sx;
			const float h  = (float)t->getHeight() * digitScale * COMBO_POS_SCALE * sy;
			renderer->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h },
			                        *t, 0.f, (float)t->getWidth(),
			                        0.f, (float)t->getHeight(),
			                        Color(1.f, 1.f, 1.f, alpha * hudAlpha), z);
		};

		for (int i = 0; i < len; ++i)
		{
			int d = buf[i] - '0';
			if (d < 0 || d > 9) continue;
			drawDigit(assets.comboDigitGlow[d], i, 111, apAlpha);
			drawDigit(assets.comboDigit[d],     i, 112, 1.f);
		}

		// Ghost pop-out layered on top of the static digits. shift_fax is
		// UNCLAMPED here, unlike the main pass, so the ghost grows past 1.0.
		if (progressFrames < 14.f)
		{
			const float ghostShiftFax = (progressFrames / 8.f) * 0.5f + 0.5f;
			const float ghostScale    = COMBO_DIGIT_BASE * ghostShiftFax;
			const float ghostAlpha    = std::clamp(1.f - progressFrames / 14.f, 0.f, 1.f);

			auto drawGhost = [&](int texIdx, int i, int z, float alpha)
			{
				const Texture* t = OverlayAssets::get(texIdx);
				if (!t) return;
				const float shift = -(len / 2.f) + (i + 1) - 0.5f;
				const float tbX = shift * COMBO_DIGIT_ADV * ghostShiftFax;
				const float cx = compCX + tbX * COMBO_POS_SCALE * sx;
				const float cy = compCY;
				const float w  = (float)t->getWidth()  * ghostScale * COMBO_POS_SCALE * sx;
				const float h  = (float)t->getHeight() * ghostScale * COMBO_POS_SCALE * sy;
				renderer->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h },
				                        *t, 0.f, (float)t->getWidth(),
				                        0.f, (float)t->getHeight(),
				                        Color(1.f, 1.f, 1.f, alpha * hudAlpha), z);
			};

			for (int i = 0; i < len; ++i)
			{
				int d = buf[i] - '0';
				if (d < 0 || d > 9) continue;
				drawGhost(assets.comboDigitGlow[d], i, 113, apAlpha * ghostAlpha);
				drawGhost(assets.comboDigit[d],     i, 114, ghostAlpha);
			}
		}

		// n00 milestone burst. Independent timer from the per-note pop.
		const float burstFrames = comboBurstAge * COMBO_FPS;
		if (comboBurstValue > 0 && burstFrames < 14.f)
		{
			char bbuf[16];
			std::snprintf(bbuf, sizeof(bbuf), "%d", comboBurstValue);
			const int blen = (int)std::strlen(bbuf);

			float burstShiftFax;
			float burstAlpha;
			if (burstFrames < 1.f)
			{
				burstShiftFax = 1.75f;
				burstAlpha    = 0.7f;
			}
			else
			{
				const float u = 1.077f - burstFrames / 14.f;
				burstShiftFax = 1.75f - u * u * u;
				burstAlpha    = std::max(0.f, 1.f - (burstFrames + 6.f) / 20.f);
			}
			const float burstDigitScale = COMBO_DIGIT_BASE * burstShiftFax;

			auto drawBurst = [&](int texIdx, int i, int z, float alpha)
			{
				const Texture* t = OverlayAssets::get(texIdx);
				if (!t) return;
				const float shift = -(blen / 2.f) + (i + 1) - 0.5f;
				const float tbX = shift * COMBO_DIGIT_ADV * burstShiftFax;
				const float cx = compCX + tbX * COMBO_POS_SCALE * sx;
				const float cy = compCY;
				const float w  = (float)t->getWidth()  * burstDigitScale * COMBO_POS_SCALE * sx;
				const float h  = (float)t->getHeight() * burstDigitScale * COMBO_POS_SCALE * sy;
				renderer->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h },
				                        *t, 0.f, (float)t->getWidth(),
				                        0.f, (float)t->getHeight(),
				                        Color(1.f, 1.f, 1.f, alpha * hudAlpha), z);
			};

			for (int i = 0; i < blen; ++i)
			{
				int d = bbuf[i] - '0';
				if (d < 0 || d > 9) continue;
				drawBurst(assets.comboDigitGlow[d], i, 113, apAlpha * burstAlpha);
				drawBurst(assets.comboDigit[d],     i, 114, burstAlpha);
			}
		}
	}

	void Overlay::drawLifeAssets(Renderer* renderer, float sx, float sy, float ox, float oy)
	{
		if (!assets.hasLife()) return;

		// sekai.obj2 @ライフ at .exo parent (705, -477.5) / 17.33% → canvas (1665, 62.5).
		constexpr float COMP_CX    = 1665.f;
		constexpr float COMP_CY    =   62.5f;
		constexpr float COMP_SCALE = 0.1733f;

		const float cx = ox + COMP_CX * sx;
		const float cy = oy + COMP_CY * sy;

		if (const Texture* t = OverlayAssets::get(assets.lifeBg))
		{
			const float w = (float)t->getWidth()  * COMP_SCALE * sx;
			const float h = (float)t->getHeight() * COMP_SCALE * sy;
			renderer->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h },
			                        *t, 0.f, (float)t->getWidth(),
			                        0.f, (float)t->getHeight(),
			                        Color(1.f, 1.f, 1.f, hudAlpha), 100);
		}

		// Preview assumes a perfect play, so life stays at max and the fill is unmasked.
		constexpr int lifeValue = 1000;

		if (const Texture* t = OverlayAssets::get(assets.lifeNormal))
		{
			const float w = (float)t->getWidth()  * COMP_SCALE * sx;
			const float h = (float)t->getHeight() * COMP_SCALE * sy;
			renderer->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h },
			                        *t, 0.f, (float)t->getWidth(),
			                        0.f, (float)t->getHeight(),
			                        Color(1.f, 1.f, 1.f, hudAlpha), 101);
		}

		char buf[16];
		std::snprintf(buf, sizeof(buf), "%d", lifeValue);
		const int dlen = (int)std::strlen(buf);

		constexpr float DIGIT_SCALE = 0.147f;
		constexpr float DIGIT_ADV   = 127.f;
		constexpr float DIGIT_TB_X  = 563.f;
		constexpr float DIGIT_TB_Y  = -145.f;

		auto drawDigit = [&](int texIdx, int rightIndex, int z)
		{
			const Texture* t = OverlayAssets::get(texIdx);
			if (!t) return;
			const float tbX = DIGIT_TB_X - rightIndex * DIGIT_ADV;
			const float glyphCX = cx + tbX * COMP_SCALE * sx;
			const float glyphCY = cy + DIGIT_TB_Y * COMP_SCALE * sy;
			const float w = (float)t->getWidth()  * DIGIT_SCALE * COMP_SCALE * sx;
			const float h = (float)t->getHeight() * DIGIT_SCALE * COMP_SCALE * sy;
			renderer->drawRectangle({ glyphCX - w * 0.5f, glyphCY - h * 0.5f }, { w, h },
			                        *t, 0.f, (float)t->getWidth(),
			                        0.f, (float)t->getHeight(),
			                        Color(1.f, 1.f, 1.f, hudAlpha), z);
		};

		for (int i = 0; i < dlen; ++i)
		{
			int d = buf[dlen - 1 - i] - '0';
			if (d < 0 || d > 9) continue;
			drawDigit(assets.lifeDigit[d], i, 102);
		}
		for (int i = 0; i < dlen; ++i)
		{
			int d = buf[dlen - 1 - i] - '0';
			if (d < 0 || d > 9) continue;
			drawDigit(assets.lifeDigitFill[d], i, 103);
		}
	}

	void Overlay::drawJudgmentAsset(Renderer* renderer, float sx, float sy, float ox, float oy)
	{
		if (judgmentFlashTimer <= 0.f) return;
		const Texture* t = OverlayAssets::get(assets.judgePerfect);
		if (!t) return;

		// sekai.obj2 @判定 pop easing. Parent 1.5x cancels the 2/3 tempbuffer scale.
		const float elapsed = JUDGE_DURATION - judgmentFlashTimer;
		const float progressFrames = elapsed * JUDGE_FPS;

		float scale = 1.f;
		float alpha = 1.f;
		if (progressFrames < 2.f)
		{
			alpha = 0.f;
		}
		else if (progressFrames < 5.f)
		{
			const float u = -1.45f + progressFrames * 0.25f;
			scale = 1.f - u * u * u * u;
		}

		if (alpha <= 0.f) return;

		const float w = (float)t->getWidth()  * sx * scale;
		const float h = (float)t->getHeight() * sy * scale;
		const float cx = ox + JUDGE_X * sx;
		const float cy = oy + JUDGE_Y * sy;
		drawTexCentered(renderer, t, cx, cy, w, h, 115, alpha * hudAlpha);
	}

	void Overlay::drawApVideo(Renderer* renderer, float sx, float sy, float ox, float oy)
	{
		if (!allPerfectTriggered || !apVideo.isOpen()) return;

		apVideo.setTime(allPerfectTimer);

		unsigned int texId = apVideo.getGLTexture();
		if (!texId) return;

		// 16:9 ソース動画をレイアウトに合わせて letterbox で配置。
		const float boxL = ox;
		const float boxT = oy;
		const float boxR = ox + LAYOUT_WIDTH  * sx;
		const float boxB = oy + LAYOUT_HEIGHT * sy;

		// Push a raw quad because the video texture is owned outside ResourceManager.
		std::array<DirectX::XMFLOAT4, 4> pos{
			DirectX::XMFLOAT4{ boxR, boxT, 0.f, 1.f },
			DirectX::XMFLOAT4{ boxR, boxB, 0.f, 1.f },
			DirectX::XMFLOAT4{ boxL, boxB, 0.f, 1.f },
			DirectX::XMFLOAT4{ boxL, boxT, 0.f, 1.f },
		};
		std::array<DirectX::XMFLOAT4, 4> uv{
			DirectX::XMFLOAT4{ 1.f, 0.f, 0.f, 0.f },
			DirectX::XMFLOAT4{ 1.f, 1.f, 0.f, 0.f },
			DirectX::XMFLOAT4{ 0.f, 1.f, 0.f, 0.f },
			DirectX::XMFLOAT4{ 0.f, 0.f, 0.f, 0.f },
		};
		DirectX::XMFLOAT4 color{ 1.f, 1.f, 1.f, 1.f };
		renderer->pushQuad(pos, uv, DirectX::XMMatrixIdentity(), color, (int)texId, 200);
	}

	void Overlay::beginIntro(float offsetSeconds, const OverlayIntroData& data)
	{
		introOffset = std::max(0.f, offsetSeconds);
		introData = data;
	}

	bool Overlay::isIntroShowing(float chartTime) const
	{
		if (introOffset <= 0.f) return false;
		const float videoTime = chartTime + introOffset;
		return videoTime >= 0.f && videoTime < 5.0f;
	}

	namespace
	{
		// AviUtl exo is center-origin with Y+ down.
		constexpr float LAYOUT_CX = LAYOUT_WIDTH * 0.5f;
		constexpr float LAYOUT_CY = LAYOUT_HEIGHT * 0.5f;

		inline float exoX(float v) { return LAYOUT_CX + v; }
		inline float exoY(float v) { return LAYOUT_CY + v; }

		inline float opacityFromTransparency(float t) { return std::clamp(1.f - t / 100.f, 0.f, 1.f); }

		// start_grad (.object [3][4]): Y 1500 → 0 over 120 frames, ease-out quadratic.
		constexpr float START_GRAD_SPAN_FRAMES = 120.f;
		float startGradY(float localFrames)
		{
			const float span = START_GRAD_SPAN_FRAMES;
			if (localFrames >= span) return 0.f;
			if (localFrames <= 0.f) return 1500.f;
			float u = localFrames / span;
			float eased = 1.f - (1.f - u) * (1.f - u);
			return 1500.f * (1.f - eased);
		}

		// Intro card fade-out (.object [6]): frame 270..299 linear α 1→0.
		float introFadeOutAlpha(float videoTime)
		{
			const float frame = videoTime * 60.f;
			if (frame < 270.f) return 1.f;
			if (frame >= 299.f) return 0.f;
			return 1.f - (frame - 270.f) / 29.f;
		}

		// Difficulty badge / text slide-in (.object [5]): X 43 → 0, Y -43 → 0 over 112 frames.
		constexpr float DIFF_SLIDE_SPAN_FRAMES = 112.f;
		struct ExoOffset { float x; float y; };
		ExoOffset diffBadgeSlideOffset(float videoTime)
		{
			const float span = DIFF_SLIDE_SPAN_FRAMES;
			const float frame = videoTime * 60.f;
			if (frame >= span) return { 0.f, 0.f };
			if (frame <= 0.f)  return { 43.f, -43.f };
			float u = frame / span;
			float eased = 1.f - (1.f - u) * (1.f - u);
			float factor = 1.f - eased;
			return { 43.f * factor, -43.f * factor };
		}
	}

	void Overlay::drawIntroBackground(Renderer* renderer, float vpWidth, float vpHeight)
	{
		// .object [1]: solid #68689c at α=0.80. HUD を letterbox しても裏のステージが
		// 透けないよう、背景色だけは viewport 全体に敷く。
		text.drawSolidRect(renderer, 0.f, 0.f,
		                   vpWidth, vpHeight,
		                   Color(0x68 / 255.f, 0x68 / 255.f, 0x9c / 255.f, 0.80f), 70);
	}

	void Overlay::drawIntroStartGrad(Renderer* renderer, float sx, float sy, float ox, float oy, float videoTime)
	{
		const Texture* t = OverlayAssets::get(assets.startGrad);
		if (!t) return;

		// Two waves slide Y=1500→0 at 透明度=90 (α=0.10) then hold.
		const float alpha = 0.10f * introFadeOutAlpha(videoTime);
		const float frame = videoTime * 60.f;

		auto drawWave = [&](float startFrame, float endFrame)
		{
			if (frame < startFrame || frame >= endFrame) return;
			const float local = frame - startFrame;
			const float yOffset = startGradY(local);

			const float w = (float)t->getWidth()  * 1.5f * sx;
			const float h = (float)t->getHeight() * 1.5f * sy;
			const float cx = ox + exoX(0.f) * sx;
			const float cy = oy + exoY(yOffset) * sy;
			renderer->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h }, *t,
			                        0.f, (float)t->getWidth(),
			                        0.f, (float)t->getHeight(),
			                        Color(1.f, 1.f, 1.f, alpha), 72);
		};

		drawWave(60.f, 180.f);
		drawWave(180.f, 300.f);
	}

	void Overlay::drawIntroCard(Renderer* renderer, float sx, float sy, float ox, float oy,
	                            float videoTime, const Jacket& jacket)
	{
		const float alpha = introFadeOutAlpha(videoTime);
		if (alpha <= 0.f) return;

		// .object [8]: difficulty badge at (-661.5, 288), 39.26%.
		if (const Texture* t = OverlayAssets::get(assets.difficultyBg(introData.difficulty)))
		{
			const auto off = diffBadgeSlideOffset(videoTime);
			const float target = 1024.f * 0.3926f;
			const float w = target * sx;
			const float h = target * sy;
			const float cx = ox + exoX(-661.5f + off.x) * sx;
			const float cy = oy + exoY(288.f   + off.y) * sy;
			renderer->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h }, *t,
			                        0.f, (float)t->getWidth(),
			                        0.f, (float)t->getHeight(),
			                        Color(1.f, 1.f, 1.f, alpha), 90);
		}

		// ジャケットはテキストパス後に drawIntroJacketOverlayPass で最前面に再描画するので、
		// ここでは描画しない。
		(void)jacket;
	}

	void Overlay::drawIntroText(Renderer* renderer, float sx, float sy, float ox, float oy, float videoTime)
	{
		const float alpha = introFadeOutAlpha(videoTime);
		if (alpha <= 0.f) return;

		const float unit = std::min(sx, sy);
		const Color white(1.f, 1.f, 1.f, alpha);

		// .object [12]: difficulty text at (-855, 486), size 84 × 38%, align=6 (左下)。
		// exoのYは下端基準なので、スクリーン座標で行高ぶん上にずらして drawText の top に揃える。
		{
			std::string up;
			up.reserve(introData.difficulty.size());
			for (char c : introData.difficulty) up.push_back((char)std::toupper((unsigned char)c));
			const auto off = diffBadgeSlideOffset(videoTime);
			const float scale = 84.f * 0.38f / 64.f * unit;
			const float screenX = ox + exoX(-855.f + off.x) * sx;
			const float screenY = oy + exoY(486.f + off.y) * sy - text.getLineHeight(scale);
			text.drawText(renderer, up, screenX, screenY, scale, white, 122, TextAlign::Left);
		}

		// .object [15]: extra/level text (e.g. "Lv. 30"), size 72 × 38%, align=0 (左上)。
		if (!introData.extra.empty())
		{
			const float scale = 72.f * 0.38f / 64.f * unit;
			text.drawText(renderer, introData.extra,
			              ox + exoX(-380.f) * sx, oy + exoY(200.f) * sy,
			              scale, white, 122, TextAlign::Left);
		}

		// .object [17]: title at (-378.5, 324), size 96 × 40%, align=6 (左下)。
		// 右端 canvas X=1920 付近で切れるのを避けるため折り返す。タイトル左端
		// (canvas X=581.5) はジャケット右端 (canvas X=543.6) より右にあるため、
		// 追加行は上方向に伸びるだけでジャケットとは横方向に干渉しない。
		if (!introData.title.empty())
		{
			const float scale = 96.f * 0.40f / 64.f * unit;
			const float screenX = ox + exoX(-378.5f) * sx;
			const float screenBottomY = oy + exoY(324.f) * sy;
			constexpr float TITLE_RIGHT_MARGIN = 60.f;
			const float maxLineWidth = (LAYOUT_WIDTH - TITLE_RIGHT_MARGIN - exoX(-378.5f)) * sx;
			const auto lines = text.wrapLines(introData.title, scale, maxLineWidth);
			const float lineH = text.getLineHeight(scale);
			for (size_t li = 0; li < lines.size(); ++li)
			{
				const float topY = screenBottomY - lineH * static_cast<float>(lines.size() - li);
				text.drawText(renderer, lines[li], screenX, topY, scale, white, 122, TextAlign::Left);
			}
		}

		// .object [16][18]: description lines.
		auto formatDescription = [&]() -> std::pair<std::string, std::string>
		{
			auto dashIfEmpty = [](const std::string& s) { return s.empty() ? "-" : s; };
			const std::string& lyr = introData.lyricist;
			const std::string& cmp = introData.composer;
			const std::string& arr = introData.arranger;
			const std::string& voc = introData.vocal;
			const std::string& aut = introData.chartAuthor;
			if (introData.useEnglish)
			{
				return {
					"Lyrics: " + dashIfEmpty(lyr) + "    Music: " + dashIfEmpty(cmp) + "    Arrangement: " + dashIfEmpty(arr),
					"Vocals: " + dashIfEmpty(voc) + "    Chart Design: " + dashIfEmpty(aut),
				};
			}
			return {
				"作詞：" + dashIfEmpty(lyr) + "    作曲：" + dashIfEmpty(cmp) + "    編曲：" + dashIfEmpty(arr),
				"Vo：" + dashIfEmpty(voc) + "    譜面制作：" + dashIfEmpty(aut),
			};
		};

		auto [desc1, desc2] = formatDescription();
		const float descScale = 27.f / 64.f * unit;
		const float descLineH = text.getLineHeight(descScale);
		const float descScreenX = ox + exoX(-380.f) * sx;
		constexpr float DESC_RIGHT_MARGIN = 60.f;
		const float descMaxW = (LAYOUT_WIDTH - DESC_RIGHT_MARGIN - exoX(-380.f)) * sx;

		const auto desc1Lines = text.wrapLines(desc1, descScale, descMaxW);
		const float desc1TopY = oy + exoY(364.5f) * sy;
		for (size_t li = 0; li < desc1Lines.size(); ++li)
		{
			text.drawText(renderer, desc1Lines[li], descScreenX,
			              desc1TopY + descLineH * static_cast<float>(li),
			              descScale, white, 122, TextAlign::Left);
		}

		// desc2 は上詰め。desc1 が折り返した分だけ下にずらして重なりを避ける。
		const auto desc2Lines = text.wrapLines(desc2, descScale, descMaxW);
		const float desc2PushDown = desc1Lines.size() > 1
			? descLineH * static_cast<float>(desc1Lines.size() - 1)
			: 0.f;
		const float desc2TopY = oy + exoY(413.f) * sy + desc2PushDown;
		for (size_t li = 0; li < desc2Lines.size(); ++li)
		{
			text.drawText(renderer, desc2Lines[li], descScreenX,
			              desc2TopY + descLineH * static_cast<float>(li),
			              descScale, white, 122, TextAlign::Left);
		}
	}

	namespace
	{
		// HUD は 1920x1080 レイアウトのアスペクト比を維持する。viewport のアスペクトと
		// 合わないときは uniform scale + letterbox/pillarbox で中央寄せする。
		struct HudLayout { float scale; float ox; float oy; };
		HudLayout computeHudLayout(float vpWidth, float vpHeight)
		{
			const float scale = std::min(vpWidth / LAYOUT_WIDTH, vpHeight / LAYOUT_HEIGHT);
			const float ox = (vpWidth  - LAYOUT_WIDTH  * scale) * 0.5f;
			const float oy = (vpHeight - LAYOUT_HEIGHT * scale) * 0.5f;
			return { scale, ox, oy };
		}
	}

	void Overlay::drawIntroPass(Renderer* renderer, float vpWidth, float vpHeight,
	                            const Jacket& jacket, float chartTime)
	{
		if (vpWidth <= 0.f || vpHeight <= 0.f) return;
		if (!isIntroShowing(chartTime)) return;

		const auto layout = computeHudLayout(vpWidth, vpHeight);
		const float videoTime = chartTime + introOffset;

		drawIntroBackground(renderer, vpWidth, vpHeight);
		drawIntroStartGrad(renderer, layout.scale, layout.scale, layout.ox, layout.oy, videoTime);
		drawIntroCard(renderer, layout.scale, layout.scale, layout.ox, layout.oy, videoTime, jacket);
	}

	void Overlay::drawAssetPass(Renderer* renderer, float vpWidth, float vpHeight, float chartTime)
	{
		if (vpWidth <= 0.f || vpHeight <= 0.f) return;
		const auto layout = computeHudLayout(vpWidth, vpHeight);

		// Post-intro HUD fade-in (.object [2]): frames 300..395.
		hudAlpha = 1.f;
		if (introOffset > 0.f)
		{
			const float videoFrame = (chartTime + introOffset) * 60.f;
			if (videoFrame < 300.f)       hudAlpha = 0.f;
			else if (videoFrame < 395.f)  hudAlpha = (videoFrame - 300.f) / 95.f;
		}
		if (hudAlpha <= 0.f) return;

		if (assets.hasCore())
		{
			if (isApPlaying())
			{
				// .object [26]: black α=0.5 dim behind the AP video. Skips other HUD.
				text.drawSolidRect(renderer, 0.f, 0.f,
				                   vpWidth, vpHeight,
				                   Color(0.f, 0.f, 0.f, 0.5f), 150);
				return;
			}
			drawScoreBarAssets(renderer, layout.scale, layout.scale, layout.ox, layout.oy);
			drawComboAssets(renderer, layout.scale, layout.scale, layout.ox, layout.oy);
			drawLifeAssets(renderer, layout.scale, layout.scale, layout.ox, layout.oy);
			drawJudgmentAsset(renderer, layout.scale, layout.scale, layout.ox, layout.oy);
		}
	}

	void Overlay::drawAdditivePass(Renderer* renderer, float vpWidth, float vpHeight)
	{
		if (vpWidth <= 0.f || vpHeight <= 0.f) return;
		const auto layout = computeHudLayout(vpWidth, vpHeight);
		drawApVideo(renderer, layout.scale, layout.scale, layout.ox, layout.oy);
	}

	void Overlay::drawTextPass(Renderer* renderer, float vpWidth, float vpHeight,
	                           const ScoreContext& context, float chartTime)
	{
		(void)context;
		if (!isInitialized() || vpWidth <= 0.f || vpHeight <= 0.f) return;
		const auto layout = computeHudLayout(vpWidth, vpHeight);

		if (isIntroShowing(chartTime))
		{
			const float videoTime = chartTime + introOffset;
			drawIntroText(renderer, layout.scale, layout.scale, layout.ox, layout.oy, videoTime);
		}

		// .object [29][30]: α 0→1 black fade-in over AP-local 4.52..5.25s, then hold.
		if (isApPlaying())
		{
			constexpr float FADE_START = 271.f / 60.f;
			constexpr float FADE_END   = 315.f / 60.f;
			if (allPerfectTimer >= FADE_START)
			{
				float alpha = 1.f;
				if (allPerfectTimer < FADE_END)
					alpha = (allPerfectTimer - FADE_START) / (FADE_END - FADE_START);
				text.drawSolidRect(renderer, 0.f, 0.f,
				                   vpWidth, vpHeight,
				                   Color(0.f, 0.f, 0.f, std::clamp(alpha, 0.f, 1.f)), 250);
			}
		}
	}

	void Overlay::drawIntroJacketOverlayPass(Renderer* renderer, float vpWidth, float vpHeight,
	                                         const Jacket& jacket, float chartTime)
	{
		if (vpWidth <= 0.f || vpHeight <= 0.f) return;
		if (!isIntroShowing(chartTime)) return;

		const Texture* t = jacket.getTexture();
		if (!t) return;

		const auto layout = computeHudLayout(vpWidth, vpHeight);
		const float videoTime = chartTime + introOffset;
		const float alpha = introFadeOutAlpha(videoTime);
		if (alpha <= 0.f) return;

		// .object [23]: jacket at (-618, 245), 78.75% of a 512x512 source.
		const float sx = layout.scale;
		const float sy = layout.scale;
		const float target = 512.f * 0.7875f;
		const float w = target * sx;
		const float h = target * sy;
		const float cx = layout.ox + exoX(-618.f) * sx;
		const float cy = layout.oy + exoY(245.f)  * sy;
		renderer->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h }, *t,
		                        0.f, (float)t->getWidth(),
		                        0.f, (float)t->getHeight(),
		                        Color(1.f, 1.f, 1.f, alpha), 130);
	}
}
