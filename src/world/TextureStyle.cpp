// Estilo HD das texturas: ampliação bicúbica com grão de detalhe procedural e mapa de
// relevo derivado da própria imagem (juntas escuras ficam fundas).

#include <algorithm>
#include <cmath>
#include <thread>
#include <unordered_map>

#include "Level.h"

namespace
{
	arena::TextureStyle currentStyle;

	// Scale2x (EPX): dobra a resolução preservando contornos; usado no recorte (alfa) dos sprites.
	void scale2x(std::vector<uint32_t> &px, int &w, int &h)
	{
		std::vector<uint32_t> out(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
		auto at = [&](int x, int y) { return px[static_cast<size_t>(std::clamp(y, 0, h - 1) * w + std::clamp(x, 0, w - 1))]; };
		for (int y = 0; y < h; y++)
		{
			for (int x = 0; x < w; x++)
			{
				const uint32_t p = at(x, y), a = at(x, y - 1), b = at(x + 1, y), c = at(x - 1, y), d = at(x, y + 1);
				const size_t o = static_cast<size_t>((y * 2) * (w * 2) + x * 2);
				out[o] = (c == a && c != d && a != b) ? a : p;
				out[o + 1] = (a == b && a != c && b != d) ? b : p;
				out[o + static_cast<size_t>(w * 2)] = (d == c && d != b && c != a) ? c : p;
				out[o + static_cast<size_t>(w * 2) + 1] = (b == d && b != a && d != c) ? d : p;
			}
		}

		px.swap(out);
		w *= 2;
		h *= 2;
	}

	// Ruído de valor suave e repetível nas bordas (as texturas se repetem pelo mapa).
	float tileNoise(float x, float y, int period, uint32_t seed)
	{
		auto hash = [seed, period](int ix, int iy)
		{
			ix = ((ix % period) + period) % period;
			iy = ((iy % period) + period) % period;
			uint32_t h = static_cast<uint32_t>(ix) * 374761393u + static_cast<uint32_t>(iy) * 668265263u + seed * 2246822519u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
		};

		const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
		const float fx = x - x0, fy = y - y0;
		const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
		const float top = hash(x0, y0) + (hash(x0 + 1, y0) - hash(x0, y0)) * sx;
		const float bottom = hash(x0, y0 + 1) + (hash(x0 + 1, y0 + 1) - hash(x0, y0 + 1)) * sx;
		return top + (bottom - top) * sy;
	}

	// Interpolação Catmull-Rom.
	float cubic(float a, float b, float c, float d, float t)
	{
		return b + 0.5f * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a)));
	}
}

namespace
{
	// Divide as linhas [0, rows) entre os núcleos do processador.
	template <typename Fn> void parallelRows(int rows, Fn &&fn)
	{
		const int workers = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 16);
		if (workers == 1 || rows < 32) { for (int y = 0; y < rows; y++) fn(y); return; }
		std::vector<std::thread> threads;
		const int chunk = (rows + workers - 1) / workers;
		for (int w = 0; w < workers; w++)
		{
			const int begin = w * chunk, end = std::min(rows, begin + chunk);
			if (begin >= end) break;
			threads.emplace_back([&fn, begin, end]() { for (int y = begin; y < end; y++) fn(y); });
		}
		for (std::thread &t : threads) t.join();
	}

	// Texturas já ampliadas, pelo conteúdo (entrar e sair de prédios não refaz o trabalho).
	struct CachedTexture { std::vector<uint32_t> pixels; std::vector<int8_t> normals; int width, height, scale; };
	std::unordered_map<uint64_t, CachedTexture> enhancedCache;
	size_t enhancedCacheBytes = 0;
	constexpr size_t EnhancedCacheLimit = size_t(768) << 20;

	uint64_t contentHash(const arena::Texture &t, const arena::TextureStyle &style)
	{
		uint64_t h = 1469598103934665603ull;
		auto mix = [&h](uint64_t v) { h ^= v; h *= 1099511628211ull; };
		mix(static_cast<uint64_t>(t.width)); mix(static_cast<uint64_t>(t.height));
		mix(t.hasTransparency ? 1u : 0u); mix(style.relief ? 1u : 0u);
		for (const uint32_t p : t.pixels) mix(p);
		return h;
	}

	// Normais do relevo a partir de um mapa de altura do mesmo tamanho da textura.
	void buildNormals(arena::Texture &t, const std::vector<float> &height, int reach)
	{
		const int W = t.width, H = t.height;
		auto heightAt = [&](int x, int y) { return height[static_cast<size_t>((((y % H) + H) % H) * W + ((x % W) + W) % W)]; };
		t.normals.resize(static_cast<size_t>(W) * static_cast<size_t>(H) * 3);
		parallelRows(H, [&](int y)
		{
			for (int x = 0; x < W; x++)
			{
				const float dx = (heightAt(x + 1, y) + heightAt(x + reach, y)) - (heightAt(x - 1, y) + heightAt(x - reach, y));
				const float dy = (heightAt(x, y + 1) + heightAt(x, y + reach)) - (heightAt(x, y - 1) + heightAt(x, y - reach));
				const float nx = -dx * 0.9f, ny = -dy * 0.9f;
				const float inv = 127.0f / std::sqrt(nx * nx + ny * ny + 1.0f);
				int8_t *n = &t.normals[(static_cast<size_t>(y) * W + x) * 3];
				n[0] = static_cast<int8_t>(nx * inv);
				n[1] = static_cast<int8_t>(ny * inv);
				n[2] = static_cast<int8_t>(inv);
			}
		});
	}
}

