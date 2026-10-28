#pragma once

#include <string>

#include "../world/MifFile.h"
#include "ArenaData.h"

namespace arena
{
	// Masmorra aleatória como no original: blocos de 32x32 do RANDOM1.MIF (um dos 4 conjuntos)
	// sorteados pela semente, escadas (*LEVELUP/*LEVELDOWN) em blocos sorteados e 1 ou 2 andares
	// (4 nas masmorras de artefato). outMif.levels = os andares, do primeiro ao último.
	bool generateDungeon(const AssetManager &assets, uint32_t seed, int widthChunks, int depthChunks, bool artifact, MifFile &outMif, std::string &outError);

	// Semente das masmorras do ermo, pela província e pelo bloco do ermo (como no original).
	uint32_t wildDungeonSeed(const ArenaData &data, int province, int wildBlockX, int wildBlockY);
}
