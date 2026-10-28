#pragma once

#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../core/Bytes.h"

namespace arena
{
	// Glifo como lista de pixels acesos; y pode ser negativo (acentos sobre
	// maiúsculas) ou maior que a altura da fonte (cedilha).
	struct Glyph
	{
		int advance = 0;
		std::vector<std::pair<int8_t, int8_t>> pixels;
	};

	// Fontes bitmap do Arena (FONT_A.DAT, ARENAFNT.DAT, ...): 95 caracteres
	// ASCII de 33 a 127, cada linha um uint16 com o bit 15 à esquerda.
	// Os caracteres acentuados do português são sintetizados a partir da letra
	// base + diacrítico desenhado em pixel art.
	class BitmapFont
	{
	public:
		bool load(const ByteBuffer &data);

		int getHeight() const { return this->height; }
		const Glyph &getGlyph(char32_t codepoint) const;
		bool hasGlyph(char32_t codepoint) const { return this->glyphs.contains(codepoint); }
		int measure(std::u32string_view text) const;

	private:
		void synthesizeAccents();

		int height = 0;
		std::unordered_map<char32_t, Glyph> glyphs;
		Glyph fallback;
	};
}
