#pragma once

#include <string>

#include "ArenaData.h"

namespace arena
{
	struct CharacterDraft;

	// Missão principal: os oito pedaços do Cajado do Caos, na ordem de províncias do original
	// (StaffProvinces do ACD.EXE). Cada pedaço segue os passos:
	//   0 = esperando o sonho de Ria (ao descansar), 1 = sonhou (procurar boatos na província),
	//   2 = conhece a masmorra da pista (local 33), 3 = conhece a masmorra do pedaço (local 32).
	namespace quest
	{
		constexpr int PieceCount = 8;
		constexpr int StaffProvinces[PieceCount] = { 1, 2, 5, 6, 4, 0, 7, 3 };
		constexpr int PieceDungeon = 32, ClueDungeon = 33;

		// Província do pedaço que o jogador procura agora (-1 com o cajado completo).
		int targetProvince(const CharacterDraft &character);
		// A masmorra da missão aparece no mapa de viagem (as aleatórias sempre aparecem).
		bool dungeonKnown(const CharacterDraft &character, const CityLocation &dungeon);
		// Ria aparece no sonho ao descansar (o primeiro só depois do nível 2, como ela avisa na cela).
		bool dreamDue(const CharacterDraft &character);
		// Arquivo do mapa pronto de uma masmorra da missão ("42068779.MIF").
		std::string premadeMif(const CityLocation &dungeon);
	}
}
