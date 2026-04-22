#include "OverlayText.h"
#include "../IO.h"
#include "../File.h"

#include <glad/glad.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <array>
#include <cmath>
#include <cstring>

namespace MikuMikuWorld
{
	namespace
	{
		bool decodeUtf8(const std::string& utf8, std::vector<uint32_t>& out)
		{
			out.clear();
			out.reserve(utf8.size());
			const unsigned char* s = reinterpret_cast<const unsigned char*>(utf8.data());
			size_t i = 0, n = utf8.size();
			while (i < n)
			{
				unsigned char c = s[i];
				uint32_t cp = 0;
				int extra = 0;
				if (c < 0x80) { cp = c; extra = 0; }
				else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
				else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
				else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
				else { ++i; continue; }

				if (i + extra >= n) return false;
				for (int k = 0; k < extra; ++k)
				{
					unsigned char cc = s[i + 1 + k];
					if ((cc & 0xC0) != 0x80) { cp = '?'; break; }
					cp = (cp << 6) | (cc & 0x3F);
				}
				out.push_back(cp);
				i += 1 + extra;
			}
			return true;
		}
	}

	OverlayText::OverlayText() = default;
	OverlayText::~OverlayText()
	{
		release();
	}

	void OverlayText::release()
	{
		if (glTexture)
		{
			glDeleteTextures(1, &glTexture);
			glTexture = 0;
		}
		glyphs.clear();
		fontBuffer.clear();
		fontBuffer.shrink_to_fit();
		font.reset();
		cursorX = 0;
		cursorY = 0;
		shelfHeight = 0;
		ascent = descent = lineGap = 0.f;
		scaleFactor = 0.f;
		initialized = false;
	}

	bool OverlayText::init(const std::string& fontPath, float pixelHeightArg)
	{
		if (initialized) release();

		if (!IO::File::exists(fontPath))
		{
			fprintf(stderr, "ERROR: OverlayText::init() font not found: %s\n", fontPath.c_str());
			return false;
		}

		{
			IO::File file(fontPath, IO::FileMode::ReadBinary);
			fontBuffer = file.readAllBytes();
		}

		if (fontBuffer.empty())
		{
			fprintf(stderr, "ERROR: OverlayText::init() font file is empty: %s\n", fontPath.c_str());
			return false;
		}

		font = std::make_unique<stbtt_fontinfo>();

		// .ttc (TrueType Collection) requires picking an offset for the first face.
		int offset = stbtt_GetFontOffsetForIndex(fontBuffer.data(), 0);
		if (offset < 0) offset = 0;
		if (!stbtt_InitFont(font.get(), fontBuffer.data(), offset))
		{
			fprintf(stderr, "ERROR: OverlayText::init() stbtt_InitFont failed for %s\n", fontPath.c_str());
			font.reset();
			fontBuffer.clear();
			return false;
		}

		pixelHeight = pixelHeightArg;
		scaleFactor = stbtt_ScaleForPixelHeight(font.get(), pixelHeight);

		int a, d, g;
		stbtt_GetFontVMetrics(font.get(), &a, &d, &g);
		ascent = a * scaleFactor;
		descent = d * scaleFactor;
		lineGap = g * scaleFactor;

		glGenTextures(1, &glTexture);
		glBindTexture(GL_TEXTURE_2D, glTexture);
		// GL 3.3 Core supports GL_R8 with GL_RED upload.
		std::vector<uint8_t> empty(atlasWidth * atlasHeight, 0);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasWidth, atlasHeight, 0, GL_RED, GL_UNSIGNED_BYTE, empty.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		// Single-channel alpha textures need the R value broadcast on sample.
		GLint swizzle[4] = { GL_RED, GL_RED, GL_RED, GL_RED };
		glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
		// Reserve a small white patch at the top-left so drawSolidRect can use the
		// same atlas & shader as text (R=1 sampled as alpha=1 through the swizzle).
		const int solidSize = 4;
		std::vector<uint8_t> whitePatch(solidSize * solidSize, 255);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, solidSize, solidSize, GL_RED, GL_UNSIGNED_BYTE, whitePatch.data());
		// Sample the center of the patch to avoid bleeding into unused texels.
		solidU0 = 1.f / (float)atlasWidth;
		solidV0 = 1.f / (float)atlasHeight;
		solidU1 = 3.f / (float)atlasWidth;
		solidV1 = 3.f / (float)atlasHeight;

		glBindTexture(GL_TEXTURE_2D, 0);

		cursorX = solidSize + 1;
		cursorY = 1;
		shelfHeight = solidSize;

