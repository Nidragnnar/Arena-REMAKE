#pragma once

#include <algorithm>
#include <string_view>
#include <vector>

#include "../assets/Font.h"
#include "../assets/Image.h"

namespace arena
{
	enum class TextAlign { Left, Center, Right };

	// Framebuffer indexado de 8 bits, como a tela VGA modo 13h do original.
	// Toda a UI clássica é composta aqui e depois convertida para RGBA.
	class Canvas
	{
	public:
		static constexpr int Width = 320;
		static constexpr int Height = 200;

		Canvas() : pixels(Width * Height, 0) { }

		void clear(uint8_t index);
		void blit(const IndexedImage &image, int x, int y, bool transparent = true);
		void blitRegion(const IndexedImage &image, int srcX, int srcY, int w, int h, int dstX, int dstY);
		void fillRect(int x, int y, int w, int h, uint8_t index);
		void drawText(const BitmapFont &font, std::string_view utf8, int x, int y, uint8_t color, int shadowColor = -1, TextAlign align = TextAlign::Left);

		// Texto com várias linhas separadas por '\n'. Retorna a altura total desenhada.
		int drawTextBlock(const BitmapFont &font, std::string_view utf8, int x, int y, uint8_t color, int shadowColor = -1, TextAlign align = TextAlign::Left, int lineSpacing = 2);

		void setPixel(int x, int y, uint8_t index)
		{
			if (x >= this->clipX0 && y >= this->clipY0 && x < this->clipX1 && y < this->clipY1)
			{
				this->pixels[static_cast<size_t>(x + y * Width)] = index;
			}
		}

		// Restringe o desenho a um retângulo (texto rolável).
		void setClip(int x, int y, int w, int h)
		{
			this->clipX0 = std::max(0, x); this->clipY0 = std::max(0, y);
			this->clipX1 = std::min(Width, x + w); this->clipY1 = std::min(Height, y + h);
		}

		void resetClip() { this->clipX0 = 0; this->clipY0 = 0; this->clipX1 = Width; this->clipY1 = Height; }

		uint8_t getPixel(int x, int y) const { return this->pixels[static_cast<size_t>(x + y * Width)]; }

		// Converte para RGBA usando a paleta; brightness (0..1) implementa o fade.
		void toRGBA(const Palette &palette, float brightness, std::vector<uint8_t> &out) const;

		const std::vector<uint8_t> &getPixels() const { return this->pixels; }

	private:
		std::vector<uint8_t> pixels;
		int clipX0 = 0, clipY0 = 0, clipX1 = Width, clipY1 = Height;
	};

	// Índice da cor mais próxima na paleta (para escolher cores de texto).
	uint8_t nearestColor(const Palette &palette, uint8_t r, uint8_t g, uint8_t b);
}
