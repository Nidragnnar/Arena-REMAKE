#include <algorithm>
#include <cmath>

#include "../render/Ui.h"
#include "UiKit.h"

namespace
{
	using namespace arena;

	// Ruído de valor suave (texturas procedurais de madeira e pergaminho).
	float hashNoise(int x, int y)
	{
		uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
		h = (h ^ (h >> 13)) * 1274126177u;
		return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
	}

	float valueNoise(float x, float y)
	{
		const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
		const float fx = x - x0, fy = y - y0;
		const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
		const float a = hashNoise(x0, y0), b = hashNoise(x0 + 1, y0), c = hashNoise(x0, y0 + 1), d = hashNoise(x0 + 1, y0 + 1);
		return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
	}

	uint32_t shade(uint32_t base, float k, uint8_t alpha = 255)
	{
		auto ch = [k](uint32_t v) { return static_cast<uint32_t>(std::clamp(v * k, 0.0f, 255.0f)); };
		return (static_cast<uint32_t>(alpha) << 24) | (ch((base >> 16) & 0xFF) << 16) | (ch((base >> 8) & 0xFF) << 8) | ch(base & 0xFF);
	}

	// Escreve um pixel misturando com o que já existe (alfa < 255 também sobre a camada transparente).
	void blend(hud::Frame &frame, int x, int y, uint32_t c)
	{
		if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) return;
		uint32_t &dst = frame.pixels[static_cast<size_t>(y) * frame.width + x];
		const uint32_t a = c >> 24;
		if (a == 255 || (dst >> 24) == 0 && a == 0) { dst = c; return; }
		const uint32_t da = dst >> 24;
		const uint32_t oa = a + da * (255 - a) / 255;
		if (oa == 0) return;
		auto mix = [a, da, oa](uint32_t s, uint32_t d) { return (s * a * 255 + d * da * (255 - a)) / (oa * 255); };
		dst = (oa << 24) | (mix((c >> 16) & 0xFF, (dst >> 16) & 0xFF) << 16) | (mix((c >> 8) & 0xFF, (dst >> 8) & 0xFF) << 8) | mix(c & 0xFF, dst & 0xFF);
	}
}

arena::kit::Painter::Painter(hud::Frame &frame, const Fonts &fonts) : target(frame), fontSet(fonts)
{
	this->scaleUnit = std::max(1, static_cast<int>(std::lround(frame.height / 360.0)));
}

void arena::kit::Painter::border(const Rect &r, int t, uint32_t tint)
{
	hud::fillRect(this->target, r.x, r.y, r.w, t, tint);
	hud::fillRect(this->target, r.x, r.y + r.h - t, r.w, t, tint);
	hud::fillRect(this->target, r.x, r.y + t, t, r.h - 2 * t, tint);
	hud::fillRect(this->target, r.x + r.w - t, r.y + t, t, r.h - 2 * t, tint);
}

void arena::kit::Painter::woodPanel(const Rect &r, uint8_t alpha)
{
	const int u = this->scaleUnit;
	const int frameT = 4 * u;
	const int x1 = std::min(this->target.width, r.x + r.w), y1 = std::min(this->target.height, r.y + r.h);
	for (int y = std::max(0, r.y); y < y1; y++)
	{
		for (int x = std::max(0, r.x); x < x1; x++)
		{
			// Veios horizontais: ruído esticado em x.
			const float grain = valueNoise(x / (40.0f * u), y / (2.2f * u)) * 0.7f + valueNoise(x / (9.0f * u), y / (0.9f * u)) * 0.3f;
			const bool inFrame = x < r.x + frameT || x >= r.x + r.w - frameT || y < r.y + frameT || y >= r.y + r.h - frameT;
			const uint32_t base = inFrame ? color::Wood : color::WoodDark;
			blend(this->target, x, y, shade(base, 0.78f + 0.42f * grain, inFrame ? 255 : alpha));
		}
	}

	// Contorno escuro, filete dourado e sombra interna.
	this->border(r, u, hud::rgb(8, 5, 3));
	this->border(Rect{ r.x + frameT - u, r.y + frameT - u, r.w - 2 * frameT + 2 * u, r.h - 2 * frameT + 2 * u }, u, color::Gold);
	this->border(Rect{ r.x + frameT, r.y + frameT, r.w - 2 * frameT, r.h - 2 * frameT }, u, hud::rgb(10, 7, 4, 200));

	// Cravos de ferro dourado nos cantos.
	const int stud = 3 * u;
	const int inset = (frameT - stud) / 2;
	const std::pair<int, int> corners[] = { { r.x + inset, r.y + inset }, { r.x + r.w - inset - stud, r.y + inset },
		{ r.x + inset, r.y + r.h - inset - stud }, { r.x + r.w - inset - stud, r.y + r.h - inset - stud } };
	for (const auto &[cx, cy] : corners)
	{
		hud::fillRect(this->target, cx, cy, stud, stud, color::GoldDim);
		hud::fillRect(this->target, cx, cy, stud - u, stud - u, color::Gold);
		hud::fillRect(this->target, cx, cy, u, u, color::GoldBright);
	}
}

