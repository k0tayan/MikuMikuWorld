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

		// Combo parent at .exo (X=673.5, Y=-62.5) with 拡大率 150% on 1920x1080
		//   → canvas center (960+673.5, 540-62.5) = (1633.5, 477.5)
		constexpr float COMBO_COMP_CX    = 1633.5f;
		constexpr float COMBO_COMP_CY    =  477.5f;
		constexpr float COMBO_POS_SCALE  = 1.5f;   // tempbuffer -> canvas pixel
		constexpr float COMBO_DIGIT_ADV  = 72.f;   // tempbuffer digit advance
		constexpr float COMBO_DIGIT_BASE = 0.70f;  // obj.draw scale for digits
		constexpr float COMBO_LABEL_SCALE = 0.67f; // obj.draw scale for nt.png
		constexpr float COMBO_LABEL_TB_Y = -67.f;  // tempbuffer Y offset for tag
		constexpr float COMBO_FPS        = 60.f;

		// Parent object at (X=0, Y=127.5) with 拡大率 150% on 1920x1080 canvas
		//   → canvas center (960, 540+127.5) = (960, 667.5).
		// Script draws the image at obj.draw(0, 0, 0, 2/3) in a tempbuffer, so
		// on-canvas pixels = image pixels * (2/3) * 1.5 = image pixels * 1.0.
		// Lifetime is 20 frames at the rendering framerate (60fps reference)
		// split into: 0-1 invisible, 2-4 scale pop, 5-19 stable.
		constexpr float JUDGE_X          = 960.f;
		constexpr float JUDGE_Y          = 667.5f;
		constexpr float JUDGE_FPS        = 60.f;
		constexpr float JUDGE_DURATION   = 20.f / JUDGE_FPS;

		constexpr float AP_VIDEO_DURATION = 10.0f; // typical ap.mp4 length
		constexpr float AP_TRIGGER_DELAY  = 2.0f;  // pause after the last note

		// Rank boundary ratios (v3 gauge)
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

		while (nextIdx < timeline.size() && timeline[nextIdx].time <= currentTime)
		{
			const auto& n = timeline[nextIdx];
			++currentCombo;
			comboPopAge = 0.f;
			if (totalWeight > 0.f)
				currentScore = std::min(1.f, currentScore + n.weight / totalWeight);
			judgmentFlashTimer = JUDGE_DURATION;
			++nextIdx;
			if (totalCombo > 0 && currentCombo >= totalCombo && fullComboTime < 0.f)
				fullComboTime = n.time;
		}

		// Wait AP_TRIGGER_DELAY seconds after the last note before the AP
		// takeover starts, so the final combo has a moment to breathe.
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

		if (judgmentFlashTimer > 0.f) judgmentFlashTimer = std::max(0.f, judgmentFlashTimer - dt);
		if (allPerfectTriggered) allPerfectTimer += dt;
	}

	// ---------------------------------------------------------------------
	// Asset-based drawing (used when res/overlay/ is populated)
	// ---------------------------------------------------------------------

	void Overlay::drawScoreBarAssets(Renderer* renderer, float sx, float sy)
	{
		// Layout derived from pjsekai-overlay-APPEND sekai.obj2 @スコア group:
		//   obj.setoption("drawtarget", "tempbuffer", x_boundary, 180)
		//   obj.draw(0, 0, 0, 0.2145)                 -- bg at composite scale 21.45%
		//   obj.draw(34, -3, 0, 0.2145)               -- bar shifted (+34, -3)
		//   obj.draw(0, 0, 0, 0.2145)                 -- fg overlay
		// Parent (.exo) positions the composite at (-583.5, -471) with 150% scale
		// in a 1920x1080 canvas whose (0,0) is the center → screen center (376.5, 69).
		constexpr float COMP_SCALE = 0.2145f * 1.5f;   // 0.32175
		constexpr float COMP_CX    = 376.5f;
		constexpr float COMP_CY    = 69.f;

		const float bgCX = COMP_CX * sx;
		const float bgCY = COMP_CY * sy;

		// bg.png (2069x446) — full bar frame with SCORE label and dark slot
		if (const Texture* t = OverlayAssets::get(assets.barBg))
		{
			const float w = (float)t->getWidth()  * COMP_SCALE * sx;
			const float h = (float)t->getHeight() * COMP_SCALE * sy;
			renderer->drawRectangle({ bgCX - w * 0.5f, bgCY - h * 0.5f }, { w, h },
			                        *t, 0.f, (float)t->getWidth(),
			                        0.f, (float)t->getHeight(),
			                        Color(1.f, 1.f, 1.f, hudAlpha), 100);
		}

		// bar.png (1650x76) — gradient fill, masked by currentScore from the left.
		// Position offset (+34, -3) is in tempbuffer coordinates which project
		// to canvas via the .exo's 1.5x scale, independent from the 0.2145 image scale.
		const float ratio = std::clamp(currentScore, 0.f, 1.f);
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

		// fg.png (2069x446) — rank letter glyphs positioned above the bar track
		if (const Texture* t = OverlayAssets::get(assets.barFg))
		{
			const float w = (float)t->getWidth()  * COMP_SCALE * sx;
			const float h = (float)t->getHeight() * COMP_SCALE * sy;
			renderer->drawRectangle({ bgCX - w * 0.5f, bgCY - h * 0.5f }, { w, h },
			                        *t, 0.f, (float)t->getWidth(),
			                        0.f, (float)t->getHeight(),
			                        Color(1.f, 1.f, 1.f, hudAlpha), 104);
		}

		// Big rank letter in the dark panel on the left.
		// sekai.obj2 draws score/rank/chr/{rank}.png at tempbuffer (-188, -6)
		// with scale 0.22, then the tempbuffer is placed at 1.5x.
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

		// "SCORE RANK" label beneath the big rank letter.
		// sekai.obj2: obj.draw(-187, 35, 0, 0.34 * (22/130)).
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

		// Numeric score display. In pjsekai's tempbuffer the leftmost digit sits
		// at (-127, 26) and each digit advances +22 in tempbuffer units, drawn at
		// a within-tempbuffer scale of 0.65. Tempbuffer is then placed on canvas
		// with a 1.5x object scale, so:
		//   canvas_pos  = composite_center + tempbuffer_xy * 1.5
		//   canvas_size = raw_pixels * 0.65 * 1.5
		// pjsekai displays scores as zero-padded 8-digit numbers (e.g. 00528711).
		const int displayScore = (int)std::min(99999999.f, std::max(0.f, currentScore * 1000000.f / 0.896f));
		char sbuf[16];
		std::snprintf(sbuf, sizeof(sbuf), "%08d", displayScore);
		const int slen = (int)std::strlen(sbuf);

		constexpr float POS_SCALE    = 1.5f;             // tempbuffer -> canvas pixel
		constexpr float DIGIT_SCALE  = 0.65f * 1.5f;     // image pixel scale on canvas
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

		// Shadow (s-prefix) pass twice for density, then the fill pass on top.
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
		}

		// "+xxxx" score gain animation: slide-in and fade-in to the right of
		// the main score, disappear after ~0.5s. Matches sekai.obj2 @スコア
		// numbers: digit scale 0.42, advance 13.65, y=+34 in tempbuffer.
		if (scoreDeltaAge < 0.5f && scoreDelta != 0)
		{
			const float progress = std::clamp(scoreDeltaAge / 0.2f, 0.f, 1.f);
			const float easedProgress = 1.f - std::pow(0.9f, progress * 12.f);
			float alpha = std::clamp(1.3f * easedProgress, 0.f, 1.f);
			// Fade out in the last portion of the lifetime.
			if (scoreDeltaAge > 0.35f)
				alpha *= std::max(0.f, 1.f - (scoreDeltaAge - 0.35f) / 0.15f);

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

			// First glyph is the sign, followed by digits.
			const int signShadow = negative ? assets.scoreDigitMinus : assets.scoreDigitPlus;
			const int signFill   = negative ? assets.scoreDigitMinusFill : assets.scoreDigitPlusFill;

			// Shadow pass (twice) then fill pass.
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

	void Overlay::drawComboAssets(Renderer* renderer, float sx, float sy)
	{
		if (currentCombo <= 0) return;

		const float compCX = COMBO_COMP_CX * sx;
		const float compCY = COMBO_COMP_CY * sy;

		// AP pulse — math.min(1, (sin(time * pi * 4/3) + 1) / 2). Pulses 0→1 with
		// period 1.5s. Used as the alpha of the backglow / label glow layers.
		const float apAlpha = std::min(1.f,
			(std::sin(lastScoreEpoch * 3.14159265f * 4.f / 3.f) + 1.f) * 0.5f);

		// "COMBO" tag — AP branch draws pe.png (glow) under pt.png.
		// pe:  obj.draw(0, -70, 0, 0.67, ap_alpha)
		// pt:  obj.draw(0, -67, 0, 0.67)
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

		// Digits — obj.draw(shift * 72 * shift_fax, 0, 0, 0.70 * shift_fax).
		//   shift      = -(len/2) + i - 0.5  (i = 1..len, 1-indexed in the script)
		//   shift_fax  = min(1, progress_frames/8 * 0.5 + 0.5), grows 0.5 → 1.0
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
	}

	void Overlay::drawLifeAssets(Renderer* renderer, float sx, float sy)
	{
		if (!assets.hasLife()) return;

		// Layout derived from pjsekai-overlay-APPEND main_jp_16-9_1920x1080.exo
		// + sekai.obj2 @ライフ block:
		//   parent object at (X=705, Y=-477.5) with 拡大率 17.33% on 1920x1080
		//     → canvas center (960+705, 540-477.5) = (1665, 62.5)
		//   script draws everything inside the tempbuffer at scale 1, so the
		//   tempbuffer pixel → canvas pixel factor is exactly 0.1733.
		constexpr float COMP_CX    = 1665.f;
		constexpr float COMP_CY    =   62.5f;
		constexpr float COMP_SCALE = 0.1733f;

		const float cx = COMP_CX * sx;
		const float cy = COMP_CY * sy;

		// bg.png (2560x600) — empty life frame / "LIFE" label
		if (const Texture* t = OverlayAssets::get(assets.lifeBg))
		{
			const float w = (float)t->getWidth()  * COMP_SCALE * sx;
			const float h = (float)t->getHeight() * COMP_SCALE * sy;
			renderer->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h },
			                        *t, 0.f, (float)t->getWidth(),
			                        0.f, (float)t->getHeight(),
			                        Color(1.f, 1.f, 1.f, hudAlpha), 100);
		}

		// Preview simulates a perfect play, so life stays at max (1000/1000)
		// and the fill graphic is drawn unmasked — covering the full bg track.
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

		// Digit string: right-aligned at tempbuffer (563, -145), advancing
		// -127 per additional digit, drawn at image-scale 0.147 in tempbuffer.
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

		// Shadow (s-prefix) pass then fill pass on top.
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

	void Overlay::drawJudgmentAsset(Renderer* renderer, float sx, float sy)
	{
		if (judgmentFlashTimer <= 0.f) return;
		const Texture* t = OverlayAssets::get(assets.judgePerfect);
		if (!t) return;

		// sekai.obj2 @判定: obj.draw uses zoom = (2/3) * factor where
		//   factor(progress) = 1 - (-1.45 + progress/4)^4 in phase 2,
		//   factor = 1          in phase 3,
		// and alpha = 0 before phase 2. Parent scale 1.5 cancels the 2/3, so
		// on-canvas scale equals factor directly.
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
		const float cx = JUDGE_X * sx;
		const float cy = JUDGE_Y * sy;
		drawTexCentered(renderer, t, cx, cy, w, h, 115, alpha * hudAlpha);
	}

	void Overlay::drawApVideo(Renderer* renderer, float /*sx*/, float /*sy*/,
	                          float vpW, float vpH)
	{
		if (!allPerfectTriggered || !apVideo.isOpen()) return;

		apVideo.setTime(allPerfectTimer);

		unsigned int texId = apVideo.getGLTexture();
		if (!texId) return;

		// Draw as a full-screen quad. We bypass the Texture wrapper and push
		// a raw quad with the video's GL texture.
		std::array<DirectX::XMFLOAT4, 4> pos{
			DirectX::XMFLOAT4{ vpW, 0.f,   0.f, 1.f },
			DirectX::XMFLOAT4{ vpW, vpH,   0.f, 1.f },
			DirectX::XMFLOAT4{ 0.f, vpH,   0.f, 1.f },
			DirectX::XMFLOAT4{ 0.f, 0.f,   0.f, 1.f },
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

	// ---------------------------------------------------------------------
	// Pass entry points
	// ---------------------------------------------------------------------

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
		// AviUtl exo → screen pixels (center origin, Y+ = down)
		constexpr float LAYOUT_CX = LAYOUT_WIDTH * 0.5f;
		constexpr float LAYOUT_CY = LAYOUT_HEIGHT * 0.5f;

		inline float exoX(float v) { return LAYOUT_CX + v; }
		inline float exoY(float v) { return LAYOUT_CY + v; }

		// 透明度 (transparency, 0..100) → opacity (1..0)
		inline float opacityFromTransparency(float t) { return std::clamp(1.f - t / 100.f, 0.f, 1.f); }

		// start_grad Y keyframe: Y=1500 → 0 with deceleration (減速@加減速TRA).
		// In the reference .object the transition spans the full object lifetime
		// (120 frames per wave), not the 15 that the legacy .exo mode-int looked
		// like. Easing is ease-out quadratic: 1 - (1 - u)^2.
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

		// Object [6] in the .object: layer=3 frame=270..299 透明度=0→100 直線移動.
		// Intro card fades out linearly over 30 frames starting at 4.5s.
		float introFadeOutAlpha(float videoTime)
		{
			const float frame = videoTime * 60.f;
			if (frame < 270.f) return 1.f;
			if (frame >= 299.f) return 0.f;
			return 1.f - (frame - 270.f) / 29.f;
		}

		// Object [5] in the .object: layer=3 frame=0..111 group control,
		// X=43→0 Y=-43→0 with 減速 (deceleration). Slides the difficulty badge
		// and difficulty text out from behind the jacket toward the bottom-left.
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

	void Overlay::drawIntroBackground(Renderer* renderer, float sx, float sy, float videoTime)
	{
		(void)videoTime;
		// Object [1]: solid rectangle #68689c (full canvas @ 150%), 透明度=20 → α=0.80
		text.drawSolidRect(renderer, 0.f, 0.f,
		                   LAYOUT_WIDTH * sx, LAYOUT_HEIGHT * sy,
		                   Color(0x68 / 255.f, 0x68 / 255.f, 0x9c / 255.f, 0.80f), 70);
	}

	void Overlay::drawIntroStartGrad(Renderer* renderer, float sx, float sy, float videoTime)
	{
		const Texture* t = OverlayAssets::get(assets.startGrad);
		if (!t) return;

		// Wave 1 (frames 61-180) and Wave 2 (frames 181-300), each slides Y=1500 → 0
		// over the object lifetime with deceleration then holds. 透明度=90 → α=0.10.
		const float alpha = 0.10f * introFadeOutAlpha(videoTime);
		const float frame = videoTime * 60.f;

		auto drawWave = [&](float startFrame, float endFrame)
		{
			if (frame < startFrame || frame >= endFrame) return;
			const float local = frame - startFrame;
			const float yOffset = startGradY(local);

			// Parent scale 150% on exo canvas. Image dims already cover most of the
			// canvas width at this scale; render centered at screen (960, 540+yOffset).
			const float w = (float)t->getWidth()  * 1.5f * sx;
			const float h = (float)t->getHeight() * 1.5f * sy;
			const float cx = exoX(0.f) * sx;
			const float cy = exoY(yOffset) * sy;
			renderer->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h }, *t,
			                        0.f, (float)t->getWidth(),
			                        0.f, (float)t->getHeight(),
			                        Color(1.f, 1.f, 1.f, alpha), 72);
		};

		drawWave(61.f, 180.f);
		drawWave(181.f, 300.f);
	}

	void Overlay::drawIntroCard(Renderer* renderer, float sx, float sy, float videoTime,
	                            const Jacket& jacket)
	{
		const float alpha = introFadeOutAlpha(videoTime);
		if (alpha <= 0.f) return;

		// Difficulty badge (object [8]): pos (-661.5, 288), 拡大率 39.26% of 1024x1024.
		// Slides out from behind the jacket via group control [5] over 112 frames.
		if (const Texture* t = OverlayAssets::get(assets.difficultyBg(introData.difficulty)))
		{
			const auto off = diffBadgeSlideOffset(videoTime);
			const float target = 1024.f * 0.3926f;
			const float w = target * sx;
			const float h = target * sy;
			const float cx = exoX(-661.5f + off.x) * sx;
			const float cy = exoY(288.f   + off.y) * sy;
			renderer->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h }, *t,
			                        0.f, (float)t->getWidth(),
			                        0.f, (float)t->getHeight(),
			                        Color(1.f, 1.f, 1.f, alpha), 90);
		}

		// Cover (object [23]): pos (-618, 245), 拡大率 78.75%, source assumed 512x512
		if (const Texture* t = jacket.getTexture())
		{
			const float target = 512.f * 0.7875f; // 403.2px on a 1920x1080 canvas
			const float w = target * sx;
			const float h = target * sy;
			const float cx = exoX(-618.f) * sx;
			const float cy = exoY(245.f)  * sy;
			renderer->drawRectangle({ cx - w * 0.5f, cy - h * 0.5f }, { w, h }, *t,
			                        0.f, (float)t->getWidth(),
			                        0.f, (float)t->getHeight(),
			                        Color(1.f, 1.f, 1.f, alpha), 91);
		}
	}

	void Overlay::drawIntroText(Renderer* renderer, float sx, float sy, float videoTime)
	{
		const float alpha = introFadeOutAlpha(videoTime);
		if (alpha <= 0.f) return;

		const float unit = std::min(sx, sy);
		const Color white(1.f, 1.f, 1.f, alpha);

		// Difficulty text (main2 .object): pos (-855, 446), size 84, 拡大率 38%, left-top anchor.
		// Shares the group control [5] slide-in with the difficulty badge.
		{
			std::string up;
			up.reserve(introData.difficulty.size());
			for (char c : introData.difficulty) up.push_back((char)std::toupper((unsigned char)c));
			const auto off = diffBadgeSlideOffset(videoTime);
			const float scale = 84.f * 0.38f / 64.f * unit;
			text.drawText(renderer, up,
			              exoX(-855.f + off.x) * sx, exoY(446.f + off.y) * sy,
			              scale, white, 122, TextAlign::Left);
		}

		// Extra text (object [15]) — e.g. "Lv. 30" above the title.
		if (!introData.extra.empty())
		{
			const float scale = 27.f / 64.f * unit;
			text.drawText(renderer, introData.extra,
			              exoX(-380.f) * sx, exoY(200.f) * sy,
			              scale, white, 122, TextAlign::Left);
		}

		// Title (main2 .object): pos (-378.5, 274), size 96, 拡大率 40%, left-top anchor.
		if (!introData.title.empty())
		{
			const float scale = 96.f * 0.40f / 64.f * unit;
			text.drawText(renderer, introData.title,
			              exoX(-378.5f) * sx, exoY(274.f) * sy,
			              scale, white, 122, TextAlign::Left);
		}

		// Description lines (objects [19] and [21])
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
		text.drawText(renderer, desc1,
		              exoX(-380.f) * sx, exoY(364.5f) * sy,
		              descScale, white, 122, TextAlign::Left);
		text.drawText(renderer, desc2,
		              exoX(-380.f) * sx, exoY(413.f) * sy,
		              descScale, white, 122, TextAlign::Left);
	}

	void Overlay::drawIntroPass(Renderer* renderer, float vpWidth, float vpHeight,
	                            const Jacket& jacket, float chartTime)
	{
		if (vpWidth <= 0.f || vpHeight <= 0.f) return;
		if (!isIntroShowing(chartTime)) return;

		const float sx = vpWidth / LAYOUT_WIDTH;
		const float sy = vpHeight / LAYOUT_HEIGHT;
		const float videoTime = chartTime + introOffset;

		drawIntroBackground(renderer, sx, sy, videoTime);
		drawIntroStartGrad(renderer, sx, sy, videoTime);
		drawIntroCard(renderer, sx, sy, videoTime, jacket);
	}

	void Overlay::drawAssetPass(Renderer* renderer, float vpWidth, float vpHeight, float chartTime)
	{
		if (vpWidth <= 0.f || vpHeight <= 0.f) return;
		const float sx = vpWidth / LAYOUT_WIDTH;
		const float sy = vpHeight / LAYOUT_HEIGHT;

		// Post-intro HUD fade-in: .object [2] 透明度 100→0 over frames 300..395.
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
				// Let the AP takeover carry the moment — skip lower-priority HUD.
				return;
			}
			drawScoreBarAssets(renderer, sx, sy);
			drawComboAssets(renderer, sx, sy);
			drawLifeAssets(renderer, sx, sy);
			drawJudgmentAsset(renderer, sx, sy);
		}
	}

	void Overlay::drawAdditivePass(Renderer* renderer, float vpWidth, float vpHeight)
	{
		if (vpWidth <= 0.f || vpHeight <= 0.f) return;
		const float sx = vpWidth / LAYOUT_WIDTH;
		const float sy = vpHeight / LAYOUT_HEIGHT;
		drawApVideo(renderer, sx, sy, vpWidth, vpHeight);
	}

	void Overlay::drawTextPass(Renderer* renderer, float vpWidth, float vpHeight,
	                           const ScoreContext& context, float chartTime)
	{
		(void)context;
		if (!isInitialized() || vpWidth <= 0.f || vpHeight <= 0.f) return;
		const float sx = vpWidth / LAYOUT_WIDTH;
		const float sy = vpHeight / LAYOUT_HEIGHT;

		// Intro overlay-specific text (title / description / difficulty label).
		if (isIntroShowing(chartTime))
		{
			const float videoTime = chartTime + introOffset;
			drawIntroText(renderer, sx, sy, videoTime);
		}
	}
}
