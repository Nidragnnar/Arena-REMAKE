#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "../core/Bytes.h"

namespace arena
{
	// Um andar de um .MIF. As camadas guardam um uint16 por voxel (índice x + y * largura):
	//   FLOR = chão (byte alto = textura; 0xC/0xD/0xE = abismos)
	//   MAP1 = paredes, portas, arcos e posição de sprites (0x8xxx)
	//   MAP2 = paredes do segundo andar (empilhadas acima do MAP1)
	struct MifLevel
	{
		std::string name;
		std::string info; // nome do .INF deste andar
		int numf = 0;
		std::vector<uint16_t> flor;
		std::vector<uint16_t> map1;
		std::vector<uint16_t> map2;

		struct Lock { int x, y, level; };
		struct Trigger { int x, y, textIndex, soundIndex; };
		std::vector<Lock> locks;
		std::vector<Trigger> triggers;
	};

	// Mapa de interior/cidade do Arena ("MHDR" + uma ou mais "LEVL").
	struct MifFile
	{
		int width = 0;
		int depth = 0;
		int startingLevel = 0;
		// Pontos de entrada em unidades do Arena (128 por voxel); (0,0) = não usado.
		std::array<std::pair<int, int>, 4> startPoints{};
		std::vector<MifLevel> levels;
	};

	std::optional<MifFile> loadMif(const ByteBuffer &data, std::string &outError);
}
