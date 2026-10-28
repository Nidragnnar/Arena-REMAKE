#pragma once

#include <string>
#include <vector>

#include "Game.h"
#include "InventoryUi.h"
#include "Spells.h"

namespace arena
{
	// Nome traduzido do feitiço e descrição dos efeitos a partir dos modelos do SPELLMKR.TXT (em PT-BR).
	std::string spellName(const Game &game, const Spell &spell);
	std::vector<std::string> spellEffectLines(const Game &game, const Spell &spell, int level);
}
