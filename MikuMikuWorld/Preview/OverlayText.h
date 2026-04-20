#pragma once
#include "../Rendering/Renderer.h"
#include "../MathUtils.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct stbtt_fontinfo;

namespace MikuMikuWorld
{
	enum class TextAlign
	{
		Left,
		Center,
		Right,
	};

	class OverlayText
	{
	public:
		OverlayText();
		~OverlayText();

		OverlayText(const OverlayText&) = delete;
		OverlayText& operator=(const OverlayText&) = delete;

		bool init(const std::string& fontPath, float pixelHeight = 64.f);
		void release();
		bool isInitialized() const { return initialized; }

		void drawText(Renderer* renderer, const std::string& utf8,
		              float x, float y, float scale,
		              const Color& tint, int zIndex,
		              TextAlign align = TextAlign::Left);

		void drawSolidRect(Renderer* renderer, float x, float y, float w, float h,
		                   const Color& tint, int zIndex);

		float measureWidth(const std::string& utf8, float scale);
		float getLineHeight(float scale) const { return (ascent - descent) * scale; }

		int getAtlasTextureId() const { return static_cast<int>(glTexture); }

	private:
		struct Glyph
		{
			float u0, v0, u1, v1;
			float xoff, yoff;
			float advance;
			float width, height;
			bool valid;
		};

		const Glyph* ensureGlyph(uint32_t codepoint);

		bool initialized{ false };
		std::vector<uint8_t> fontBuffer;
		std::unique_ptr<stbtt_fontinfo> font;
		std::unordered_map<uint32_t, Glyph> glyphs;

		int atlasWidth{ 2048 };
		int atlasHeight{ 2048 };
		int cursorX{ 0 };
		int cursorY{ 0 };
		int shelfHeight{ 0 };

		float pixelHeight{ 64.f };
		float scaleFactor{ 0.f };
		float ascent{ 0.f };
		float descent{ 0.f };
		float lineGap{ 0.f };

		unsigned int glTexture{ 0 };

		float solidU0{ 0.f }, solidV0{ 0.f }, solidU1{ 0.f }, solidV1{ 0.f };
	};
}
