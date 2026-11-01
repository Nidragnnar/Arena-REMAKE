#pragma once

#include <array>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "../core/Bytes.h"

namespace arena
{
	// Trinket: anéis, amuleto, chave, livro e cofre dos artefatos (type = índice do artefato).
	enum class ItemKind : uint8_t { Weapon, Armor, Potion, Trinket };

	// Classes de armadura do original (placas, malha, couro).
	enum class ArmorClass : uint8_t { Plate = 0, Chain = 1, Leather = 2 };

	constexpr int WeaponTypeCount = 18;     // Cajado .. Arco longo
	constexpr int ArmorPieceCount = 11;     // Couraça .. Escudo torre (7..10 = escudos)
	constexpr int MaterialCount = 8;        // Ferro .. Ébano
	constexpr int PotionTypeCount = 15;
	constexpr int BodyPartCount = 7;        // peito, mãos, pernas, ombro E, ombro D, cabeça, pés
	constexpr int MagicItemTypeCount = 4;   // peças dos itens mágicos (anel, amuleto...)
	constexpr int MagicSpellCounts[3] = { 15, 9, 8 }; // feitiços de ataque, defesa e diversos
	constexpr int EnchantmentCount = 14;    // encantamentos de armas e de armaduras

	struct Item
	{
		ItemKind kind = ItemKind::Weapon;
		int type = 0;                         // id da arma, peça de armadura ou poção
		ArmorClass armorClass = ArmorClass::Leather;
		int material = 1;                     // metais (armas e placas); 1 = Aço
		bool equipped = false;
		int artifact = -1;                    // artefato (ver Artifacts.h); -1 = item comum
		// Item mágico comum (Trinket sem artefato; type = peça: Marca, Cristal, Braçadeiras, Anel):
		// grupo 0..2 = feitiço de ataque/defesa/diversos (índice na tabela, charges = cargas);
		// 3 = bônus de atributo (índice = atributo, charges = bônus); 4 = bônus de armadura (charges = bônus).
		int magicGroup = -1, magicIndex = 0, charges = 0;
		int enchant = -1; // encantamento de arma ou armadura (índice na tabela do tipo), -1 = nenhum
		bool isMagicItem() const { return this->kind == ItemKind::Trinket && this->artifact < 0 && this->magicGroup >= 0; }

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

		// Itens mágicos de feitiço (SpellcastingItem* do executável).
		std::array<std::string, MagicItemTypeCount> magicItemNames;
		std::array<int, MagicItemTypeCount> magicItemChance{}, magicItemPrice{};
		std::array<std::pair<int, int>, MagicItemTypeCount> magicItemCharges{};
		struct MagicSpell { std::string name; int spell = 0, quality = 0, pricePerCharge = 0; };
		std::array<std::vector<MagicSpell>, 3> magicSpells; // ataque, defesa, diversos

		// Encantamentos de armas (0) e armaduras (1): feitiço 255 = bônus de atributo.
		struct Enchantment { std::string name; int quality = 0, spell = 0, bonusPrice = 0, attribute = -1; };
		std::array<std::vector<Enchantment>, 2> enchantments;
		std::array<int, 3> enchantmentChance{}; // 1..10: material especial, encantamento, os dois

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

	// Item mágico sorteado como no original: peça pela chance acumulada, cargas pela faixa da peça e um feitiço
	// do grupo com qualidade até maxQuality (nível de quem acha/compra).
	Item randomMagicItem(const ItemTables &tables, int maxQuality, std::mt19937 &rng);
	int magicItemSpell(const Item &item, const ItemTables &tables); // feitiço do SPELLSG (-1 se não for de feitiço)

	// Encantamentos de armas e armaduras: um feitiço (vai para o livro enquanto equipado) ou um bônus de atributo.
	const ItemTables::Enchantment *enchantmentOf(const Item &item, const ItemTables &tables);
	int enchantmentSpell(const Item &item, const ItemTables &tables);              // -1 = nenhum
	int enchantmentAttribute(const Item &item, const ItemTables &tables, int &bonus); // -1 = nenhum
	// Peça especial como no original (EnchantmentChances, 1..10): só material raro, só encantamento ou os dois;
	// encantamentos até a qualidade maxQuality.
	void makeSpecial(Item &item, const ItemTables &tables, int maxQuality, std::mt19937 &rng);

	// Equipamento inicial por classe e geração de saque (aproximação das regras do original).
	std::vector<Item> startingKit(int classIndex, int armorAllowance);
	struct Loot
	{
		int gold = 0;
		std::vector<Item> items;
	};

	enum class LootSource { TreasurePile, EquipmentPile, DeadBody, Chest, Creature };
	// playerLevel limita a qualidade dos itens mágicos que podem sair.
	Loot generateLoot(LootSource source, int creatureIndex, const ItemTables &tables, std::mt19937 &rng, int playerLevel = 1);
}
