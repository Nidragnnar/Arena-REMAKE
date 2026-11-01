#pragma once

#include <algorithm>
#include <random>

#include "Game.h"
#include "GameClock.h"

namespace arena
{
	// Feriados do original (TEMPLATE.DAT #0047..#0062): data, texto anunciado ao chegar numa cidade e efeito
	// nas lojas e templos. Os textos traduzidos ficam no idioma (Holidays.Name0.., Holidays.Text0..).
	namespace holiday
	{
		enum class Effect { None, FreeDrinks, FreeHealing, FreeRoom, FreeCures, Jester, TempleHalf, ShopsHalf, ShopsClosed, MagicHalf, SpellsHalf, WeaponsHalf };
		struct Holiday { int month, day; Effect effect; };

		constexpr Holiday Holidays[] =
		{
			{ 1, 1, Effect::FreeDrinks },    // Vida Nova: cerveja grátis nas tavernas
			{ 1, 15, Effect::FreeHealing },  // Prece do Vento Sul: cura grátis nos templos
			{ 2, 16, Effect::FreeRoom },     // Dia do Coração: quarto grátis nas estalagens
			{ 3, 7, Effect::FreeCures },     // Primeiro Plantio: doenças curadas de graça
			{ 4, 28, Effect::Jester },       // Dia do Bufão: todos de olho nos batedores de carteira
			{ 5, 7, Effect::FreeCures },     // Segundo Plantio
			{ 6, 16, Effect::TempleHalf },   // Celebração do Meio do Ano: templos pela metade
			{ 7, 10, Effect::ShopsHalf },    // Festival dos Mercadores: lojas de equipamento pela metade
			{ 7, 20, Effect::ShopsClosed },  // Descanso do Sol: lojas de equipamento fechadas
			{ 8, 27, Effect::FreeDrinks },   // Fim da Colheita: bebida grátis
			{ 9, 3, Effect::MagicHalf },     // Contos e Velas: itens mágicos pela metade
			{ 10, 13, Effect::SpellsHalf },  // Festival das Bruxas: feitiços pela metade
			{ 10, 30, Effect::None },        // Aniversário do Imperador
			{ 11, 20, Effect::WeaponsHalf }, // Festival dos Guerreiros: armas pela metade
			{ 12, 15, Effect::TempleHalf },  // Prece do Vento Norte: templos pela metade
			{ 12, 30, Effect::None },        // Vida Velha
		};
		constexpr int HolidayCount = static_cast<int>(sizeof(Holidays) / sizeof(Holidays[0]));

		// Feriado de hoje (índice na tabela) ou -1.
		inline int today(double gameMinutes)
		{
			const int month = clock::month(gameMinutes), day = clock::dayOfMonth(gameMinutes);
			for (int i = 0; i < HolidayCount; i++) if (Holidays[i].month == month && Holidays[i].day == day) return i;
			return -1;
		}

		inline Effect effectToday(double gameMinutes)
		{
			const int index = today(gameMinutes);
			return index < 0 ? Effect::None : Holidays[index].effect;
		}
	}

	// Furto: (Int + Agi) / divisor de furto da classe, como no arrombamento, mais o nível; ladrões têm o divisor
	// menor. difficulty tira pontos (o valor do que se tenta pegar); no Dia do Bufão todos vigiam os bolsos.
	inline int stealChance(const Game &game, int difficulty)
	{
		const CharacterDraft &c = game.getCharacter();
		const ClassDefinition &def = game.getData().getClass(std::max(0, c.classIndex));
		const int ability = (c.attributes[1] + c.attributes[3]) / std::max(1, def.thievingDivisor) * 2;
		int chance = ability / 2 + c.level * 2 + (def.isThief ? 15 : 0) - difficulty;
		if (holiday::effectToday(c.gameMinutes) == holiday::Effect::Jester) chance /= 2;
		return std::clamp(chance, 5, 90);
	}
}