void arena::kit::Painter::parchmentPanel(const Rect &r)
{
	const int u = this->scaleUnit;
	const float edge = 14.0f * u;
	const int x1 = std::min(this->target.width, r.x + r.w), y1 = std::min(this->target.height, r.y + r.h);
	for (int y = std::max(0, r.y); y < y1; y++)
	{
		for (int x = std::max(0, r.x); x < x1; x++)
		{
			const float n = valueNoise(x / (60.0f * u), y / (60.0f * u)) * 0.6f + valueNoise(x / (12.0f * u), y / (12.0f * u)) * 0.3f
				+ hashNoise(x / u, y / u) * 0.1f;
			const float d = static_cast<float>(std::min({ x - r.x, r.x + r.w - 1 - x, y - r.y, r.y + r.h - 1 - y }));
			const float t = std::clamp(d / edge, 0.0f, 1.0f);
			const float k = (0.80f + 0.22f * n) * (0.72f + 0.28f * t * t * (3 - 2 * t));
			this->target.pixels[static_cast<size_t>(y) * this->target.width + x] = shade(hud::rgb(214, 186, 136), k);
		}
	}

	this->border(r, u, color::InkSoft);
}

void arena::kit::Painter::inset(const Rect &r, bool onParchment)
{
	const int u = this->scaleUnit;
	hud::fillRect(this->target, r.x, r.y, r.w, r.h, onParchment ? hud::rgb(120, 86, 46, 60) : hud::rgb(6, 4, 2, 150));
	hud::fillRect(this->target, r.x, r.y, r.w, u, onParchment ? hud::rgb(90, 60, 30, 140) : hud::rgb(0, 0, 0, 200));
	hud::fillRect(this->target, r.x, r.y, u, r.h, onParchment ? hud::rgb(90, 60, 30, 140) : hud::rgb(0, 0, 0, 200));
	hud::fillRect(this->target, r.x, r.y + r.h - u, r.w, u, onParchment ? hud::rgb(255, 240, 200, 90) : hud::rgb(120, 90, 50, 120));
	hud::fillRect(this->target, r.x + r.w - u, r.y, u, r.h, onParchment ? hud::rgb(255, 240, 200, 90) : hud::rgb(120, 90, 50, 120));
}

void arena::kit::Painter::divider(int x, int y, int w, uint32_t tint)
{
	const int u = this->scaleUnit;
	hud::fillRect(this->target, x, y, w, u, tint);
	// Losango no meio, como nos ornamentos dos menus originais.
	const int cx = x + w / 2;
	for (int i = 0; i < 3; i++) hud::fillRect(this->target, cx - (2 - i) * u, y - i * u, (5 - 2 * i) * u, (2 * i + 1) * u, tint);
}

void arena::kit::Painter::text(const BitmapFont &font, std::string_view s, int x, int y, uint32_t tint, TextAlign align, bool shadow, int scaleMul)
{
	hud::drawText(this->target, font, s, x, y, this->scaleUnit * scaleMul, tint, shadow, align);
}

int arena::kit::Painter::textWidth(const BitmapFont &font, std::string_view s, int scaleMul) const
{
	return hud::textWidth(font, s, this->scaleUnit * scaleMul);
}

int arena::kit::Painter::lineHeight(const BitmapFont &font, int scaleMul) const
{
	return (font.getHeight() + 3) * this->scaleUnit * scaleMul;
}

int arena::kit::Painter::paragraph(const BitmapFont &font, std::string_view s, const Rect &area, uint32_t tint, bool shadow)
{
	const std::string wrapped = ui::wrapText(font, s, std::max(10, area.w / this->scaleUnit));
	int y = area.y;
	size_t start = 0;
	while (start <= wrapped.size())
	{
		const size_t end = std::min(wrapped.find('\n', start), wrapped.size());
		if (y + this->lineHeight(font) > area.y + area.h) break;
		this->text(font, std::string_view(wrapped).substr(start, end - start), area.x, y, tint, TextAlign::Left, shadow);
		y += this->lineHeight(font);
		start = end + 1;
	}

	return y - area.y;
}

