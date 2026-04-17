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
#include <cstdio>

namespace MikuMikuWorld
{
	namespace
	{
		constexpr float LAYOUT_WIDTH  = 1920.f;
		constexpr float LAYOUT_HEIGHT = 1080.f;

		// 1920x1080 layout coordinates (match the pjsekai-overlay-APPEND .exo roughly)
		constexpr float BAR_CENTER_X     = 960.f;
		constexpr float BAR_Y            = 70.f;
		constexpr float BAR_WIDTH        = 1650.f;
		constexpr float BAR_HEIGHT       = 40.f;

		constexpr float COMBO_CENTER_X   = 1660.f;
		constexpr float COMBO_DIGIT_Y    = 460.f;
		constexpr float COMBO_LABEL_Y    = 620.f;
		constexpr float COMBO_DIGIT_SIZE = 120.f;  // Reference height of a digit in layout space.
		constexpr float COMBO_DIGIT_ADV  = 96.f;   // Advance between digits.
		constexpr float COMBO_LABEL_W    = 180.f;
		constexpr float COMBO_LABEL_H    = 50.f;

		constexpr float JUDGE_X          = 960.f;
		constexpr float JUDGE_Y          = 860.f;
		constexpr float JUDGE_WIDTH      = 560.f;
		constexpr float JUDGE_HEIGHT     = 120.f;
		constexpr float JUDGE_DURATION   = 0.28f;

