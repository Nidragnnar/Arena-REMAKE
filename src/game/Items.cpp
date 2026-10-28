// Itens: tabelas do ACD.EXE (offsets e significados via OpenTESArena, MIT).

#include <algorithm>

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
			item.type = randomInt(rng, 0, 15); // arcos ficam de fora até haver combate à distância
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
	default: return 0.5f;
	}
}

int arena::itemPrice(const Item &item, const ItemTables &tables)
{
	const int multiplier = std::max(1, tables.materialPriceMultiplier[static_cast<size_t>(std::clamp(item.material, 0, MaterialCount - 1))]);
	switch (item.kind)
	{
	case ItemKind::Weapon: return std::max(1, tables.weaponPrice[static_cast<size_t>(item.type)] * multiplier);
	case ItemKind::Armor:
	{
		const int base = tables.armorPrice[static_cast<size_t>(item.armorClass)][static_cast<size_t>(item.type)];
		const bool metal = item.armorClass == ArmorClass::Plate || item.isShield();
		return std::max(1, metal ? base * multiplier : base * 4);
	}
	default: return tables.potionPrice[static_cast<size_t>(item.type)];
	}
}

int arena::weaponMinDamage(const Item &item, const ItemTables &tables)
{
	// O material soma 1 de dano a cada 5 pontos de bônus (Ferro tem -1).
	return std::max(1, tables.weaponMinDamage[static_cast<size_t>(item.type)] + tables.materialBonus[static_cast<size_t>(item.material)] / 5);
}

int arena::weaponMaxDamage(const Item &item, const ItemTables &tables)
{
	return std::max(weaponMinDamage(item, tables), tables.weaponMaxDamage[static_cast<size_t>(item.type)] + tables.materialBonus[static_cast<size_t>(item.material)] / 5);
}

int arena::armorRating(const Item &item, const ItemTables &tables)
{
	if (item.kind != ItemKind::Armor)
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
	constexpr int Weapons[18] = { 0, 2, 0, 0, 11, 2, 2, 1, 4, 1, 2, 7, -1, 2, 2, 15, 5, 5 };
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

arena::Loot arena::generateLoot(LootSource source, int creatureIndex, const ItemTables &tables, std::mt19937 &rng)
{
	Loot loot;
	auto chance = [&rng](int percent) { return randomInt(rng, 0, 99) < percent; };
	switch (source)
	{
	case LootSource::TreasurePile:
		loot.gold = randomInt(rng, 5, 40);
		if (chance(30)) loot.items.push_back(potion(chance(85) ? 2 : 8));
		if (chance(15)) loot.items.push_back(randomEquipment(tables, rng));
		break;
	case LootSource::EquipmentPile:
		loot.items.push_back(randomEquipment(tables, rng));
		if (chance(40)) loot.items.push_back(randomEquipment(tables, rng));
		if (chance(20)) loot.gold = randomInt(rng, 1, 15);
		break;
	case LootSource::DeadBody:
		loot.gold = randomInt(rng, 0, 12);
		if (chance(30)) loot.items.push_back(randomEquipment(tables, rng));
		if (chance(20)) loot.items.push_back(potion(2));
		break;
	case LootSource::Chest:
		loot.gold = randomInt(rng, 15, 60);
		loot.items.push_back(randomEquipment(tables, rng));
		if (chance(40)) loot.items.push_back(potion(chance(80) ? 2 : 8));
		break;
	case LootSource::Creature:
	{
		// Bytes da tabela por criatura: ouro, poção, equipamento, item mágico (tratado como poção).
		const size_t index = static_cast<size_t>(creatureIndex + 1);
		const uint32_t chances = index < tables.creatureLootChances.size() ? tables.creatureLootChances[index] : 0;
		if (chance(static_cast<int>(chances & 0xFF))) loot.gold = randomInt(rng, 2, 25);
		if (chance(static_cast<int>((chances >> 8) & 0xFF)) || chance(static_cast<int>((chances >> 24) & 0xFF))) loot.items.push_back(potion(2));
		if (chance(static_cast<int>((chances >> 16) & 0xFF))) loot.items.push_back(randomEquipment(tables, rng));
		break;
	}
	}

	return loot;
}