		initialized = true;
		return true;
	}

	const OverlayText::Glyph* OverlayText::ensureGlyph(uint32_t codepoint)
	{
		auto it = glyphs.find(codepoint);
		if (it != glyphs.end())
			return &it->second;

		Glyph g{};
		g.valid = false;

		int glyphIndex = stbtt_FindGlyphIndex(font.get(), (int)codepoint);
		int adv = 0, lsb = 0;
		stbtt_GetGlyphHMetrics(font.get(), glyphIndex, &adv, &lsb);
		g.advance = adv * scaleFactor;

		if (glyphIndex == 0)
		{
			auto [ins, _] = glyphs.emplace(codepoint, g);
			return &ins->second;
		}

		int x0, y0, x1, y1;
		stbtt_GetGlyphBitmapBox(font.get(), glyphIndex, scaleFactor, scaleFactor, &x0, &y0, &x1, &y1);
		int w = x1 - x0;
		int h = y1 - y0;

		if (w <= 0 || h <= 0)
		{
			g.valid = true;
			g.width = 0;
			g.height = 0;
			auto [ins, _] = glyphs.emplace(codepoint, g);
			return &ins->second;
		}

		const int padding = 1;
		int needW = w + padding;
		int needH = h + padding;

		if (cursorX + needW > atlasWidth)
		{
			cursorX = 1;
			cursorY += shelfHeight + padding;
			shelfHeight = 0;
		}
		if (cursorY + needH > atlasHeight)
		{
			// Atlas full; return invalid glyph but keep advance so layout still works.
			auto [ins, _] = glyphs.emplace(codepoint, g);
			return &ins->second;
		}

		std::vector<uint8_t> bitmap(w * h, 0);
		stbtt_MakeGlyphBitmap(font.get(), bitmap.data(), w, h, w, scaleFactor, scaleFactor, glyphIndex);

		glBindTexture(GL_TEXTURE_2D, glTexture);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexSubImage2D(GL_TEXTURE_2D, 0, cursorX, cursorY, w, h, GL_RED, GL_UNSIGNED_BYTE, bitmap.data());

		g.u0 = (float)cursorX / (float)atlasWidth;
		g.v0 = (float)cursorY / (float)atlasHeight;
		g.u1 = (float)(cursorX + w) / (float)atlasWidth;
		g.v1 = (float)(cursorY + h) / (float)atlasHeight;
		g.xoff = (float)x0;
		g.yoff = (float)y0;
		g.width = (float)w;
		g.height = (float)h;
		g.valid = true;

		cursorX += needW;
		if (needH > shelfHeight) shelfHeight = needH;

		auto [ins, _] = glyphs.emplace(codepoint, g);
		return &ins->second;
	}

	float OverlayText::measureWidth(const std::string& utf8, float scale)
	{
		if (!initialized || utf8.empty()) return 0.f;
		std::vector<uint32_t> cps;
		decodeUtf8(utf8, cps);

		float width = 0.f;
		for (uint32_t cp : cps)
		{
			const Glyph* g = ensureGlyph(cp);
			if (!g) continue;
			width += g->advance * scale;
		}
		return width;
	}

	std::vector<std::string> OverlayText::wrapLines(const std::string& utf8, float scale, float maxWidth)
	{
		std::vector<std::string> lines;
		if (utf8.empty()) return lines;
		if (!initialized || maxWidth <= 0.f)
		{
			lines.push_back(utf8);
			return lines;
		}

		struct CP { uint32_t cp; size_t byteStart; size_t byteLen; float advance; };
		std::vector<CP> cps;
		{
			const unsigned char* s = reinterpret_cast<const unsigned char*>(utf8.data());
			size_t i = 0, n = utf8.size();
			while (i < n)
			{
				unsigned char c = s[i];
				uint32_t cp = 0;
				int extra = 0;
				if (c < 0x80) { cp = c; extra = 0; }
				else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
				else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
				else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
				else { ++i; continue; }

				if (i + (size_t)extra >= n) break;
				size_t start = i;
				for (int k = 0; k < extra; ++k)
				{
					unsigned char cc = s[i + 1 + k];
					if ((cc & 0xC0) != 0x80) { cp = '?'; break; }
					cp = (cp << 6) | (cc & 0x3F);
				}
				const Glyph* g = ensureGlyph(cp);
				float adv = g ? g->advance * scale : 0.f;
				cps.push_back({ cp, start, (size_t)(1 + extra), adv });
				i += 1 + extra;
			}
		}

		auto isSoftBreak = [](uint32_t cp) {
			return cp == ' ' || cp == '\t' || cp == 0x3000; // U+3000 IDEOGRAPHIC SPACE
		};

		size_t i = 0;
		while (i < cps.size())
		{
			// Drop leading whitespace left over from the previous wrap.
			while (i < cps.size() && isSoftBreak(cps[i].cp)) ++i;
			if (i >= cps.size()) break;

			const size_t start = i;
			size_t lastSoftBreak = SIZE_MAX;
			float w = 0.f;
			size_t j = start;

			while (j < cps.size())
			{
				if (cps[j].cp == '\n') break;
				const float nextW = w + cps[j].advance;
				if (w > 0.f && nextW > maxWidth) break;
				if (j > start && isSoftBreak(cps[j].cp)) lastSoftBreak = j;
				w = nextW;
				++j;
			}

			size_t lineEnd;   // exclusive
			size_t nextStart;
			if (j == cps.size())
			{
				lineEnd = j;
				nextStart = j;
			}
			else if (cps[j].cp == '\n')
			{
				lineEnd = j;
				nextStart = j + 1;
			}
			else if (lastSoftBreak != SIZE_MAX)
			{
				lineEnd = lastSoftBreak;
				nextStart = lastSoftBreak + 1;
			}
			else
			{
				// No soft break point: force a per-codepoint break, guaranteeing
				// forward progress even when a single glyph exceeds maxWidth.
				lineEnd = (j > start) ? j : (start + 1);
				nextStart = lineEnd;
			}

			size_t trimmed = lineEnd;
			while (trimmed > start && isSoftBreak(cps[trimmed - 1].cp)) --trimmed;

			if (trimmed > start)
			{
				const size_t bStart = cps[start].byteStart;
				const size_t bEnd = cps[trimmed - 1].byteStart + cps[trimmed - 1].byteLen;
				lines.push_back(utf8.substr(bStart, bEnd - bStart));
			}
			else
			{
				lines.emplace_back();
			}

			i = nextStart;
		}

		return lines;
	}

	void OverlayText::drawSolidRect(Renderer* renderer, float x, float y, float w, float h,
	                                const Color& tint, int zIndex)
	{
		if (!initialized || !renderer || w <= 0.f || h <= 0.f) return;

		std::array<DirectX::XMFLOAT4, 4> pos{
			DirectX::XMFLOAT4{ x + w, y,     0.f, 1.f },
			DirectX::XMFLOAT4{ x + w, y + h, 0.f, 1.f },
			DirectX::XMFLOAT4{ x,     y + h, 0.f, 1.f },
			DirectX::XMFLOAT4{ x,     y,     0.f, 1.f },
		};
		std::array<DirectX::XMFLOAT4, 4> uv{
			DirectX::XMFLOAT4{ solidU1, solidV0, 0.f, 0.f },
			DirectX::XMFLOAT4{ solidU1, solidV1, 0.f, 0.f },
			DirectX::XMFLOAT4{ solidU0, solidV1, 0.f, 0.f },
			DirectX::XMFLOAT4{ solidU0, solidV0, 0.f, 0.f },
		};
		DirectX::XMFLOAT4 color{ tint.r, tint.g, tint.b, tint.a };
		renderer->pushQuad(pos, uv, DirectX::XMMatrixIdentity(), color, (int)glTexture, zIndex);
	}

	void OverlayText::drawText(Renderer* renderer, const std::string& utf8,
	                           float x, float y, float scale,
	                           const Color& tint, int zIndex,
	                           TextAlign align)
	{
		if (!initialized || utf8.empty() || !renderer) return;

		std::vector<uint32_t> cps;
		decodeUtf8(utf8, cps);

		float originX = x;
		if (align == TextAlign::Center)
			originX -= measureWidth(utf8, scale) * 0.5f;
		else if (align == TextAlign::Right)
			originX -= measureWidth(utf8, scale);

		// Baseline y: caller gives the top of the line, so shift down by ascent.
		const float baseline = y + ascent * scale;

		for (uint32_t cp : cps)
		{
			const Glyph* g = ensureGlyph(cp);
			if (!g)
			{
				originX += pixelHeight * 0.3f * scale;
				continue;
			}
			if (g->valid && g->width > 0.f)
			{
				float qx0 = originX + g->xoff * scale;
				float qy0 = baseline + g->yoff * scale;
				float qx1 = qx0 + g->width * scale;
				float qy1 = qy0 + g->height * scale;

				std::array<DirectX::XMFLOAT4, 4> pos{
					DirectX::XMFLOAT4{ qx1, qy0, 0.f, 1.f },
					DirectX::XMFLOAT4{ qx1, qy1, 0.f, 1.f },
					DirectX::XMFLOAT4{ qx0, qy1, 0.f, 1.f },
					DirectX::XMFLOAT4{ qx0, qy0, 0.f, 1.f },
				};
				std::array<DirectX::XMFLOAT4, 4> uv{
					DirectX::XMFLOAT4{ g->u1, g->v0, 0.f, 0.f },
					DirectX::XMFLOAT4{ g->u1, g->v1, 0.f, 0.f },
					DirectX::XMFLOAT4{ g->u0, g->v1, 0.f, 0.f },
					DirectX::XMFLOAT4{ g->u0, g->v0, 0.f, 0.f },
				};
				DirectX::XMFLOAT4 color{ tint.r, tint.g, tint.b, tint.a };
				renderer->pushQuad(pos, uv, DirectX::XMMatrixIdentity(), color, (int)glTexture, zIndex);
			}
			originX += g->advance * scale;
		}
	}
}
