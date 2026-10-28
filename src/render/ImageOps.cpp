#include <vector>

#include "ImageOps.h"

bool arena::isDarkOrRedInk(const Color &c)
{
	const int luminance = (c.r * 3 + c.g * 6 + c.b) / 10;
	const bool dark = luminance < 115;
	const bool red = (c.r - c.g) > 70;
	return dark || red;
}

bool arena::isRedInk(const Color &c)
{
	return c.r > 110 && (c.r - c.g) > 70 && (c.r - c.b) > 70;
}

bool arena::isGoldInk(const Color &c)
{
	return c.r > 110 && (c.r - c.b) > 60;
}

void arena::eraseInkInRegion(IndexedImage &image, const Palette &palette, const Rect &region, const InkPredicate &isInk)
{
	const int w = image.width;
	const int h = image.height;
	std::vector<uint8_t> unknown(static_cast<size_t>(w * h), 0);

	auto idx = [w](int x, int y) { return static_cast<size_t>(x + y * w); };

	// Marca a tinta e dilata 1 pixel para pegar o contorno suavizado das letras.
	std::vector<uint8_t> ink(unknown.size(), 0);
	for (int y = region.y; y < region.y + region.h; y++)
	{
		for (int x = region.x; x < region.x + region.w; x++)
		{
			if (x >= 0 && y >= 0 && x < w && y < h && isInk(palette[image.at(x, y)]))
			{
				ink[idx(x, y)] = 1;
			}
		}
	}

	for (int y = region.y; y < region.y + region.h; y++)
	{
		for (int x = region.x; x < region.x + region.w; x++)
		{
			if (x < 1 || y < 1 || x >= w - 1 || y >= h - 1) continue;
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++)
					if (ink[idx(x + dx, y + dy)]) unknown[idx(x, y)] = 1;
		}
	}

	// Preenche de fora para dentro copiando um vizinho conhecido (escolha pseudoaleatória
	// determinística para manter o granulado do pergaminho sem criar listras).
	uint32_t seed = 0x9E3779B9u;
	bool remaining = true;
	while (remaining)
	{
		remaining = false;
		std::vector<std::pair<size_t, uint8_t>> updates;
		for (int y = region.y; y < region.y + region.h; y++)
		{
			for (int x = region.x; x < region.x + region.w; x++)
			{
				if (x < 1 || y < 1 || x >= w - 1 || y >= h - 1 || !unknown[idx(x, y)]) continue;

				// Vizinhos conhecidos; fora da região, ignora pixels que também são "tinta"
				// (bordas escuras da arte) para não espalhá-los para dentro.
				uint8_t candidates[8];
				int count = 0;
				for (int dy = -1; dy <= 1; dy++)
				{
					for (int dx = -1; dx <= 1; dx++)
					{
						const int nx = x + dx;
						const int ny = y + dy;
						if ((dx == 0 && dy == 0) || unknown[idx(nx, ny)]) continue;
						const uint8_t neighbor = image.at(nx, ny);
						if (!region.contains(nx, ny) && isInk(palette[neighbor])) continue;
						candidates[count++] = neighbor;
					}
				}

				if (count > 0)
				{
					seed = seed * 1664525u + 1013904223u;
					updates.emplace_back(idx(x, y), candidates[(seed >> 16) % count]);
				}
				else
				{
					remaining = true;
				}
			}
		}

		if (updates.empty())
		{
			break;
		}

		for (const auto &[i, value] : updates)
		{
			image.pixels[i] = value;
			unknown[i] = 0;
		}
	}
}
