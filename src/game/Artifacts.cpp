// Artefatos do original: itens, esconderijos e poderes.

#include <algorithm>

#include "Artifacts.h"
#include "ArenaData.h"
#include "Game.h"

namespace
{
	using namespace arena;

	// spell = poder do artefato no SPELLSG.65 (46..58); -1 = sem feitiço (efeito próprio, ver powersOf e useInventoryItem).
	struct Definition { ItemKind kind; int type; ArmorClass armorClass; int material; int spell; };

	const Definition &definitionOf(int id)
	{
		static const Definition Table[artifact::Count] =
		{
			{ ItemKind::Weapon, 9, ArmorClass::Plate, 7, 47 },    // Lâmina de Ébano (katana)
			{ ItemKind::Weapon, 6, ArmorClass::Plate, 7, 46 },    // Chrysamere (claymore)
			{ ItemKind::Weapon, 13, ArmorClass::Plate, 7, 53 },   // Volendrung (martelo de guerra)
			{ ItemKind::Weapon, 11, ArmorClass::Plate, 7, -1 },   // Maça de Molag Bal: rouba magia a cada golpe
			{ ItemKind::Weapon, 0, ArmorClass::Plate, 7, 51 },    // Cajado de Magnus
			{ ItemKind::Armor, 0, ArmorClass::Plate, 7, 56 },     // Cota de Ébano (couraça)
			{ ItemKind::Armor, 0, ArmorClass::Chain, 1, 49 },     // Cota do Senhor (couraça de malha)
			{ ItemKind::Armor, 9, ArmorClass::Plate, 7, 50 },     // Escudo de Auriel (pipa)
			{ ItemKind::Armor, 10, ArmorClass::Plate, 4, 48 },    // Quebra-Feitiço (torre)
			{ ItemKind::Trinket, artifact::WarlocksRing, ArmorClass::Leather, 1, 52 },
			{ ItemKind::Trinket, artifact::RingOfKhajiiti, ArmorClass::Leather, 1, 55 },
			{ ItemKind::Trinket, artifact::RingOfPhynaster, ArmorClass::Leather, 1, 54 },
			{ ItemKind::Trinket, artifact::NecromancersAmulet, ArmorClass::Leather, 1, 58 },
			{ ItemKind::Trinket, artifact::SkeletonsKey, ArmorClass::Leather, 1, -1 }, // abre qualquer porta trancada
			{ ItemKind::Trinket, artifact::OghmaInfinium, ArmorClass::Leather, 1, -1 }, // ler: +10 em tudo
			{ ItemKind::Trinket, artifact::OrgnumsCoffer, ArmorClass::Leather, 1, -1 }, // ouro todo dia
			{ ItemKind::Weapon, 17, ArmorClass::Plate, 7, 57 },   // Arco de Auriel (arco longo)
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

int arena::artifact::spellOf(int id)
{
	return definitionOf(id).spell;
}

void arena::artifact::syncSpells(CharacterDraft &character, const ItemTables &tables)
{
	// O poder fica no livro de feitiços só enquanto o item está equipado.
	std::vector<int> powers;
	for (const Item &item : character.inventory)
	{
		if (!item.equipped) continue;
		const int spell = item.artifact >= 0 ? spellOf(item.artifact) : enchantmentSpell(item, tables);
		if (spell >= 0 && std::find(powers.begin(), powers.end(), spell) == powers.end()) powers.push_back(spell);
	}

	// Saves antigos: poderes de artefato no livro sem registro de onde vieram.
	for (int spell : character.spells)
		if (spell >= 46 && spell <= 58 && std::find(character.grantedSpells.begin(), character.grantedSpells.end(), spell) == character.grantedSpells.end()) character.grantedSpells.push_back(spell);

	auto has = [](const std::vector<int> &list, int spell) { return std::find(list.begin(), list.end(), spell) != list.end(); };
	std::vector<int> granted;
	std::erase_if(character.spells, [&](int spell) { return has(character.grantedSpells, spell) && !has(powers, spell); });
	for (int spell : powers)
	{
		if (!has(character.spells, spell)) { character.spells.push_back(spell); granted.push_back(spell); }
		else if (has(character.grantedSpells, spell)) granted.push_back(spell);
	}
	character.grantedSpells = granted;
	if (character.readySpell >= 0 && !has(character.spells, character.readySpell)) character.readySpell = character.spells.empty() ? -1 : character.spells.front();
}

bool arena::artifact::isGranted(const CharacterDraft &character, int spell)
{
	return std::find(character.grantedSpells.begin(), character.grantedSpells.end(), spell) != character.grantedSpells.end();
}

const arena::CityLocation *arena::artifact::dungeon(const ArenaData &data, int id)
{
	// Um em cada província (duas voltas e mais um), em masmorras aleatórias diferentes entre si.
	return data.getCity(id % 8, 34 + (id * 5) % 14);
}

bool arena::artifact::hiddenIn(int id, const CityLocation &place)
{
	return place.type == CityLocation::Type::Dungeon && place.province == id % 8 && place.localId == 34 + (id * 5) % 14;
}

arena::artifact::Powers arena::artifact::powersOf(const CharacterDraft &character)
{
	// Efeitos que não são feitiço: a Maça de Molag Bal (equipada) e a Chave do Esqueleto (no inventário).
	Powers total;
	for (const Item &item : character.inventory)
	{
		if (item.artifact == SkeletonsKey) total.skeletonKey = true;
		if (item.artifact == MaceOfMolagBal && item.equipped) total.manaSteal = 100;
	}
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