void arena::kit::Painter::button(const Rect &r, std::string_view label, bool hovered, bool active, bool enabled)
{
	const int u = this->scaleUnit;
	for (int y = 0; y < r.h; y++)
	{
		// Degradê vertical (mais claro em cima); afundado quando ativo.
		const float t = static_cast<float>(y) / std::max(1, r.h - 1);
		const float k = active ? 0.75f + 0.15f * t : (hovered ? 1.25f : 1.05f) - 0.35f * t;
		hud::fillRect(this->target, r.x, r.y + y, r.w, 1, shade(enabled ? color::WoodLight : color::Wood, k));
	}

	this->border(r, u, hud::rgb(8, 5, 3));
	this->border(Rect{ r.x + u, r.y + u, r.w - 2 * u, r.h - 2 * u }, u, hovered || active ? color::Gold : color::GoldDim);
	const uint32_t ink = !enabled ? color::TextDim : (hovered || active) ? color::GoldBright : color::Text;
	const BitmapFont &font = this->fontSet.arena;
	this->text(font, label, r.x + r.w / 2, r.y + (r.h - font.getHeight() * u) / 2 + (active ? u : 0), ink, TextAlign::Center);
}

void arena::kit::Painter::bar(const Rect &r, int value, int maxValue, uint32_t fillTint, std::string_view label)
{
	const int u = this->scaleUnit;
	hud::fillRect(this->target, r.x, r.y, r.w, r.h, hud::rgb(10, 8, 6, 220));
	const int innerW = r.w - 2 * u, innerH = r.h - 2 * u;
	const int filled = (maxValue > 0) ? innerW * std::clamp(value, 0, maxValue) / maxValue : 0;
	for (int y = 0; y < innerH; y++)
	{
		const float t = static_cast<float>(y) / std::max(1, innerH - 1);
		hud::fillRect(this->target, r.x + u, r.y + u + y, filled, 1, shade(fillTint, 1.35f - 0.55f * t));
	}

	this->border(r, u, color::GoldDim);
	this->text(this->fontSet.arena, label, r.x + r.w / 2, r.y + (r.h - this->fontSet.arena.getHeight() * u) / 2, color::Text, TextAlign::Center);
}

void arena::kit::Painter::rowHighlight(const Rect &r, bool selected, bool hovered, bool onParchment)
{
	if (selected)
	{
		hud::fillRect(this->target, r.x, r.y, r.w, r.h, onParchment ? hud::rgb(150, 100, 30, 90) : hud::rgb(201, 158, 82, 70));
		this->border(r, this->scaleUnit, onParchment ? color::InkSoft : color::Gold);
	}
	else if (hovered)
	{
		hud::fillRect(this->target, r.x, r.y, r.w, r.h, onParchment ? hud::rgb(255, 240, 200, 70) : hud::rgb(255, 230, 180, 28));
	}
}

void arena::kit::Painter::image(const std::vector<uint32_t> &rgba, int width, int height, const Rect &dst)
{
	if (width <= 0 || height <= 0 || rgba.size() < static_cast<size_t>(width) * height) return;
	const int x1 = std::min(this->target.width, dst.x + dst.w), y1 = std::min(this->target.height, dst.y + dst.h);
	for (int y = std::max(0, dst.y); y < y1; y++)
	{
		const int sy = std::min(height - 1, (y - dst.y) * height / std::max(1, dst.h));
		for (int x = std::max(0, dst.x); x < x1; x++)
		{
			const int sx = std::min(width - 1, (x - dst.x) * width / std::max(1, dst.w));
			const uint32_t p = rgba[static_cast<size_t>(sy) * width + sx];
			const uint32_t a = p >> 24;
			uint32_t &d = this->target.pixels[static_cast<size_t>(y) * this->target.width + x];
			if (a == 255) d = p;
			else if (a != 0)
			{
				// Borda semitransparente (PNG redesenhado) sobre o fundo.
				auto mix = [&](int shift) { return ((((p >> shift) & 0xFF) * a + ((d >> shift) & 0xFF) * (255 - a)) / 255) << shift; };
				d = 0xFF000000u | mix(16) | mix(8) | mix(0);
			}
		}
	}
}
