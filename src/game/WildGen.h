#pragma once

#include <string>

#include "../world/MifFile.h"
#include "ArenaData.h"

namespace arena
{
	// Ermo em volta de uma cidade, como no original: 64x64 blocos de 64x64 voxels (WILDnnn.RMD)
	// sorteados pela semente do nome da cidade, com as muralhas da cidade nos 4 blocos do centro.
	// Aqui só uma janela de WildWindow x WildWindow blocos em volta da cidade é montada.
	constexpr int WildChunk = 64;
	constexpr int WildWindow = 10;
	constexpr int WildCityChunk = 31;                          // canto da cidade no ermo do original
	constexpr int WildWindowFirst = WildCityChunk - 4;         // primeiro bloco da janela
	constexpr int WildCityOffset = (WildCityChunk - WildWindowFirst) * WildChunk; // cidade na janela (voxels)

	// weatherLetter: 'N' limpo, 'R' nublado/chuva, 'S' neve (como o .INF do original).
	bool generateWilderness(const AssetManager &assets, const ArenaData &data, const CityLocation &city, char weatherLetter, MifFile &outMif, std::string &outError);
}
