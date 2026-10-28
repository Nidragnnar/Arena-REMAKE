#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unordered_map>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4505) // funções internas do stb sem uso
#endif
#include <stb_image.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "Level.h"
#include "SpriteOverrides.h"

namespace
{
	std::unordered_map<std::string, std::filesystem::path> overrides; // "GOBLIN1_CFA_000" -> arquivo

	std::string upper(std::string text)
	{
		for (char &c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		return text;
	}
}

void arena::loadSpriteOverrides(const std::filesystem::path &modDir)
{
	overrides.clear();
	std::error_code error;
	if (!std::filesystem::is_directory(modDir, error)) return;
	for (auto it = std::filesystem::recursive_directory_iterator(modDir, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
	{
		if (!it->is_regular_file(error) || upper(it->path().extension().string()) != ".PNG") continue;
		overrides[upper(it->path().stem().string())] = it->path();
	}

	if (!overrides.empty()) std::printf("Sprites redesenhados: %zu PNG em %s\n", overrides.size(), modDir.string().c_str());
}

size_t arena::spriteOverrideCount()
{
	return overrides.size();
}

namespace
{
	// PNG que substitui o quadro, validado contra o tamanho original (múltiplo exato).
	bool readOverride(const std::string &source, int frame, int frameCount, int originalW, int originalH, std::vector<uint32_t> &out, int &w, int &h, int &scale)
	{
		if (overrides.empty() || originalW <= 0 || originalH <= 0) return false;

		// Nome da exportação: "GOBLIN1.CFA" -> "GOBLIN1_CFA" (+ "_000" quando há vários quadros).
		std::string base = upper(source);
		const size_t slash = base.find_last_of("/\\");
		if (slash != std::string::npos) base = base.substr(slash + 1);
		const size_t dot = base.rfind('.');
		if (dot != std::string::npos) base = base.substr(0, dot) + "_" + base.substr(dot + 1);
		char numbered[16];
		std::snprintf(numbered, sizeof(numbered), "_%03d", frame);
		auto it = overrides.find(frameCount > 1 ? base + numbered : base);
		if (it == overrides.end()) it = overrides.find(frameCount > 1 ? base : base + numbered);
		if (it == overrides.end()) return false;

		int channels = 0;
		stbi_uc *rgba = stbi_load(it->second.string().c_str(), &w, &h, &channels, 4);
		if (!rgba) return false;

		// Só múltiplos exatos encaixam no lugar do original.
		scale = w / originalW;
		if (scale < 1 || w != originalW * scale || h != originalH * scale)
		{
			std::printf("Sprite redesenhado ignorado (%s): %dx%d não é múltiplo de %dx%d\n", it->second.filename().string().c_str(), w, h, originalW, originalH);
			stbi_image_free(rgba);
			return false;
		}

		out.resize(static_cast<size_t>(w) * h);
		for (size_t i = 0; i < out.size(); i++)
		{
			const stbi_uc *q = rgba + i * 4;
			out[i] = (static_cast<uint32_t>(q[3]) << 24) | (static_cast<uint32_t>(q[2]) << 16) | (static_cast<uint32_t>(q[1]) << 8) | q[0];
		}

		stbi_image_free(rgba);
		return true;
	}
}

std::optional<arena::Texture> arena::findSpriteOverride(const std::string &source, int frame, int frameCount, const IndexedImage &original)
{
	std::vector<uint32_t> pixels;
	int w = 0, h = 0, scale = 1;
	if (!readOverride(source, frame, frameCount, original.width, original.height, pixels, w, h, scale)) return std::nullopt;

	Texture texture;
	texture.width = w;
	texture.height = h;
	texture.scale = scale;
	texture.pixels = std::move(pixels);
	for (uint32_t &p : texture.pixels)
	{
		// Meio-transparente vira recorte (o renderizador 3D usa teste de alfa).
		p = (p >> 24) >= 128 ? (p | 0xFF000000u) : 0u;
		texture.hasTransparency |= p == 0;
	}

	return texture;
}

arena::RgbaImage arena::loadRgba(const IndexedImage &image, const Palette &palette, const std::string &source, int frame, int frameCount, bool transparentZero)
{
	RgbaImage out;
	if (readOverride(source, frame, frameCount, image.width, image.height, out.pixels, out.width, out.height, out.scale))
	{
		out.fromPng = true;
		return out;
	}

	out.width = image.width;
	out.height = image.height;
	out.pixels.resize(image.pixels.size());
	for (size_t i = 0; i < image.pixels.size(); i++)
	{
		const uint8_t index = image.pixels[i];
		const Color &c = palette[index];
		out.pixels[i] = ((transparentZero && index == 0) ? 0u : 0xFF000000u) | (static_cast<uint32_t>(c.b) << 16) | (static_cast<uint32_t>(c.g) << 8) | c.r;
	}

	return out;
}
