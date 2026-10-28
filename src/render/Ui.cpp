#include <algorithm>
#include <cctype>

#include "../core/Utf8.h"
#include "Ui.h"

int arena::ui::textWidth(const BitmapFont &font, std::string_view utf8)
{
	const std::vector<char32_t> codepoints = decodeUtf8(utf8);
	return font.measure(std::u32string(codepoints.begin(), codepoints.end()));
}

std::string arena::ui::sortKey(std::string_view utf8)
{
	std::string out;
	for (char32_t c : decodeUtf8(utf8))
	{
		switch (c)
		{
		case U'á': case U'à': case U'â': case U'ã': case U'Á': case U'À': case U'Â': case U'Ã': c = U'a'; break;
		case U'é': case U'ê': case U'É': case U'Ê': c = U'e'; break;
		case U'í': case U'Í': c = U'i'; break;
		case U'ó': case U'ô': case U'õ': case U'Ó': case U'Ô': case U'Õ': c = U'o'; break;
		case U'ú': case U'ü': case U'Ú': case U'Ü': c = U'u'; break;
		case U'ç': case U'Ç': c = U'c'; break;
		default: break;
		}

		out.push_back(static_cast<char>((c < 0x80) ? std::tolower(static_cast<int>(c)) : '~'));
	}

	return out;
}

void arena::ui::measureBlock(const BitmapFont &font, std::string_view utf8, int lineSpacing, int &outWidth, int &outHeight)
{
	outWidth = 0;
	int lines = 0;
	size_t start = 0;
	while (start <= utf8.size())
	{
		const size_t end = std::min(utf8.find('\n', start), utf8.size());
		outWidth = std::max(outWidth, textWidth(font, utf8.substr(start, end - start)));
		lines++;
		start = end + 1;
	}

	outHeight = lines * (font.getHeight() + lineSpacing) - lineSpacing;
}

std::string arena::ui::wrapText(const BitmapFont &font, std::string_view utf8, int maxWidth)
{
	std::string out;
	size_t start = 0;
	while (start <= utf8.size())
	{
		const size_t end = std::min(utf8.find('\n', start), utf8.size());
		const std::string_view paragraph = utf8.substr(start, end - start);

		std::string line;
		size_t wordStart = 0;
		while (wordStart < paragraph.size())
		{
			size_t wordEnd = paragraph.find(' ', wordStart);
			if (wordEnd == std::string_view::npos) wordEnd = paragraph.size();
			const std::string_view word = paragraph.substr(wordStart, wordEnd - wordStart);

			const std::string candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
			if (!line.empty() && textWidth(font, candidate) > maxWidth)
			{
				out += line + "\n";
				line = std::string(word);
			}
			else
			{
				line = candidate;
			}

			wordStart = wordEnd + 1;
		}

		out += line;
		if (end < utf8.size()) out += "\n";
		start = end + 1;
	}

	return out;
}

std::string arena::ui::format(std::string text, std::initializer_list<std::string> args)
{
	size_t searchFrom = 0;
	for (const std::string &arg : args)
	{
		const size_t pos = text.find("%s", searchFrom);
		if (pos == std::string::npos)
		{
			break;
		}

		text.replace(pos, 2, arg);
		searchFrom = pos + arg.size();
	}

	return text;
}

arena::IndexedImage arena::ui::makeParchment(const std::vector<IndexedImage> &tiles, int width, int height)
{
	IndexedImage out;
	out.width = width;
	out.height = height;
	out.pixels.assign(static_cast<size_t>(width * height), 0);
	if (tiles.size() < 9)
	{
		return out;
	}

	auto stamp = [&out](const IndexedImage &tile, int x, int y, bool clear)
	{
		for (int ty = 0; ty < tile.height; ty++)
		{
			for (int tx = 0; tx < tile.width; tx++)
			{
				const int dx = x + tx;
				const int dy = y + ty;
				if (dx < 0 || dy < 0 || dx >= out.width || dy >= out.height) continue;
				const uint8_t value = tile.at(tx, ty);
				if (value != 0 || clear) out.pixels[static_cast<size_t>(dx + dy * out.width)] = value;
			}
		}
	};

	const IndexedImage &corner = tiles[0];
	for (int y = corner.height; y < height - corner.height; y += tiles[4].height)
		for (int x = corner.width; x < width - corner.width; x += tiles[4].width)
			stamp(tiles[4], x, y, true);

	for (int y = corner.height; y < height - corner.height; y += tiles[3].height)
	{
		stamp(tiles[3], 0, y, true);
		stamp(tiles[5], width - tiles[5].width, y, true);
	}

	for (int x = corner.width; x < width - corner.width; x += tiles[1].width)
	{
		stamp(tiles[1], x, 0, true);
		stamp(tiles[7], x, height - tiles[7].height, true);
	}

	stamp(tiles[0], 0, 0, true);
	stamp(tiles[2], width - tiles[2].width, 0, true);
	stamp(tiles[6], 0, height - tiles[6].height, true);
	stamp(tiles[8], width - tiles[8].width, height - tiles[8].height, true);
	return out;
}

void arena::ui::drawDarkBox(Canvas &canvas, const Palette &palette, const Rect &r)
{
	const uint8_t fill = nearestColor(palette, 28, 24, 36);
	const uint8_t top = nearestColor(palette, 36, 36, 48);
	const uint8_t bottom = nearestColor(palette, 12, 12, 24);
	const uint8_t right = nearestColor(palette, 56, 69, 77);
	const uint8_t topRight = nearestColor(palette, 69, 85, 89);

	canvas.fillRect(r.x, r.y, r.w, r.h, fill);
	canvas.fillRect(r.x, r.y, r.w, 2, top);
	canvas.fillRect(r.x, r.y + r.h - 2, r.w, 2, bottom);
	canvas.fillRect(r.x, r.y, 2, r.h, bottom);
	canvas.fillRect(r.x + r.w - 2, r.y, 2, r.h, right);
	canvas.setPixel(r.x + r.w - 1, r.y, topRight);
	canvas.setPixel(r.x + r.w - 2, r.y + 1, topRight);
}