void arena::setTextureStyle(const TextureStyle &style)
{
	currentStyle = style;
}

const arena::TextureStyle &arena::getTextureStyle()
{
	return currentStyle;
}

void arena::enhanceTexture(Texture &t)
{
	if (!currentStyle.hd || t.width <= 0 || t.height <= 0)
	{
		return;
	}

	// 4x nas texturas de parede e sprites pequenos, 2x nas grandes (água/lava 320x100), para caber na memória.
	const int largest = std::max(t.width, t.height);
	const int factor = (largest <= 128) ? 4 : (largest <= 320) ? 2 : 1;
	if (factor == 1)
	{
		return;
	}

	const uint64_t key = contentHash(t, currentStyle);
	if (const auto it = enhancedCache.find(key); it != enhancedCache.end())
	{
		t.pixels = it->second.pixels;
		t.normals = it->second.normals;
		t.width = it->second.width;
		t.height = it->second.height;
		t.scale = it->second.scale;
		return;
	}

	const int w = t.width, h = t.height, W = w * factor, H = h * factor;
	const std::vector<uint32_t> src = t.pixels;
	std::vector<uint32_t> mask;
	if (t.hasTransparency)
	{
		mask = src;
		int mw = w, mh = h;
		while (mw < W)
		{
			scale2x(mask, mw, mh);
		}
	}

	auto at = [&](int x, int y) { return src[static_cast<size_t>((((y % h) + h) % h) * w + ((x % w) + w) % w)]; };
	std::vector<uint32_t> out(static_cast<size_t>(W) * static_cast<size_t>(H));
	std::vector<float> height(out.size());
	const float detailStrength = t.hasTransparency ? 0.12f : 0.28f;
	parallelRows(H, [&](int y)
	{
		for (int x = 0; x < W; x++)
		{
			const float fx = (x + 0.5f) / factor - 0.5f, fy = (y + 0.5f) / factor - 0.5f;
			const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
			const float tx = fx - ix, ty = fy - iy;
			float rgb[3];
			for (int c = 0; c < 3; c++)
			{
				float rows[4];
				for (int j = 0; j < 4; j++)
				{
					float v[4];
					for (int i = 0; i < 4; i++)
					{
						uint32_t q = at(ix - 1 + i, iy - 1 + j);
						// Vizinhos transparentes usam o texel mais próximo (evita contornos escuros).
						if (t.hasTransparency && (q >> 24) == 0) q = at(ix + (tx > 0.5f ? 1 : 0), iy + (ty > 0.5f ? 1 : 0));
						v[i] = static_cast<float>((q >> (8 * c)) & 0xFF);
					}
					rows[j] = cubic(v[0], v[1], v[2], v[3], tx);
				}
				rgb[c] = std::clamp(cubic(rows[0], rows[1], rows[2], rows[3], ty), 0.0f, 255.0f);
			}

			// Grão fino procedural: quebra os blocos de cor sem mudar o desenho.
			const float detail = tileNoise(x / 3.0f, y / 3.0f, W / 3, 7u) * 0.6f + tileNoise(x / 1.2f, y / 1.2f, static_cast<int>(W / 1.2f), 11u) * 0.4f;
			const float k = 1.0f + (detail - 0.5f) * detailStrength;
			const size_t index = static_cast<size_t>(y) * W + x;
			// Opacas: janelas (alfa 254) pelo texel original mais próximo.
			const uint32_t alpha = t.hasTransparency ? (mask[index] >> 24) : (at(x / factor, y / factor) >> 24) == 254u ? 254u : 255u;
			out[index] = (alpha << 24) | (static_cast<uint32_t>(std::clamp(rgb[2] * k, 0.0f, 255.0f)) << 16)
				| (static_cast<uint32_t>(std::clamp(rgb[1] * k, 0.0f, 255.0f)) << 8) | static_cast<uint32_t>(std::clamp(rgb[0] * k, 0.0f, 255.0f));
			height[index] = (rgb[0] * 0.3f + rgb[1] * 0.59f + rgb[2] * 0.11f) / 255.0f + (detail - 0.5f) * 0.08f;
		}
	});

	t.pixels.swap(out);
	t.width = W;
	t.height = H;
	t.scale = factor;

	// Relevo só em superfícies opacas (paredes, pisos, tetos); sprites continuam planos.
	if (currentStyle.relief && !t.hasTransparency) buildNormals(t, height, std::max(1, factor - 1));

	const size_t bytes = t.pixels.size() * 4 + t.normals.size();
	if (enhancedCacheBytes + bytes > EnhancedCacheLimit) { enhancedCache.clear(); enhancedCacheBytes = 0; }
	enhancedCache[key] = CachedTexture{ t.pixels, t.normals, t.width, t.height, t.scale };
	enhancedCacheBytes += bytes;
}

void arena::addRelief(Texture &t)
{
	// Relevo para texturas prontas (PNG redesenhado em 2x/4x): altura pela luminância.
	if (!currentStyle.relief || t.hasTransparency || t.width <= 0 || t.height <= 0) return;
	std::vector<float> height(t.pixels.size());
	for (size_t i = 0; i < t.pixels.size(); i++)
	{
		const uint32_t p = t.pixels[i];
		height[i] = ((p & 0xFF) * 0.3f + ((p >> 8) & 0xFF) * 0.59f + ((p >> 16) & 0xFF) * 0.11f) / 255.0f;
	}

	buildNormals(t, height, std::max(1, t.scale - 1));
}
