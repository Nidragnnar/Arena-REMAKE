#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "../assets/Font.h"
#include "../render/Canvas.h"

namespace arena::hud
{
	// Desenho 2D sobre o quadro RGBA da visão 3D (HUD em alta resolução).
	struct Frame
	{
		std::vector<uint32_t> &pixels;
		int width;
		int height;
	};

	constexpr uint32_t rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
	{
		return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | r;
	}

	// Retângulo com mistura alfa.
	void fillRect(Frame &frame, int x, int y, int w, int h, uint32_t color);

	// Texto com as fontes do Arena ampliadas por um fator inteiro.
	int textWidth(const BitmapFont &font, std::string_view utf8, int scale);
	void drawText(Frame &frame, const BitmapFont &font, std::string_view utf8, int x, int y, int scale, uint32_t color,
		bool shadow = true, TextAlign align = TextAlign::Left);
}
