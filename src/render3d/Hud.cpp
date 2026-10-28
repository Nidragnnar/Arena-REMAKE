#include <algorithm>

#include "../core/Utf8.h"
#include "Hud.h"

void arena::hud::fillRect(Frame &frame, int x, int y, int w, int h, uint32_t color)
{
	const uint32_t alpha = color >> 24;
	const int x0 = std::max(0, x), y0 = std::max(0, y);
	const int x1 = std::min(frame.width, x + w), y1 = std::min(frame.height, y + h);
	for (int py = y0; py < y1; py++)
	{
		uint32_t *row = frame.pixels.data() + static_cast<size_t>(py) * frame.width;
		for (int px = x0; px < x1; px++)
		{
			if (alpha == 255)
			{
				row[px] = color;
				continue;
			}

			// Operador "over": também funciona sobre a camada transparente do HUD no modo GPU.
			const uint32_t dst = row[px];
			const uint32_t dstAlpha = dst >> 24;
			const uint32_t outAlpha = alpha + dstAlpha * (255 - alpha) / 255;
			if (outAlpha == 0) continue;
			auto mix = [alpha, dstAlpha, outAlpha](uint32_t s, uint32_t d) { return (s * alpha * 255 + d * dstAlpha * (255 - alpha)) / (outAlpha * 255); };
			const uint32_t r = mix(color & 0xFF, dst & 0xFF);
			const uint32_t g = mix((color >> 8) & 0xFF, (dst >> 8) & 0xFF);
			const uint32_t b = mix((color >> 16) & 0xFF, (dst >> 16) & 0xFF);
			row[px] = (outAlpha << 24) | (b << 16) | (g << 8) | r;
		}
	}
}

int arena::hud::textWidth(const BitmapFont &font, std::string_view utf8, int scale)
{
	const std::vector<char32_t> cps = decodeUtf8(utf8);
	return font.measure(std::u32string(cps.begin(), cps.end())) * scale;
}

void arena::hud::drawText(Frame &frame, const BitmapFont &font, std::string_view utf8, int x, int y, int scale, uint32_t color,
	bool shadow, TextAlign align)
{
	const std::vector<char32_t> cps = decodeUtf8(utf8);
	int penX = x;
	if (align == TextAlign::Center) penX -= textWidth(font, utf8, scale) / 2;
	else if (align == TextAlign::Right) penX -= textWidth(font, utf8, scale);

	auto plot = [&frame, scale](int px, int py, uint32_t c)
	{
		for (int sy = 0; sy < scale; sy++)
		{
			const int yy = py + sy;
			if (yy < 0 || yy >= frame.height) continue;
			for (int sx = 0; sx < scale; sx++)
			{
				const int xx = px + sx;
				if (xx >= 0 && xx < frame.width) frame.pixels[static_cast<size_t>(yy) * frame.width + xx] = c;
			}
		}
	};

	for (char32_t c : cps)
	{
		const Glyph &glyph = font.getGlyph(c);
		if (shadow)
		{
			for (const auto &[gx, gy] : glyph.pixels) plot(penX + (gx + 1) * scale, y + (gy + 1) * scale, rgb(0, 0, 0));
		}

		for (const auto &[gx, gy] : glyph.pixels) plot(penX + gx * scale, y + gy * scale, color);
		penX += glyph.advance * scale;
	}
}
