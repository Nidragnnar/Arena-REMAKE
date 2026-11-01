// Itens: tabelas do ACD.EXE (offsets e significados via OpenTESArena, MIT).

#include <algorithm>

#include "Artifacts.h"
#include "Items.h"

namespace
{
	std::vector<std::string> readStrings(const arena::ByteBuffer &exe, size_t offset, int count)
	{
		std::vector<std::string> out;
		for (int i = 0; i < count && offset < exe.size(); i++)
		{
			std::string s;
			while (offset < exe.size() && exe[offset] != 0) s.push_back(static_cast<char>(exe[offset++]));
			offset++;
			out.push_back(std::move(s));
		}

		return out;
	}

	int randomInt(std::mt19937 &rng, int lo, int hi)
	{
		return (hi <= lo) ? lo : lo + static_cast<int>(rng() % static_cast<uint32_t>(hi - lo + 1));
	}

	// Material sorteado pela tabela acumulada do original (Ferro 11%, Aço 49%, ...).
	int rollMaterial(const arena::ItemTables &tables, std::mt19937 &rng)
	{
		const int roll = randomInt(rng, 0, 99);
		for (int i = 0; i < arena::MaterialCount; i++)
		{
			if (roll < tables.materialChance[static_cast<size_t>(i)]) return i;
		}

		return 1;
	}

	arena::Item randomEquipment(const arena::ItemTables &tables, std::mt19937 &rng)
	{
		arena::Item item;
		if (randomInt(rng, 0, 99) < 60)
		{
			item.kind = arena::ItemKind::Weapon;
			item.type = randomInt(rng, 0, arena::WeaponTypeCount - 1);
			item.material = rollMaterial(tables, rng);
		}
		else
		{
			item.kind = arena::ItemKind::Armor;
			item.type = randomInt(rng, 0, arena::ArmorPieceCount - 1);
			item.armorClass = static_cast<arena::ArmorClass>(randomInt(rng, 0, 2));
			item.material = rollMaterial(tables, rng);
		}

		return item;
	}

	arena::Item potion(int type)
	{
		arena::Item item;
		item.kind = arena::ItemKind::Potion;
		item.type = type;
		return item;
	}
}

