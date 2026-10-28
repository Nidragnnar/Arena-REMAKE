#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "../assets/AssetManager.h"
#include "../render/ImageOps.h"
#include "ArenaRandom.h"
#include "Items.h"
#include "Spells.h"

namespace arena
{
	enum class ClassCategory { Mage = 0, Thief = 1, Warrior = 2 };

	// Tipos de armadura permitidos (valor do ACD.EXE).
	enum class ArmorAllowance { All = 0, LeatherAndChain = 1, LeatherOnly = 2, None = 3 };

	struct ClassDefinition
	{
		std::string originalName;       // nome em inglês do executável
		std::string originalPreferred;  // "intelligent and willful", ...
		ClassCategory category = ClassCategory::Mage;
		ArmorAllowance armor = ArmorAllowance::All;
		int healthDie = 0;
		bool castsMagic = false;
		bool isThief = false;
		bool criticalHit = false;
		double spellPointsMultiplier = 0.0;
		int initialExperienceCap = 0;
		int thievingDivisor = 0;
	};

	// Criatura (tabelas "Creature*" do ACD.EXE). Índice = *ITEM do .INF - 32.
	struct CreatureDefinition
	{
		std::string originalName;
		std::string animFilename; // ex.: "RAT@.CFA" (@ = direção 1..6)
		int level = 1;
		int minHp = 1, maxHp = 1;
		int minDamage = 1, maxDamage = 1;
		int scale = 256;          // 1/256 (0 = 100%)
		int yOffset = 0;          // unidades do Arena (negativo = flutua)
		int baseExperience = 0;
		int experienceMultiplier = 0;
		bool hasCorpse = true;
		std::string soundFile;    // ex.: "RATS.VOC"
	};

	struct ClassQuestion
	{
		std::string text;
		std::array<std::string, 3> choices;
		std::array<ClassCategory, 3> categories{};
	};

	// Área clicável de uma província no TAMRIEL.MNU (bits empacotados, MSB à esquerda).
	struct ProvinceMask
	{
		Rect rect;
		std::vector<uint8_t> bits;

		bool contains(int x, int y) const;
	};

	// Local do mapa-múndi (CITYDATA.65): 8 cidades-estado, 8 vilas e 16 aldeias por província.
	struct CityLocation
	{
		enum class Type { CityState, Town, Village, Dungeon };
		enum class Climate { Temperate, Desert, Mountain };

		std::string name;
		int province = 0;
		int localId = 0;  // 0..31 dentro da província
		int globalId = 0; // (província << 5) + localId
		int localX = 0, localY = 0; // ponto no mapa da província (320x200)
		int globalX = 0, globalY = 0; // ponto no mapa de Tamriel
		Type type = Type::CityState;
		Climate climate = Climate::Temperate;
		bool coastal = false;

		// Semente da cidade: (x local << 16) + y local, como no original.
		uint32_t seed() const { return (static_cast<uint32_t>(this->localX) << 16) + static_cast<uint32_t>(this->localY); }
		// Masmorras (locais 32..47): 32 = do pedaço do cajado, 33 = do mapa, 34.. = aleatórias.
		uint32_t dungeonSeed() const
		{
			const uint32_t s = (static_cast<uint32_t>(this->localY) << 16) + static_cast<uint32_t>(this->localX) + static_cast<uint32_t>(this->province);
			return ~((s << 5) | (s >> 27));
		}
		bool mainQuest() const { return this->type == Type::Dungeon && this->localId < 34; }
	};

	// Tabelas de geração de cidades do ACD.EXE ([CityGeneration]).
	struct CityGenTables
	{
		std::vector<uint8_t> coastalCities;                // IDs globais das cidades costeiras
		std::vector<std::string> templateNames;           // town%d.mif, townw%d.mif, ..., cityw%d.mif
		std::array<std::vector<uint8_t>, 5> wildBlocks;   // ermo: comum, vilarejo, masmorra, taverna, templo (WILDnnn.RMD)
		std::vector<std::pair<int, int>> startingPositions; // canto do primeiro bloco no molde
		std::vector<std::vector<uint8_t>> reservedBlocks;  // blocos do molde que não são sorteados
	};