		constexpr float AP_VIDEO_DURATION = 10.0f; // typical ap.mp4 length

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
		if (currentTime + 1e-3f < lastScoreEpoch)
			reset();

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
			judgmentFlashTimer = JUDGE_DURATION;
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
			                        Color(1.f, 1.f, 1.f, 1.f), 100);
		}

		// bar.png (1650x76) — gradient fill, masked by currentScore from the left
		const float ratio = std::clamp(currentScore, 0.f, 1.f);
		if (ratio > 0.f)
		{
			if (const Texture* t = OverlayAssets::get(assets.bar))
			{
				const float barW = (float)t->getWidth()  * COMP_SCALE * sx;
				const float barH = (float)t->getHeight() * COMP_SCALE * sy;
				const float barCX = bgCX + 34.f * COMP_SCALE * sx;
				const float barCY = bgCY - 3.f  * COMP_SCALE * sy;
				const float barLeft = barCX - barW * 0.5f;
				const float barTop  = barCY - barH * 0.5f;

				const float fillW = barW * ratio;
				const float uvX2  = (float)t->getWidth() * ratio;
				renderer->drawRectangle({ barLeft, barTop }, { fillW, barH },
				                        *t, 0.f, uvX2,
				                        0.f, (float)t->getHeight(),
				                        Color(1.f, 1.f, 1.f, 1.f), 102);
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
			                        Color(1.f, 1.f, 1.f, 1.f), 104);
		}

		// Big rank letter in the dark panel on the left.
		// sekai.obj2 draws score/rank/chr/{rank}.png at tempbuffer (-188, -6)
		// with scale 0.22, then the tempbuffer is placed at 1.5x.
		{
			int rankTex = assets.rankD;
			if (ratio >= RANK_S) rankTex = assets.rankS;
			else if (ratio >= RANK_A) rankTex = assets.rankA;
			else if (ratio >= RANK_B) rankTex = assets.rankB;
			else if (ratio >= RANK_C) rankTex = assets.rankC;

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
				                        Color(1.f, 1.f, 1.f, 1.f), 107);
			}
		}

		// Numeric score display. In pjsekai's tempbuffer the leftmost digit sits
		// at (-127, 26) and each digit advances +22 in tempbuffer units, drawn at
		// a within-tempbuffer scale of 0.65. Tempbuffer is then placed on canvas
		// with a 1.5x object scale, so:
		//   canvas_pos  = composite_center + tempbuffer_xy * 1.5
		//   canvas_size = raw_pixels * 0.65 * 1.5
		const int displayScore = (int)std::min(1200000.f, currentScore * 1000000.f / 0.896f);
		char sbuf[16];
		std::snprintf(sbuf, sizeof(sbuf), "%d", displayScore);
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
			                        Color(1.f, 1.f, 1.f, 1.f), z);
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
	}

	void Overlay::drawComboAssets(Renderer* renderer, float sx, float sy)
	{
		if (currentCombo <= 0) return;

		// Render digits right-to-left from the center-x
		char buf[16];
		std::snprintf(buf, sizeof(buf), "%d", currentCombo);
		const int len = (int)std::strlen(buf);

		const float digitH = COMBO_DIGIT_SIZE * std::min(sx, sy);
		const float digitAdv = COMBO_DIGIT_ADV * std::min(sx, sy);
		const float totalW = digitAdv * len;
		const float cy = COMBO_DIGIT_Y * sy;
		const float startX = COMBO_CENTER_X * sx - totalW * 0.5f;

		for (int i = 0; i < len; ++i)
		{
			char c = buf[i];
			int d = c - '0';
			if (d < 0 || d > 9) continue;
			const Texture* t = OverlayAssets::get(assets.comboDigit[d]);
			if (!t) continue;

			const float aspect = (float)t->getWidth() / (float)t->getHeight();
			const float w = digitH * aspect;
			const float cx = startX + digitAdv * i + digitAdv * 0.5f;
			drawTexCentered(renderer, t, cx, cy + digitH * 0.5f, w, digitH, 110);
		}

		// COMBO label beneath the digits
		if (const Texture* lbl = OverlayAssets::get(assets.comboLabel))
		{
			const float lw = COMBO_LABEL_W * std::min(sx, sy);
			const float lh = COMBO_LABEL_H * std::min(sx, sy);
			const float cx = COMBO_CENTER_X * sx;
			const float cy2 = COMBO_LABEL_Y * sy;
			drawTexCentered(renderer, lbl, cx, cy2, lw, lh, 110);
		}
	}

	void Overlay::drawJudgmentAsset(Renderer* renderer, float sx, float sy)
	{
		if (judgmentFlashTimer <= 0.f) return;
		const Texture* t = OverlayAssets::get(assets.judgePerfect);
		if (!t) return;

		float a = judgmentFlashTimer / JUDGE_DURATION;
		a = std::clamp(a, 0.f, 1.f);
		// Quick pop: scale goes 0.9 -> 1.0 in first 40% of the lifetime.
		float lifeT = 1.f - a;
		float scale = 0.9f + 0.1f * std::min(1.f, lifeT / 0.4f);

		const float w = JUDGE_WIDTH * sx * scale;
		const float h = JUDGE_HEIGHT * sy * scale;
		const float cx = JUDGE_X * sx;
		const float cy = JUDGE_Y * sy;
		drawTexCentered(renderer, t, cx, cy, w, h, 115, a);
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
	// Fallback (self-drawn) helpers — used when res/overlay/ is empty
	// ---------------------------------------------------------------------

	void Overlay::drawScoreBarFallback(Renderer* renderer, float sx, float sy)
	{
		const float barLeft = (BAR_CENTER_X - BAR_WIDTH * 0.5f) * sx;
		const float barTop  = BAR_Y * sy;
		const float barW    = BAR_WIDTH * sx;
		const float barH    = BAR_HEIGHT * sy;

		text.drawSolidRect(renderer, barLeft, barTop, barW, barH,
		                   Color(0.f, 0.f, 0.f, 0.55f), 100);
		const float ratio = std::clamp(currentScore, 0.f, 1.f);
		if (ratio > 0.f)
			text.drawSolidRect(renderer, barLeft, barTop, barW * ratio, barH,
			                   Color(1.f, 0.95f, 0.35f, 0.9f), 101);

		auto marker = [&](float pos) {
			const float x = barLeft + barW * pos;
			const float lineW = 2.f * sx;
			text.drawSolidRect(renderer, x - lineW * 0.5f, barTop - 2.f * sy,
			                   lineW, barH + 4.f * sy,
			                   Color(1.f, 1.f, 1.f, 0.6f), 102);
		};
		marker(RANK_C); marker(RANK_B); marker(RANK_A); marker(RANK_S);
	}

	void Overlay::drawComboTextFallback(Renderer* renderer, float sx, float sy)
	{
		if (currentCombo <= 0) return;
		char buf[16];
		std::snprintf(buf, sizeof(buf), "%d", currentCombo);
		const float unit = std::min(sx, sy);
		const float digitScale = 108.f / 64.f * unit;
		text.drawText(renderer, buf, COMBO_CENTER_X * sx, COMBO_DIGIT_Y * sy,
		              digitScale, Color(1.f, 1.f, 1.f, 0.95f), 120, TextAlign::Right);
		const float labelScale = 36.f / 64.f * unit;
		text.drawText(renderer, "COMBO", COMBO_CENTER_X * sx, COMBO_LABEL_Y * sy,
		              labelScale, Color(1.f, 1.f, 1.f, 0.7f), 120, TextAlign::Right);
	}

	void Overlay::drawJudgmentTextFallback(Renderer* renderer, float sx, float sy)
	{
		if (judgmentFlashTimer <= 0.f) return;
		float alpha = std::clamp(judgmentFlashTimer / JUDGE_DURATION, 0.f, 1.f);
		const float unit = std::min(sx, sy);
		const float jScale = 72.f / 64.f * unit;
		text.drawText(renderer, "PERFECT", JUDGE_X * sx, JUDGE_Y * sy,
		              jScale, Color(1.f, 0.95f, 0.35f, alpha), 125, TextAlign::Center);
	}

	void Overlay::drawAllPerfectTextFallback(Renderer* renderer, float sx, float sy)
	{
		if (!allPerfectTriggered) return;
		const float unit = std::min(sx, sy);
		const float pulse = 0.5f + 0.5f * std::sin(allPerfectTimer * 6.283185f);
		const float hue = std::fmod(allPerfectTimer * 0.4f, 1.f);
		const float r = 0.5f + 0.5f * std::sin(hue * 6.283185f);
		const float g = 0.5f + 0.5f * std::sin(hue * 6.283185f + 2.094395f);
		const float b = 0.5f + 0.5f * std::sin(hue * 6.283185f + 4.188790f);
		const float apScale = 120.f / 64.f * unit;
		text.drawText(renderer, "ALL PERFECT",
		              960.f * sx, 360.f * sy,
		              apScale, Color(r, g, b, 0.55f + 0.45f * pulse),
		              130, TextAlign::Center);
	}

	// ---------------------------------------------------------------------
	// Pass entry points
	// ---------------------------------------------------------------------

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

		const float x = 36.f * sx;
		const float y = 36.f * sy;
		const float w = 180.f * sx;
		const float h = 180.f * sy;

		renderer->drawRectangle({ x, y }, { w, h }, t,
		                        0.f, (float)t.getWidth(),
		                        0.f, (float)t.getHeight(),
		                        Color(1.f, 1.f, 1.f, 1.f), 92);
	}

	void Overlay::drawAssetPass(Renderer* renderer, float vpWidth, float vpHeight)
	{
		if (vpWidth <= 0.f || vpHeight <= 0.f) return;
		const float sx = vpWidth / LAYOUT_WIDTH;
		const float sy = vpHeight / LAYOUT_HEIGHT;

		if (assets.hasCore())
		{
			if (isApPlaying())
			{
				// Let the AP takeover carry the moment — skip lower-priority HUD.
				return;
			}
			drawScoreBarAssets(renderer, sx, sy);
			drawComboAssets(renderer, sx, sy);
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
	                           const ScoreContext& context)
	{
		if (!isInitialized() || vpWidth <= 0.f || vpHeight <= 0.f) return;
		const float sx = vpWidth / LAYOUT_WIDTH;
		const float sy = vpHeight / LAYOUT_HEIGHT;

		// Jacket caption (title + artist) stays as text.
		const float unit = std::min(sx, sy);
		if (!context.workingData.title.empty())
		{
			const float titleScale = 40.f / 64.f * unit;
			text.drawText(renderer, context.workingData.title,
			              232.f * sx, 56.f * sy,
			              titleScale, Color(1.f, 1.f, 1.f, 0.95f), 120, TextAlign::Left);
		}
		if (!context.workingData.artist.empty())
		{
			const float artistScale = 26.f / 64.f * unit;
			text.drawText(renderer, context.workingData.artist,
			              232.f * sx, 118.f * sy,
			              artistScale, Color(1.f, 1.f, 1.f, 0.75f), 120, TextAlign::Left);
		}

		// Score value now comes from the asset-based digit drawing in drawScoreBarAssets.

		// When no asset pack is present, draw the self-made fallbacks.
		if (!assets.hasCore())
		{
			drawScoreBarFallback(renderer, sx, sy);
			drawComboTextFallback(renderer, sx, sy);
			drawJudgmentTextFallback(renderer, sx, sy);
			drawAllPerfectTextFallback(renderer, sx, sy);
		}
	}
}