bool arena::ItemTables::load(const ByteBuffer &exe, const std::function<size_t(const char*)> &offsetOf)
{
	const size_t weaponNamesAt = offsetOf("Equipment.WeaponNames");
	if (weaponNamesAt == 0)
	{
		return false;
	}

	const auto weapons = readStrings(exe, weaponNamesAt, WeaponTypeCount);
	const size_t damageAt = offsetOf("Equipment.WeaponDamages");
	const size_t weightAt = offsetOf("Equipment.WeaponWeights");
	const size_t priceAt = offsetOf("Equipment.WeaponBasePrices");
	const size_t handsAt = offsetOf("Equipment.WeaponHandednesses");
	for (size_t i = 0; i < WeaponTypeCount; i++)
	{
		this->weaponNames[i] = i < weapons.size() ? weapons[i] : std::string();
		this->weaponMinDamage[i] = exe[damageAt + i * 2];
		this->weaponMaxDamage[i] = exe[damageAt + i * 2 + 1];
		this->weaponWeight[i] = readLE16(exe.data() + weightAt + i * 2) / 256.0f;
		this->weaponPrice[i] = exe[priceAt + i];
		this->weaponHands[i] = exe[handsAt + i];
	}

	const auto armors = readStrings(exe, offsetOf("Equipment.ArmorNames"), ArmorPieceCount);
	const char *qualityKeys[3] = { "Equipment.PlateArmorQualities", "Equipment.ChainArmorQualities", "Equipment.LeatherArmorQualities" };
	const char *weightKeys[3] = { "Equipment.PlateArmorWeights", "Equipment.ChainArmorWeights", "Equipment.LeatherArmorWeights" };
	for (size_t c = 0; c < 3; c++)
	{
		const size_t q = offsetOf(qualityKeys[c]);
		const size_t w = offsetOf(weightKeys[c]);
		for (size_t i = 0; i < ArmorPieceCount; i++)
		{
			this->armorQuality[c][i] = exe[q + i];
			this->armorWeight[c][i] = readLE16(exe.data() + w + i * 2) / 256.0f;
		}
	}

	for (size_t i = 0; i < ArmorPieceCount; i++) this->armorNames[i] = i < armors.size() ? armors[i] : std::string();
	for (size_t i = 0; i < 4; i++) this->shieldArmorClass[i] = exe[offsetOf("Equipment.ShieldArmorClasses") + i];

	const auto materials = readStrings(exe, offsetOf("Equipment.MaterialNames"), MaterialCount);
	for (size_t i = 0; i < MaterialCount; i++)
	{
		this->materialNames[i] = i < materials.size() ? materials[i] : std::string();
		this->materialBonus[i] = static_cast<int8_t>(exe[offsetOf("Equipment.MaterialBonuses") + i]);
		this->materialChance[i] = exe[offsetOf("Equipment.MaterialChances") + i];
	}

	const auto potions = readStrings(exe, offsetOf("Equipment.PotionNames"), PotionTypeCount);
	for (size_t i = 0; i < PotionTypeCount; i++) this->potionNames[i] = i < potions.size() ? potions[i] : std::string();

	// Preços.
	const char *priceKeys[3] = { "Equipment.PlateArmorBasePrices", "Equipment.ChainArmorBasePrices", "Equipment.LeatherArmorBasePrices" };
	for (size_t c = 0; c < 3; c++)
		for (size_t i = 0; i < ArmorPieceCount; i++) this->armorPrice[c][i] = exe[offsetOf(priceKeys[c]) + i];
	for (size_t i = 0; i < MaterialCount; i++) this->materialPriceMultiplier[i] = readLE16(exe.data() + offsetOf("Equipment.MaterialPriceMultipliers") + i * 2);
	for (size_t i = 0; i < PotionTypeCount; i++) this->potionPrice[i] = readLE16(exe.data() + offsetOf("Equipment.PotionGoldPrices") + i * 2);
	const auto drinks = readStrings(exe, offsetOf("Services.TavernDrinks"), 15);
	for (size_t i = 0; i < 15; i++)
	{
		this->drinkNames[i] = i < drinks.size() ? drinks[i] : std::string();
		this->drinkPrice[i] = exe[offsetOf("Services.TavernDrinkGoldPrices") + i];
	}

	const auto animFiles = readStrings(exe, offsetOf("Equipment.WeaponAnimationFilenames"), 8);
	for (size_t i = 0; i < 8; i++) this->weaponAnimFiles[i] = i < animFiles.size() ? toUpperAscii(animFiles[i]) : std::string();

	// Itens mágicos de feitiço (anéis, amuletos...): peças, chances, preço-base e cargas, e os feitiços de
	// ataque (15), defesa (9) e diversos (8), cada um com nome, feitiço do SPELLSG, qualidade e preço por carga.
	const auto bases = readStrings(exe, offsetOf("Equipment.SpellcastingItemNames"), MagicItemTypeCount);
	for (size_t i = 0; i < MagicItemTypeCount; i++)
	{
		this->magicItemNames[i] = i < bases.size() ? bases[i] : std::string();
		this->magicItemChance[i] = exe[offsetOf("Equipment.SpellcastingItemCumulativeChances") + i];
		this->magicItemPrice[i] = readLE16(exe.data() + offsetOf("Equipment.SpellcastingItemBasePrices") + i * 2);
		this->magicItemCharges[i] = { exe[offsetOf("Equipment.SpellcastingItemChargeRanges") + i * 2], exe[offsetOf("Equipment.SpellcastingItemChargeRanges") + i * 2 + 1] };
	}
	const char *groups[3] = { "Attack", "Defensive", "Misc" };
	for (size_t g = 0; g < 3; g++)
	{
		const std::string key = std::string("Equipment.SpellcastingItem") + groups[g] + "Spell";
		const int count = MagicSpellCounts[g];
		const auto names = readStrings(exe, offsetOf((key + "Names").c_str()), count);
		for (int i = 0; i < count; i++)
		{
			MagicSpell spell;
			spell.name = i < static_cast<int>(names.size()) ? names[static_cast<size_t>(i)] : std::string();
			spell.spell = exe[offsetOf((key + "Spells").c_str()) + i];
			spell.quality = exe[offsetOf((key + "Qualities").c_str()) + i];
			spell.pricePerCharge = readLE16(exe.data() + offsetOf((key + "PricesPerCharge").c_str()) + i * 2);
			this->magicSpells[g].push_back(spell);
		}
	}

	// Encantamentos de armas e armaduras (14 cada): nome, qualidade, feitiço (255 = bônus de atributo) e preço extra.
	const char *enchantGroups[2] = { "Weapon", "Armor" };
	for (size_t g = 0; g < 2; g++)
	{
		const std::string key = std::string("Equipment.") + enchantGroups[g] + "Enchantment";
		const auto names = readStrings(exe, offsetOf((key + "Names").c_str()), EnchantmentCount);
		for (int i = 0; i < EnchantmentCount; i++)
		{
			Enchantment e;
			e.name = i < static_cast<int>(names.size()) ? names[static_cast<size_t>(i)] : std::string();
			e.quality = exe[offsetOf((key + "Qualities").c_str()) + i];
			e.spell = exe[offsetOf((key + "Spells").c_str()) + i];
			e.bonusPrice = readLE16(exe.data() + offsetOf((key + "BonusPrices").c_str()) + i * 2);
			if (e.spell == 255)
			{
				// Bônus de atributo: qual, pelo nome ("of Strength", "of Will"...).
				static const char *Attributes[] = { "Strength", "Intelligence", "Will", "Agility", "Speed", "Endurance", "Personality", "Luck" };
				for (int a = 0; a < 8; a++) if (e.name.find(Attributes[a]) != std::string::npos) { e.attribute = a; break; }
			}
			this->enchantments[g].push_back(e);
		}
	}
	for (size_t i = 0; i < 3; i++) this->enchantmentChance[i] = exe[offsetOf("Equipment.EnchantmentChances") + i];

	const size_t lootAt = offsetOf("Entities.CreatureLootChances");
	for (size_t i = 0; i < 25 && lootAt + i * 4 + 4 <= exe.size(); i++) this->creatureLootChances.push_back(readLE32(exe.data() + lootAt + i * 4));
	return true;
}

