#pragma once

#include <string>

#include "Items.h"

namespace arena
{
	struct CharacterDraft;
	class ArenaData;
	struct CityLocation;

	// Os 16 artefatos do original (a lista do executável), mais a Maça de Molag Bal, que não é do Arena e
	// não é oferecida (fica só para saves antigos do remake). Armas e armaduras são itens comuns (de ébano, em geral) com o
	// índice do artefato; anéis, amuleto, chave, livro e cofre são do tipo Trinket. Como no original,
	// o poder de cada um é um feitiço do SPELLSG.65 (46..58), que entra no livro de feitiços enquanto
	// o artefato está equipado e sai sem gastar magia (com recarga). Os sem feitiço têm efeito próprio.
	namespace artifact
	{
		constexpr int Count = 17;
		enum Id
		{
			EbonyBlade, Chrysamere, Volendrung, MaceOfMolagBal, StaffOfMagnus, EbonyMail, LordsMail, AurielsShield,
			SpellBreaker, WarlocksRing, RingOfKhajiiti, RingOfPhynaster, NecromancersAmulet, SkeletonsKey, OghmaInfinium, OrgnumsCoffer,
			AurielsBow
		};

		// Efeitos dos artefatos sem feitiço.
		struct Powers
		{
			int manaSteal = 0;        // % do dano causado que volta como magia (Maça de Molag Bal)
			bool skeletonKey = false; // abre qualquer porta trancada (Chave do Esqueleto)
		};

		Item makeItem(int id);
		int spellOf(int id); // feitiço do poder (-1 = nenhum)
		// Põe no livro os poderes dos artefatos e os feitiços das armas e armaduras encantadas equipados, e tira
		// os dos que não estão mais (só os que vieram de itens; os aprendidos ficam).
		void syncSpells(CharacterDraft &character, const ItemTables &tables);
		bool isGranted(const CharacterDraft &character, int spell);
		constexpr double PowerRecharge = 30.0; // segundos reais até o poder poder ser usado de novo
		// Província e masmorra com nome (34..47) onde o artefato está escondido.
		const CityLocation *dungeon(const ArenaData &data, int id);
		bool hiddenIn(int id, const CityLocation &place);
		Powers powersOf(const CharacterDraft &character);
		bool owned(const CharacterDraft &character, int id);
		float weight(int id);
		int price(int id);
	}
}
