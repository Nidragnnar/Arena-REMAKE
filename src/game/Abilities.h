#pragma once

#include <algorithm>

// Habilidades de raça e de classe descritas no manual do Arena (as chances por nível não estão no manual: estimadas).
// Raças: 0 Bretão, 1 Guarda Vermelho, 2 Nórdico, 3 Elfo Negro, 4 Alto Elfo, 5 Elfo da Floresta, 6 Khajiit, 7 Argoniano.
// Classes: 3 Feiticeiro, 4 Curandeiro, 5 Lâmina Noturna, 6 Bardo, 7 Arrombador, 8 Velhaco, 9 Acrobata, 10 Ladrão,
// 11 Assassino, 12 Monge, 13 Arqueiro, 14 Patrulheiro, 15 Bárbaro, 17 Cavaleiro.
namespace arena::ability
{
	// Chance (em milésimos) de golpe crítico (3x o dano). Assassino é o melhor; o Monge não acerta crítico com
	// arco e o Arqueiro só acerta com arco.
	inline int criticalPerMille(int classIndex, int level, bool missile)
	{
		int perLevel = 0;
		switch (classIndex)
		{
		case 11: perLevel = 30; break;              // Assassino
		case 10: perLevel = 20; break;              // Ladrão
		case 12: perLevel = missile ? 0 : 20; break; // Monge
		case 13: perLevel = missile ? 20 : 0; break; // Arqueiro
		case 5: case 9: perLevel = 15; break;       // Lâmina Noturna, Acrobata
		case 6: case 7: perLevel = 10; break;       // Bardo, Arrombador
		case 8: perLevel = 5; break;                // Velhaco
		default: break;
		}
		return std::min(500, perLevel * std::max(1, level));
	}

	// Bônus de raça para acertar e no dano: Guardas Vermelhos (nível / 3) só corpo a corpo, Elfos da Floresta
	// (nível / 3) só com arco, Elfos Negros (nível / 4) com qualquer arma.
	inline int raceWeaponBonus(int raceIndex, int level, bool missile)
	{
		if (raceIndex == 1 && !missile) return level / 3;
		if (raceIndex == 5 && missile) return level / 3;
		if (raceIndex == 3) return level / 4;
		return 0;
	}

	// Patrulheiros causam dano extra igual ao nível.
	inline int classDamageBonus(int classIndex, int level) { return classIndex == 14 ? level : 0; }

	// Teste de resistência contra feitiços (em %): Vontade e Sorte.
	inline int spellSavePercent(int willpower, int luck) { return std::clamp(willpower / 2 + (luck - 50) / 5, 5, 75); }

	// Nórdicos sofrem metade do dano de frio e Bretões metade do de magia (e nada se resistirem). Elementos:
	// 0 fogo, 1 frio, 2 veneno, 3 choque, 4 magia.
	inline bool halvesElement(int raceIndex, int element) { return (raceIndex == 2 && element == 1) || (raceIndex == 0 && element == 4); }

	// Bárbaros são imunes a veneno.
	inline bool immuneToElement(int classIndex, int element) { return classIndex == 15 && element == 2; }

	// Velocidade dentro de água ou fosso: Argonianos nadam rápido e Khajiits e Acrobatas escalam bem.
	inline float pitSpeed(int raceIndex, int classIndex, bool water)
	{
		if (water) return raceIndex == 7 ? 1.0f : 0.6f;
		return (raceIndex == 6 || classIndex == 9) ? 1.0f : 0.7f;
	}
}