	constexpr int AttributeCount = 8; // For, Int, Von, Agi, Vel, Res, Per, Sor
	constexpr int ClassCount = 18;
	constexpr int RaceCount = 8;
	constexpr int CenterProvinceID = 8;

	// Dados de regras extraídos do ACD.EXE, CLASSES.DAT, QUESTION.TXT e TAMRIEL.MNU.
	class ArenaData
	{
	public:
		bool load(const AssetManager &assets, const std::filesystem::path &dataDir, std::string &outError);

		const ClassDefinition &getClass(int index) const { return this->classes[static_cast<size_t>(index)]; }
		const std::vector<ClassQuestion> &getQuestions() const { return this->questions; }
		const ItemTables &getItems() const { return this->items; }
		const SpellLibrary &getSpells() const { return this->spells; }
		const CreatureDefinition *getCreature(int index) const
		{
			return (index >= 0 && index < static_cast<int>(this->creatures.size())) ? &this->creatures[static_cast<size_t>(index)] : nullptr;
		}

		// Classe sugerida pelo total de respostas de cada categoria (soma 10). -1 se inválido.
		// O CLASSES.DAT guarda as combinações na ordem (guerreiro, ladrão, mago).
		int classFromAnswers(int warrior, int thief, int mage) const;

		// Província sob o ponto (0..7), ignorando a Província Imperial e o botão "Exit".
		int provinceAt(int x, int y) const;

		// Atributos iniciais (0..100) por raça e gênero.
		std::array<int, AttributeCount> getBaseAttributes(int raceIndex, bool male) const;

		// Locais e geração de cidades.
		const CityLocation *getCity(int province, int localId) const;
		const CityGenTables &getCityGen() const { return this->cityGen; }

		// Dias de viagem entre dois locais pelo terreno do mapa-múndi e o mês (como no original,
		// em tempo bom; acima de 20 dias varia um pouco).
		int travelDays(const CityLocation &from, const CityLocation &to, int month) const;
		const std::vector<CityLocation> &getCities() const { return this->cities; }
		// Canto da província no mapa-múndi (semente das masmorras do ermo).
		std::pair<int, int> provinceOrigin(int province) const { return province >= 0 && province < 9 ? this->provinceOrigins[static_cast<size_t>(province)] : std::pair<int, int>{ 0, 0 }; }

		// Nome de morador pelas regras do original (NAMECHNK.DAT), por raça (0..8) e gênero.
		std::string npcName(int race, bool male, ArenaRandom &random) const;

	private:
		bool loadWorldMap(const AssetManager &assets, const ByteBuffer &exe, const std::function<size_t(const char *)> &offsetOf, std::string &outError);

		std::vector<CityLocation> cities;
		std::vector<std::vector<std::string>> nameChunks;
		std::array<std::pair<int, int>, 9> provinceOrigins{};
		std::vector<uint8_t> terrain;                     // TERRAIN.IMG (320x200)
		std::array<std::array<uint8_t, 12>, 7> climateSpeed{};
		std::array<std::array<uint8_t, 8>, 7> weatherSpeed{};
		CityGenTables cityGen;
		std::array<ClassDefinition, ClassCount> classes;
		std::array<uint8_t, ClassCount> generationClassIds{};
		std::array<std::array<uint8_t, 3>, 66> generationChoices{};
		std::array<std::array<uint8_t, AttributeCount>, RaceCount * 2> raceAttributes{};
		std::vector<ClassQuestion> questions;
		std::vector<ProvinceMask> provinceMasks;
		std::vector<CreatureDefinition> creatures;
		ItemTables items;
		SpellLibrary spells;
	};

	// Parser do formato do QUESTION.TXT (também usado pela tradução).
	std::vector<ClassQuestion> parseClassQuestions(const std::string &text);

	// Rolagem do Arena original: valor 0..n com distribuição aproximadamente normal.
	int rollClassic(int n, std::mt19937 &rng);

	// Bônus derivados dos atributos (fórmulas do executável, via OpenTESArena).
	inline int damageBonusFor(int strength) { return strength <= 43 ? 0 : (strength - 48) / 5; }
	inline int hitBonusFor(int agility)
	{
		if (agility <= 45) return -1;
		if (agility <= 46) return 0;
		return (agility - 50) / 5;
	}
}
