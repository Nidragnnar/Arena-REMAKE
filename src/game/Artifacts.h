#pragma once

#include <string>

#include "Items.h"

namespace arena
{
	struct CharacterDraft;
	class ArenaData;
	struct CityLocation;

	// Os 16 artefatos do original. Armas e armaduras são itens comuns (de ébano, em geral) com o
	// índice do artefato; anéis, amuleto, chave, livro e cofre são do tipo Trinket. Os poderes
	// valem enquanto o artefato está equipado (a chave e o cofre, só de carregar).
	namespace artifact
	{
		constexpr int Count = 16;
		enum Id
		{
			EbonyBlade, Chrysamere, Volendrung, MaceOfMolagBal, StaffOfMagnus, EbonyMail, LordsMail, AurielsShield,
			SpellBreaker, WarlocksRing, RingOfKhajiiti, RingOfPhynaster, NecromancersAmulet, SkeletonsKey, OghmaInfinium, OrgnumsCoffer
		};

		// Poderes passivos, somados entre os artefatos equipados.
		struct Powers
		{
			int strength = 0, agility = 0, intelligence = 0; // pontos de atributo
			int lifeSteal = 0;       // % do dano causado que volta como vida
			int manaSteal = 0;       // % do dano causado que volta como magia
			int healthRegen = 0;     // pontos de vida por minuto real
			int magicRegen = 0;      // pontos de magia por minuto real
			int damageReduction = 0; // % a menos de dano recebido
			int reflect = 0;         // % do dano recebido devolvido ao atacante
			int hitBonus = 0;        // pontos de acerto (5% cada)
			int speed = 0;           // % a mais de velocidade
			int spellDiscount = 0;   // % a menos no custo dos feitiços
			bool skeletonKey = false; // abre qualquer porta trancada
		};

		Item makeItem(int id);
		// Província e masmorra com nome (34..47) onde o artefato está escondido.
		const CityLocation *dungeon(const ArenaData &data, int id);
		bool hiddenIn(int id, const CityLocation &place);
		Powers powersOf(const CharacterDraft &character);
		bool owned(const CharacterDraft &character, int id);
		float weight(int id);
		int price(int id);
	}
}