float arena::itemWeight(const Item &item, const ItemTables &tables)
{
	switch (item.kind)
	{
	case ItemKind::Weapon: return tables.weaponWeight[static_cast<size_t>(item.type)];
	case ItemKind::Armor: return tables.armorWeight[static_cast<size_t>(item.armorClass)][static_cast<size_t>(item.type)];
	case ItemKind::Trinket: return item.artifact >= 0 ? artifact::weight(item.type) : 0.2f;
	default: return 0.5f;
	}
}

int arena::itemPrice(const Item &item, const ItemTables &tables)
{
	if (item.artifact >= 0) return artifact::price(item.artifact);
	const int enchantPrice = enchantmentOf(item, tables) ? enchantmentOf(item, tables)->bonusPrice : 0;
	if (item.kind == ItemKind::Trinket)
	{
		// Itens mágicos: preço-base da peça mais o preço por carga do feitiço (bônus: 500 por ponto).
		const int base = tables.magicItemPrice[static_cast<size_t>(std::clamp(item.type, 0, MagicItemTypeCount - 1))];
		if (item.magicGroup >= 0 && item.magicGroup < 3)
		{
			const auto &group = tables.magicSpells[static_cast<size_t>(item.magicGroup)];
			const int perCharge = item.magicIndex >= 0 && item.magicIndex < static_cast<int>(group.size()) ? group[static_cast<size_t>(item.magicIndex)].pricePerCharge : 0;
			return std::max(1, base + perCharge * item.charges);
		}
		return std::max(1, base + 500 * item.charges);
	}
	const int multiplier = std::max(1, tables.materialPriceMultiplier[static_cast<size_t>(std::clamp(item.material, 0, MaterialCount - 1))]);
	switch (item.kind)
	{
	case ItemKind::Weapon: return std::max(1, tables.weaponPrice[static_cast<size_t>(item.type)] * multiplier) + enchantPrice;
	case ItemKind::Armor:
	{
		const int base = tables.armorPrice[static_cast<size_t>(item.armorClass)][static_cast<size_t>(item.type)];
		const bool metal = item.armorClass == ArmorClass::Plate || item.isShield();
		return std::max(1, metal ? base * multiplier : base * 4) + enchantPrice;
	}
	default: return tables.potionPrice[static_cast<size_t>(item.type)];
	}
}

