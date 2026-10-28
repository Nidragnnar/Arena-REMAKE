#include <algorithm>
#include <climits>
#include <string>

#include "../core/Utf8.h"
#include "Canvas.h"

void arena::Canvas::clear(uint8_t index)
{
	std::fill(this->pixels.begin(), this->pixels.end(), index);
}

void arena::Canvas::blit(const IndexedImage &image, int x, int y, bool transparent)
{
	for (int iy = 0; iy < image.height; iy++)
	{
		for (int ix = 0; ix < image.width; ix++)
		{
			const uint8_t value = image.at(ix, iy);
			if (!transparent || value != 0)
			{
				this->setPixel(x + ix, y + iy, value);
			}
		}
	}
}

void arena::Canvas::blitRegion(const IndexedImage &image, int srcX, int srcY, int w, int h, int dstX, int dstY)
{
	for (int iy = 0; iy < h; iy++)
	{
		for (int ix = 0; ix < w; ix++)
		{
			const int sx = srcX + ix;
			const int sy = srcY + iy;
			if (sx >= 0 && sy >= 0 && sx < image.width && sy < image.height)
			{
				this->setPixel(dstX + ix, dstY + iy, image.at(sx, sy));
			}
		}
	}
}

void arena::Canvas::fillRect(int x, int y, int w, int h, uint8_t index)
{
	for (int iy = y; iy < y + h; iy++)
	{
		for (int ix = x; ix < x + w; ix++)
		{
			this->setPixel(ix, iy, index);
		}
	}
}

void arena::Canvas::drawText(const BitmapFont &font, std::string_view utf8, int x, int y, uint8_t color, int shadowColor, TextAlign align)
{
	const std::vector<char32_t> codepoints = decodeUtf8(utf8);
	const std::u32string text(codepoints.begin(), codepoints.end());

	int penX = x;
	if (align == TextAlign::Center) penX -= font.measure(text) / 2;
	else if (align == TextAlign::Right) penX -= font.measure(text);

	for (char32_t c : text)
	{
		const Glyph &glyph = font.getGlyph(c);
		for (const auto &[gx, gy] : glyph.pixels)
		{
			if (shadowColor >= 0)
			{
				this->setPixel(penX + gx + 1, y + gy + 1, static_cast<uint8_t>(shadowColor));
			}
		}

		for (const auto &[gx, gy] : glyph.pixels)
		{
			this->setPixel(penX + gx, y + gy, color);
		}

		penX += glyph.advance;
	}
}

int arena::Canvas::drawTextBlock(const BitmapFont &font, std::string_view utf8, int x, int y, uint8_t color, int shadowColor, TextAlign align, int lineSpacing)
{
	int lineY = y;
	size_t start = 0;
	while (start <= utf8.size())
	{
		const size_t end = std::min(utf8.find('\n', start), utf8.size());
		this->drawText(font, utf8.substr(start, end - start), x, lineY, color, shadowColor, align);
		lineY += font.getHeight() + lineSpacing;
		start = end + 1;
	}

	return lineY - y;
}

void arena::Canvas::toRGBA(const Palette &palette, float brightness, std::vector<uint8_t> &out) const
{
	out.resize(this->pixels.size() * 4);
	const int scale = static_cast<int>(std::clamp(brightness, 0.0f, 1.0f) * 256.0f);
	for (size_t i = 0; i < this->pixels.size(); i++)
	{
		const Color &c = palette[this->pixels[i]];
		out[i * 4 + 0] = static_cast<uint8_t>((c.r * scale) >> 8);
		out[i * 4 + 1] = static_cast<uint8_t>((c.g * scale) >> 8);
		out[i * 4 + 2] = static_cast<uint8_t>((c.b * scale) >> 8);
		out[i * 4 + 3] = 255;
	}
}

uint8_t arena::nearestColor(const Palette &palette, uint8_t r, uint8_t g, uint8_t b)
{
	int best = 1;
	int bestDist = INT_MAX;
	for (int i = 1; i < 256; i++)
	{
		const int dr = palette[i].r - r;
		const int dg = palette[i].g - g;
		const int db = palette[i].b - b;
		const int dist = dr * dr * 3 + dg * dg * 4 + db * db * 2;
		if (dist < bestDist)
		{
			bestDist = dist;
			best = i;
		}
	}

	return static_cast<uint8_t>(best);
}
