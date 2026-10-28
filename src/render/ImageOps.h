#pragma once

#include <functional>

#include "../assets/Image.h"

namespace arena
{
	struct Rect
	{
		int x = 0, y = 0, w = 0, h = 0;

		bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
		int centerX() const { return x + w / 2; }
		int centerY() const { return y + h / 2; }
	};

	using InkPredicate = std::function<bool(const Color&)>;

	// Tinta escura ou vermelha (texto sobre pergaminho).
	bool isDarkOrRedInk(const Color &c);
	// Somente vermelho (rótulos do mapa de Tamriel).
	bool isRedInk(const Color &c);
	// Dourado/alaranjado (rótulos da ficha de personagem).
	bool isGoldInk(const Color &c);

	// Apaga texto "queimado" na arte original (ex.: rótulos em inglês do menu),
	// preenchendo os pixels de tinta da região com a textura vizinha, de fora
	// para dentro (inpainting por camadas).
	void eraseInkInRegion(IndexedImage &image, const Palette &palette, const Rect &region, const InkPredicate &isInk = isDarkOrRedInk);
}
