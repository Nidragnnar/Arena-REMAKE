#pragma once

#include <array>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "../core/Bytes.h"

namespace arena
{
	enum class ItemKind : uint8_t { Weapon, Armor, Potion };

	// Classes de armadura do original (placas, malha, couro).
	enum class ArmorClass : uint8_t { Plate = 0, Chain = 1, Leather = 2 };

	constexpr int WeaponTypeCount = 18;     // Cajado .. Arco longo
	constexpr int ArmorPieceCount = 11;     // Couraça .. Escudo torre (7..10 = escudos)
	constexpr int MaterialCount = 8;        // Ferro .. Ébano
	constexpr int PotionTypeCount = 15;
	constexpr int BodyPartCount = 7;        // peito, mãos, pernas, ombro E, ombro D, cabeça, pés

	struct Item
	{
		ItemKind kind = ItemKind::Weapon;
		int type = 0;                         // id da arma, peça de armadura ou poção
		ArmorClass armorClass = ArmorClass::Leather;
		int material = 1;                     // metais (armas e placas); 1 = Aço
		bool equipped = false;

		bool isShield() const { return this->kind == ItemKind::Armor && this->type >= 7; }
	};

	// Tabelas de itens do ACD.EXE (seção Equipment).
	struct ItemTables
	{
		std::array<std::string, WeaponTypeCount> weaponNames;
		std::array<int, WeaponTypeCount> weaponMinDamage{}, weaponMaxDamage{};
		std::array<float, WeaponTypeCount> weaponWeight{};
		std::array<int, WeaponTypeCount> weaponPrice{};
		std::array<int, WeaponTypeCount> weaponHands{};

		std::array<std::string, ArmorPieceCount> armorNames;
		std::array<std::array<int, ArmorPieceCount>, 3> armorQuality{};  // por classe (placas/malha/couro)
		std::array<std::array<float, ArmorPieceCount>, 3> armorWeight{};
		std::array<int, 4> shieldArmorClass{};

		std::array<std::string, MaterialCount> materialNames;
		std::array<int, MaterialCount> materialBonus{};
		std::array<int, MaterialCount> materialChance{};  // acumulado (0..100)

		std::array<std::string, PotionTypeCount> potionNames;
		std::array<std::array<int, ArmorPieceCount>, 3> armorPrice{}; // por classe (placas/malha/couro)
		std::array<int, MaterialCount> materialPriceMultiplier{};    // Ferro 3 .. Ébano 2000 (Aço = 4)
		std::array<int, PotionTypeCount> potionPrice{};
		std::array<std::string, 15> drinkNames;                       // taverna
		std::array<int, 15> drinkPrice{};
		std::array<std::string, 8> weaponAnimFiles;       // staff.cif .. hand.cif
		std::vector<uint32_t> creatureLootChances;        // por índice de criatura + 1

		bool load(const ByteBuffer &exe, const std::function<size_t(const char*)> &offsetOf);
	};

	// Peso (kg), dano e proteção conforme as tabelas.
	float itemWeight(const Item &item, const ItemTables &tables);
	// Preço de compra numa loja: base do executável x multiplicador do material (Ferro 3, Aço 4 .. Ébano 2000).
	int itemPrice(const Item &item, const ItemTables &tables);
	int weaponMinDamage(const Item &item, const ItemTables &tables);
	int weaponMaxDamage(const Item &item, const ItemTables &tables);
	int armorRating(const Item &item, const ItemTables &tables);
	int bodyPartOf(const Item &item); // -1 para escudos e não-armaduras
	std::string weaponAnimationFile(int weaponType, const ItemTables &tables); // "" = punhos

	// Equipamento inicial por classe e geração de saque (aproximação das regras do original).
	std::vector<Item> startingKit(int classIndex, int armorAllowance);
	struct Loot
	{
		int gold = 0;
		std::vector<Item> items;
	};

	enum class LootSource { TreasurePile, EquipmentPile, DeadBody, Chest, Creature };
	Loot generateLoot(LootSource source, int creatureIndex, const ItemTables &tables, std::mt19937 &rng);
}