bool arena::wears(const Item &item)
{
	return item.artifact < 0 && (item.kind == ItemKind::Weapon || item.kind == ItemKind::Armor);
}

int arena::maxCondition(const Item &item)
{
	if (item.kind == ItemKind::Armor && !item.isShield() && item.armorClass != ArmorClass::Plate) return item.armorClass == ArmorClass::Chain ? 80 : 50;
	return 40 + 20 * std::clamp(item.material, 0, MaterialCount - 1);
}

bool arena::isBroken(const Item &item)
{
	return wears(item) && item.wear >= maxCondition(item);
}

int arena::conditionPercent(const Item &item)
{
	return wears(item) ? std::clamp(100 - item.wear * 100 / std::max(1, maxCondition(item)), 0, 100) : 100;
}

int arena::weaponMinDamage(const Item &item, const ItemTables &tables)
{
	if (isBroken(item)) return 1;
	// O material soma 1 de dano a cada 5 pontos de bônus (Ferro tem -1).
	return std::max(1, tables.weaponMinDamage[static_cast<size_t>(item.type)] + tables.materialBonus[static_cast<size_t>(item.material)] / 5);
}

int arena::weaponMaxDamage(const Item &item, const ItemTables &tables)
{
	if (isBroken(item)) return 2;
	return std::max(weaponMinDamage(item, tables), tables.weaponMaxDamage[static_cast<size_t>(item.type)] + tables.materialBonus[static_cast<size_t>(item.material)] / 5);
}

int arena::armorRating(const Item &item, const ItemTables &tables)
{
	if (item.kind != ItemKind::Armor || isBroken(item))
	{
		return 0;
	}

	if (item.isShield())
	{
		// Escudos: classe de armadura do executável (5..20) em pontos de proteção.
		return tables.shieldArmorClass[static_cast<size_t>(item.type - 7)] / 5;
	}

	int rating = tables.armorQuality[static_cast<size_t>(item.armorClass)][static_cast<size_t>(item.type)];
	if (item.armorClass == ArmorClass::Plate) rating += tables.materialBonus[static_cast<size_t>(item.material)] / 5;
	return std::max(0, rating);
}

int arena::bodyPartOf(const Item &item)
{
	return (item.kind == ItemKind::Armor && !item.isShield()) ? item.type : -1;
}

std::string arena::weaponAnimationFile(int weaponType, const ItemTables &tables)
{
	// Índices do original: cajado, lâminas, maça, mangual, martelo, machados, arcos.
	constexpr int FileIndices[WeaponTypeCount] = { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 5, 6, 6 };
	const int index = (weaponType < 0 || weaponType >= WeaponTypeCount) ? 7 : FileIndices[weaponType];
	return tables.weaponAnimFiles[static_cast<size_t>(index)];
}

