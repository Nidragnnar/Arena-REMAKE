// Artefatos do original: itens, esconderijos e poderes.

#include <algorithm>

#include "Artifacts.h"
#include "ArenaData.h"
#include "Game.h"

namespace
{
	using namespace arena;

	struct Definition { ItemKind kind; int type; ArmorClass armorClass; int material; artifact::Powers powers; };

	artifact::Powers with(void (*set)(artifact::Powers &))
	{
		artifact::Powers p;
		set(p);
		return p;
	}

	const Definition &definitionOf(int id)
	{
		using P = artifact::Powers;
		static const Definition Table[artifact::Count] =
		{
			{ ItemKind::Weapon, 9, ArmorClass::Plate, 7, with([](P &p) { p.lifeSteal = 50; }) },              // Lâmina de Ébano (katana)
			{ ItemKind::Weapon, 6, ArmorClass::Plate, 7, with([](P &p) { p.hitBonus = 4; p.damageReduction = 15; }) }, // Chrysamere (claymore)
			{ ItemKind::Weapon, 13, ArmorClass::Plate, 7, with([](P &p) { p.strength = 25; }) },             // Volendrung (martelo de guerra)
			{ ItemKind::Weapon, 11, ArmorClass::Plate, 7, with([](P &p) { p.manaSteal = 100; }) },           // Maça de Molag Bal
			{ ItemKind::Weapon, 0, ArmorClass::Plate, 7, with([](P &p) { p.magicRegen = 12; p.spellDiscount = 25; }) }, // Cajado de Magnus
			{ ItemKind::Armor, 0, ArmorClass::Plate, 7, with([](P &p) { p.reflect = 50; }) },                // Cota de Ébano (couraça)
			{ ItemKind::Armor, 0, ArmorClass::Chain, 1, with([](P &p) { p.healthRegen = 12; }) },            // Cota do Senhor (couraça de malha)
			{ ItemKind::Armor, 9, ArmorClass::Plate, 7, with([](P &p) { p.damageReduction = 30; }) },        // Escudo de Auriel (pipa)
			{ ItemKind::Armor, 10, ArmorClass::Plate, 4, with([](P &p) { p.damageReduction = 15; p.magicRegen = 6; }) }, // Quebra-Feitiço (torre)
			{ ItemKind::Trinket, artifact::WarlocksRing, ArmorClass::Leather, 1, with([](P &p) { p.spellDiscount = 50; }) },
			{ ItemKind::Trinket, artifact::RingOfKhajiiti, ArmorClass::Leather, 1, with([](P &p) { p.speed = 30; p.agility = 15; }) },
			{ ItemKind::Trinket, artifact::RingOfPhynaster, ArmorClass::Leather, 1, with([](P &p) { p.damageReduction = 20; }) },
			{ ItemKind::Trinket, artifact::NecromancersAmulet, ArmorClass::Leather, 1, with([](P &p) { p.magicRegen = 18; p.intelligence = 15; }) },
			{ ItemKind::Trinket, artifact::SkeletonsKey, ArmorClass::Leather, 1, with([](P &p) { p.skeletonKey = true; }) },
			{ ItemKind::Trinket, artifact::OghmaInfinium, ArmorClass::Leather, 1, {} },
			{ ItemKind::Trinket, artifact::OrgnumsCoffer, ArmorClass::Leather, 1, {} },
		};
		return Table[std::clamp(id, 0, artifact::Count - 1)];
	}
}

arena::Item arena::artifact::makeItem(int id)
{
	const Definition &def = definitionOf(id);
	Item item;
	item.kind = def.kind;
	item.type = def.type;
	item.armorClass = def.armorClass;
	item.material = def.material;
	item.artifact = std::clamp(id, 0, Count - 1);
	return item;
}

const arena::CityLocation *arena::artifact::dungeon(const ArenaData &data, int id)
{
	// Um em cada província (duas voltas), em masmorras aleatórias diferentes entre si.
	return data.getCity(id % 8, 34 + (id * 5) % 14);
}

bool arena::artifact::hiddenIn(int id, const CityLocation &place)
{
	return place.type == CityLocation::Type::Dungeon && place.province == id % 8 && place.localId == 34 + (id * 5) % 14;
}

arena::artifact::Powers arena::artifact::powersOf(const CharacterDraft &character)
{
	Powers total;
	for (const Item &item : character.inventory)
	{
		if (item.artifact < 0) continue;
		if (item.artifact == SkeletonsKey) total.skeletonKey = true;
		if (!item.equipped) continue;
		const Powers &p = definitionOf(item.artifact).powers;
		total.strength += p.strength;
		total.agility += p.agility;
		total.intelligence += p.intelligence;
		total.lifeSteal += p.lifeSteal;
		total.manaSteal += p.manaSteal;
		total.healthRegen += p.healthRegen;
		total.magicRegen += p.magicRegen;
		total.damageReduction += p.damageReduction;
		total.reflect += p.reflect;
		total.hitBonus += p.hitBonus;
		total.speed += p.speed;
		total.spellDiscount += p.spellDiscount;
	}

	total.damageReduction = std::min(total.damageReduction, 75);
	total.spellDiscount = std::min(total.spellDiscount, 75);
	return total;
}

bool arena::artifact::owned(const CharacterDraft &character, int id)
{
	return (character.artifactsFound >> id) & 1;
}

float arena::artifact::weight(int id)
{
	return id == OghmaInfinium ? 2.0f : id == OrgnumsCoffer ? 4.0f : 0.1f;
}

int arena::artifact::price(int id)
{
	return 2500 + 250 * (id % 5);
}
