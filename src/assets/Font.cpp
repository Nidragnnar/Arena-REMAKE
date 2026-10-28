#include <algorithm>
#include <climits>

#include "Font.h"

namespace
{
	enum class Diacritic { Acute, Grave, Circumflex, Tilde, Cedilla, Diaeresis };

	struct AccentDef
	{
		char32_t codepoint;
		char base;
		Diacritic diacritic;
	};

	constexpr AccentDef AccentDefs[] =
	{
		{ U'á', 'a', Diacritic::Acute }, { U'à', 'a', Diacritic::Grave }, { U'â', 'a', Diacritic::Circumflex },
		{ U'ã', 'a', Diacritic::Tilde }, { U'é', 'e', Diacritic::Acute }, { U'ê', 'e', Diacritic::Circumflex },
		{ U'í', 'i', Diacritic::Acute }, { U'ó', 'o', Diacritic::Acute }, { U'ô', 'o', Diacritic::Circumflex },
		{ U'õ', 'o', Diacritic::Tilde }, { U'ú', 'u', Diacritic::Acute }, { U'ü', 'u', Diacritic::Diaeresis },
		{ U'ç', 'c', Diacritic::Cedilla },
		{ U'Á', 'A', Diacritic::Acute }, { U'À', 'A', Diacritic::Grave }, { U'Â', 'A', Diacritic::Circumflex },
		{ U'Ã', 'A', Diacritic::Tilde }, { U'É', 'E', Diacritic::Acute }, { U'Ê', 'E', Diacritic::Circumflex },
		{ U'Í', 'I', Diacritic::Acute }, { U'Ó', 'O', Diacritic::Acute }, { U'Ô', 'O', Diacritic::Circumflex },
		{ U'Õ', 'O', Diacritic::Tilde }, { U'Ú', 'U', Diacritic::Acute }, { U'Ü', 'U', Diacritic::Diaeresis },
		{ U'Ç', 'C', Diacritic::Cedilla }
	};

	void bounds(const arena::Glyph &glyph, int &minX, int &maxX, int &minY, int &maxY)
	{
		minX = minY = INT_MAX;
		maxX = maxY = INT_MIN;
		for (const auto &[x, y] : glyph.pixels)
		{
			minX = std::min<int>(minX, x); maxX = std::max<int>(maxX, x);
			minY = std::min<int>(minY, y); maxY = std::max<int>(maxY, y);
		}
	}
}

bool arena::BitmapFont::load(const ByteBuffer &data)
{
	constexpr size_t LinesOffset = 95;
	if (data.size() < LinesOffset)
	{
		return false;
	}

	this->height = data[0];
	this->glyphs.clear();

	size_t line = 0;
	for (char32_t c = 33; c <= 127; c++)
	{
		Glyph glyph;
		int maxWidth = 0;
		for (int y = 0; y < this->height; y++, line++)
		{
			const size_t offset = LinesOffset + line * 2;
			if (offset + 1 >= data.size())
			{
				return false;
			}

			const uint16_t bits = readLE16(data.data() + offset);
			for (int x = 0; x < 16; x++)
			{
				if ((bits & (0x8000 >> x)) != 0)
				{
					glyph.pixels.emplace_back(static_cast<int8_t>(x), static_cast<int8_t>(y));
					maxWidth = std::max(maxWidth, x + 1);
				}
			}
		}

		glyph.advance = maxWidth + 1;
		this->glyphs[c] = std::move(glyph);
	}

	// Espaço tem a largura do ponto de exclamação, como no jogo original.
	this->glyphs[U' '] = Glyph{ this->glyphs[U'!'].advance, {} };
	this->fallback = this->glyphs[U'?'];
	this->synthesizeAccents();
	return true;
}

void arena::BitmapFont::synthesizeAccents()
{
	// Variações tipográficas comuns em textos traduzidos.
	this->glyphs[U'ª'] = this->glyphs[U'a'];
	this->glyphs[U'º'] = this->glyphs[U'o'];
	this->glyphs[U'’'] = this->glyphs[U'\''];
	this->glyphs[U'“'] = this->glyphs[U'"'];
	this->glyphs[U'”'] = this->glyphs[U'"'];
	this->glyphs[U'–'] = this->glyphs[U'-'];
	this->glyphs[U'—'] = this->glyphs[U'-'];

	for (const AccentDef &def : AccentDefs)
	{
		const auto baseIt = this->glyphs.find(static_cast<char32_t>(def.base));
		if (baseIt == this->glyphs.end() || baseIt->second.pixels.empty())
		{
			continue;
		}

		Glyph glyph = baseIt->second;
		int minX, maxX, minY, maxY;
		bounds(glyph, minX, maxX, minY, maxY);

		// "í": remove o pingo do i (linhas acima do primeiro vão vertical).
		if (def.base == 'i')
		{
			int gapRow = -1;
			for (int y = minY; y <= maxY && gapRow < 0; y++)
			{
				const bool rowUsed = std::any_of(glyph.pixels.begin(), glyph.pixels.end(), [y](const auto &p) { return p.second == y; });
				if (!rowUsed) gapRow = y;
			}

			if (gapRow > 0)
			{
				std::erase_if(glyph.pixels, [gapRow](const auto &p) { return p.second < gapRow; });
				bounds(glyph, minX, maxX, minY, maxY);
			}
		}

		const int cx = (minX + maxX) / 2;
		const int top = minY - 2; // linha inferior do acento, com um pixel de folga
		auto add = [&glyph](int x, int y) { glyph.pixels.emplace_back(static_cast<int8_t>(x), static_cast<int8_t>(y)); };

		switch (def.diacritic)
		{
		case Diacritic::Acute:
			add(cx, top); add(cx + 1, top - 1);
			break;
		case Diacritic::Grave:
			add(cx, top); add(cx - 1, top - 1);
			break;
		case Diacritic::Circumflex:
			add(cx - 1, top); add(cx, top - 1); add(cx + 1, top);
			break;
		case Diacritic::Tilde:
			add(cx - 1, top); add(cx, top - 1); add(cx + 1, top); add(cx + 2, top - 1);
			break;
		case Diacritic::Diaeresis:
			add(cx - 1, top); add(cx + 1, top);
			break;
		case Diacritic::Cedilla:
			add(cx, maxY + 1); add(cx - 1, maxY + 2);
			break;
		}

		this->glyphs[def.codepoint] = std::move(glyph);
	}
}

const arena::Glyph &arena::BitmapFont::getGlyph(char32_t codepoint) const
{
	const auto it = this->glyphs.find(codepoint);
	return (it != this->glyphs.end()) ? it->second : this->fallback;
}

int arena::BitmapFont::measure(std::u32string_view text) const
{
	int width = 0;
	for (char32_t c : text)
	{
		width += this->getGlyph(c).advance;
	}

	return width;
}