std::vector<arena::Item> arena::startingKit(int classIndex, int armorAllowance)
{
	// Arma inicial por classe (o original sorteia; aqui uma escolha fixa e coerente).
	constexpr int Weapons[18] = { 0, 2, 0, 0, 11, 2, 2, 1, 4, 1, 2, 7, -1, 17, 2, 15, 5, 5 }; // Arqueiro: arco longo
	std::vector<Item> kit;
	if (classIndex >= 0 && classIndex < 18 && Weapons[classIndex] >= 0)
	{
		Item weapon;
		weapon.kind = ItemKind::Weapon;
		weapon.type = Weapons[classIndex];
		weapon.material = 1; // aço
		weapon.equipped = true;
		kit.push_back(weapon);
	}

	auto armor = [&kit](int piece, ArmorClass armorClass)
	{
		Item item;
		item.kind = ItemKind::Armor;
		item.type = piece;
		item.armorClass = armorClass;
		item.material = 0;
		item.equipped = true;
		kit.push_back(item);
	};

	if (armorAllowance == 0) { armor(0, ArmorClass::Chain); armor(5, ArmorClass::Chain); }
	else if (armorAllowance == 1) { armor(0, ArmorClass::Leather); armor(6, ArmorClass::Leather); }
	else if (armorAllowance == 2) { armor(0, ArmorClass::Leather); }

	kit.push_back(potion(2)); // Poção de Cura
	return kit;
}

arena::Item arena::randomMagicItem(const ItemTables &tables, int maxQuality, std::mt19937 &rng)
{
	Item item;
	item.kind = ItemKind::Trinket;
	const int roll = randomInt(rng, 0, 99);
	item.type = MagicItemTypeCount - 1;
	for (int i = 0; i < MagicItemTypeCount; i++) if (roll < tables.magicItemChance[static_cast<size_t>(i)]) { item.type = i; break; }
	const auto [lo, hi] = tables.magicItemCharges[static_cast<size_t>(item.type)];
	item.magicGroup = randomInt(rng, 0, 2);
	std::vector<int> allowed;
	const auto &group = tables.magicSpells[static_cast<size_t>(item.magicGroup)];
	for (size_t i = 0; i < group.size(); i++) if (group[i].quality <= std::max(3, maxQuality)) allowed.push_back(static_cast<int>(i));
	item.magicIndex = allowed.empty() ? 0 : allowed[static_cast<size_t>(randomInt(rng, 0, static_cast<int>(allowed.size()) - 1))];
	item.charges = randomInt(rng, lo, std::max(lo, hi - 1)); // a tabela guarda o máximo + 1
	return item;
}

const arena::ItemTables::Enchantment *arena::enchantmentOf(const Item &item, const ItemTables &tables)
{
	if (item.enchant < 0 || item.artifact >= 0 || (item.kind != ItemKind::Weapon && item.kind != ItemKind::Armor)) return nullptr;
	const auto &list = tables.enchantments[item.kind == ItemKind::Weapon ? 0 : 1];
	return item.enchant < static_cast<int>(list.size()) ? &list[static_cast<size_t>(item.enchant)] : nullptr;
}

int arena::enchantmentSpell(const Item &item, const ItemTables &tables)
{
	const ItemTables::Enchantment *e = enchantmentOf(item, tables);
	return e && e->spell < 64 ? e->spell : -1; // 255 = atributo; 193 (Saltar) não é feitiço do livro
}

int arena::enchantmentAttribute(const Item &item, const ItemTables &tables, int &bonus)
{
	const ItemTables::Enchantment *e = enchantmentOf(item, tables);
	if (!e || e->attribute < 0) return -1;
	bonus = std::max(3, e->quality / 2); // a qualidade dá o tamanho do bônus (aproximação: +3 a +6)
	return e->attribute;
}

