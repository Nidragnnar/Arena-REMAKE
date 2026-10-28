#include <algorithm>
#include <cmath>

#include "Leveling.h"

int arena::experienceForLevel(const ClassDefinition &def, int level)
{
	if (level <= 1) return 0;
	double xp = std::max(1, def.initialExperienceCap);
	for (int l = 3; l <= level; l++) xp *= (l <= 10) ? 1.875 : 1.5;
	return static_cast<int>(xp);
}

int arena::healthBonusFor(int endurance)
{
	return damageBonusFor(endurance);
}

void arena::recalculateDerived(CharacterDraft &character, const ClassDefinition &def)
{
	character.maxStamina = character.attributes[0] + character.attributes[5];
	character.maxSpellPoints = static_cast<int>(character.attributes[1] * def.spellPointsMultiplier);
}

int arena::tryLevelUp(CharacterDraft &character, const ClassDefinition &def, std::mt19937 &rng)
{
	if (character.experience < experienceForLevel(def, character.level + 1)) return 0;

	character.level++;
	const int die = std::max(1, def.healthDie);
	const int gained = std::max(1, 1 + static_cast<int>(rng() % static_cast<uint32_t>(die)) + healthBonusFor(character.attributes[5]));
	character.maxHealth += gained;
	character.levelPoints += 3 + static_cast<int>(rng() % 4u);
	character.addedPoints.fill(0);
	return gained;
}
