#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "../assets/Image.h"

namespace arena
{
	struct Texture;

	// Sprites e texturas redesenhados em PNG (pasta data/mods, em qualquer subpasta), com os
	// mesmos nomes da exportação (arena_tool export-sprites): NOME_EXT.png para arquivos de um
	// quadro e NOME_EXT_NNN.png para animações. O PNG pode ter o tamanho original ou um múltiplo
	// exato (2x, 4x...); a posição e o tamanho no jogo continuam os do original.
	void loadSpriteOverrides(const std::filesystem::path &modDir);
	size_t spriteOverrideCount();

	// PNG da pasta de mods pelo nome do arquivo sem extensão (ex.: "INTRO_000"), em RGBA 0xAABBGGRR.
	bool hasModPng(const std::string &stem);
	bool loadModPng(const std::string &stem, std::vector<uint32_t> &out, int &width, int &height);

	// Textura do PNG que substitui o quadro `frame` do arquivo `source` (frameCount = total de
	// quadros do arquivo), ou nada se não houver. `original` dá o tamanho de referência.
	std::optional<Texture> findSpriteOverride(const std::string &source, int frame, int frameCount, const IndexedImage &original);

	// Imagem pronta para a interface (0xAABBGGRR): o PNG redesenhado, se houver, ou o original pela
	// paleta (índice 0 transparente quando transparentZero). scale = ampliação sobre o original.
	struct RgbaImage
	{
		std::vector<uint32_t> pixels;
		int width = 0, height = 0;
		int scale = 1;
		bool fromPng = false;
	};

	RgbaImage loadRgba(const IndexedImage &image, const Palette &palette, const std::string &source, int frame, int frameCount, bool transparentZero = true);
}