void arena::makeSpecial(Item &item, const ItemTables &tables, int maxQuality, std::mt19937 &rng)
{
	if (item.kind != ItemKind::Weapon && item.kind != ItemKind::Armor) return;
	const int roll = randomInt(rng, 1, 10);
	const bool material = roll <= tables.enchantmentChance[0] || roll > tables.enchantmentChance[1];
	const bool enchant = roll > tables.enchantmentChance[0];
	if (material && (item.kind == ItemKind::Weapon || item.armorClass == ArmorClass::Plate || item.isShield())) item.material = randomInt(rng, 3, MaterialCount - 1);
	if (!enchant) return;
	std::vector<int> allowed;
	const auto &list = tables.enchantments[item.kind == ItemKind::Weapon ? 0 : 1];
	for (size_t i = 0; i < list.size(); i++) if (list[i].quality <= maxQuality && (list[i].spell < 64 || list[i].attribute >= 0)) allowed.push_back(static_cast<int>(i));
	if (!allowed.empty()) item.enchant = allowed[static_cast<size_t>(randomInt(rng, 0, static_cast<int>(allowed.size()) - 1))];
}

int arena::magicItemSpell(const Item &item, const ItemTables &tables)
{
	if (!item.isMagicItem() || item.magicGroup > 2) return -1;
	const auto &group = tables.magicSpells[static_cast<size_t>(item.magicGroup)];
	return item.magicIndex >= 0 && item.magicIndex < static_cast<int>(group.size()) ? group[static_cast<size_t>(item.magicIndex)].spell : -1;
}

arena::Loot arena::generateLoot(LootSource source, int creatureIndex, const ItemTables &tables, std::mt19937 &rng, int playerLevel)
{
	Loot loot;
	auto chance = [&rng](int percent) { return randomInt(rng, 0, 99) < percent; };
	const int magicQuality = playerLevel + 2;
	// Equipamento especial (material raro e/ou encantamento) em parte do saque.
	auto equipment = [&]() { Item e = randomEquipment(tables, rng); if (chance(15)) makeSpecial(e, tables, playerLevel + 6, rng); return e; };
	switch (source)
	{
	case LootSource::TreasurePile:
		loot.gold = randomInt(rng, 5, 40);
		if (chance(30)) loot.items.push_back(potion(chance(85) ? 2 : 8));
		if (chance(15)) loot.items.push_back(equipment());
		if (chance(8)) loot.items.push_back(randomMagicItem(tables, magicQuality, rng));
		break;
	case LootSource::EquipmentPile:
		loot.items.push_back(equipment());
		if (chance(40)) loot.items.push_back(equipment());
		if (chance(20)) loot.gold = randomInt(rng, 1, 15);
		break;
	case LootSource::DeadBody:
		loot.gold = randomInt(rng, 0, 12);
		if (chance(30)) loot.items.push_back(equipment());
		if (chance(20)) loot.items.push_back(potion(2));
		break;
	case LootSource::Chest:
		loot.gold = randomInt(rng, 15, 60);
		loot.items.push_back(equipment());
		if (chance(40)) loot.items.push_back(potion(chance(80) ? 2 : 8));
		if (chance(12)) loot.items.push_back(randomMagicItem(tables, magicQuality, rng));
		break;
	case LootSource::Creature:
	{
		// Bytes da tabela por criatura: ouro, poção, equipamento, item mágico.
		const size_t index = static_cast<size_t>(creatureIndex + 1);
		const uint32_t chances = index < tables.creatureLootChances.size() ? tables.creatureLootChances[index] : 0;
		if (chance(static_cast<int>(chances & 0xFF))) loot.gold = randomInt(rng, 2, 25);
		if (chance(static_cast<int>((chances >> 8) & 0xFF))) loot.items.push_back(potion(2));
		if (chance(static_cast<int>((chances >> 24) & 0xFF))) loot.items.push_back(randomMagicItem(tables, magicQuality, rng));
		if (chance(static_cast<int>((chances >> 16) & 0xFF))) loot.items.push_back(equipment());
		break;
	}
	}

	return loot;
}
