#pragma once

#include <string>

#include "../i18n/Localization.h"
#include "ArenaData.h"

namespace arena
{
	struct CharacterDraft;

	// Missão principal: os oito pedaços do Cajado do Caos, na ordem de províncias do original
	// (StaffProvinces do ACD.EXE). Cada pedaço segue os passos:
	//   0 = esperando o sonho de Ria (ao descansar), 1 = sonhou (procurar boatos na província),
	//   2 = conhece a masmorra da pista (local 33), 3 = tem o item da pista (levar ao governante),
	//   4 = conhece a masmorra do pedaço (local 32).
	// Com os oito pedaços: 0 = último sonho, 1 = ir ao Palácio Imperial, 2 = entrou no palácio,
	//   3 = chegou à câmara da Joia do Fogo, 4 = Tharn derrotado (fim da missão).
	namespace quest
	{
		constexpr int PieceCount = 8;
		constexpr int StaffProvinces[PieceCount] = { 1, 2, 5, 6, 4, 0, 7, 3 };
		constexpr int PieceDungeon = 32, ClueDungeon = 33;
		constexpr int TharnCreature = 23;   // criatura do executável (*ITEM 73 no IMPPAL.MIF)
		constexpr int JewelLevel = 3;       // último andar do IMPPAL.MIF, com a Joia do Fogo
		constexpr const char *PalaceMif = "IMPPAL.MIF";

		// Província do pedaço que o jogador procura agora (-1 com o cajado completo).
		int targetProvince(const CharacterDraft &character);
		// A masmorra da missão aparece no mapa de viagem (as aleatórias sempre aparecem).
		bool dungeonKnown(const CharacterDraft &character, const CityLocation &dungeon);
		// Ria aparece no sonho ao descansar (o primeiro só depois do nível 2, como ela avisa na cela).
		bool dreamDue(const CharacterDraft &character);
		// O que fazer agora, em uma frase (diário). inPrison: ainda nas Masmorras Imperiais.
		std::string objective(const CharacterDraft &character, const ArenaData &data, const Localization &text, bool inPrison);
		// Anota no diário com a hora atual do jogo.
		void note(CharacterDraft &character, std::string text, std::string detail = {});
		// Arquivo do mapa pronto de uma masmorra da missão ("42068779.MIF").
		std::string premadeMif(const CityLocation &dungeon);
	}
}
