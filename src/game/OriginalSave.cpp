// Importação dos saves do Arena original (formato via OpenTESArena, ArenaTypes::SaveEngine).

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

#include "../render/Ui.h"
#include "Game.h"
#include "GameClock.h"
#include "Leveling.h"
#include "OriginalSave.h"

namespace
{
	using namespace arena;

	constexpr size_t NpcSize = 1054, PlayerDataSize = 2609;

	std::vector<uint8_t> readFile(const std::filesystem::path &path)
	{
		std::ifstream in(path, std::ios::binary);
		return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}

	// Arquivos do original em maiúsculas; procura também em minúsculas (cópias de outros sistemas).
	std::filesystem::path find(const std::filesystem::path &dir, const std::string &name)
	{
		std::error_code ec;
		for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
		{
			std::string file = entry.path().filename().string();
			std::transform(file.begin(), file.end(), file.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			if (file == name) return entry.path();
		}
		return {};
	}

	// Desembaralha um bloco: XOR com o contador (que começa no tamanho e desce) rodado por ele mesmo.
	void unscramble(uint8_t *data, size_t length)
	{
		uint16_t buffer = static_cast<uint16_t>(length);
		for (size_t i = 0; i < length; i++)
		{
			const int shift = buffer & 0xF;
			const uint16_t rotated = static_cast<uint16_t>((buffer >> shift) | (buffer << ((16 - shift) & 15)));
			data[i] ^= static_cast<uint8_t>(rotated & 0xFF);
			buffer--;
		}
	}

	uint16_t le16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
	uint32_t le32(const uint8_t *p) { return le16(p) | (static_cast<uint32_t>(le16(p + 2)) << 16); }

	std::string text(const uint8_t *p, size_t n)
	{
		size_t e = 0;
		while (e < n && p[e] != 0) e++;
		return std::string(reinterpret_cast<const char *>(p), e);
	}

	struct Engine { std::vector<uint8_t> npc, player; };

	bool readEngine(const std::filesystem::path &arenaDir, int slot, Engine &out)
	{
		const std::filesystem::path path = find(arenaDir, "SAVEENGN.0" + std::to_string(slot - 1));
		if (path.empty()) return false;
		std::vector<uint8_t> bytes = readFile(path);
		if (bytes.size() < NpcSize + PlayerDataSize) return false;
		out.npc.assign(bytes.begin(), bytes.begin() + NpcSize);
		out.player.assign(bytes.begin() + NpcSize, bytes.begin() + NpcSize + PlayerDataSize);
		unscramble(out.npc.data(), out.npc.size());
		unscramble(out.player.data(), out.player.size());
		return true;
	}

	// NPCData (posições pelo tamanho de cada campo): semente 0, raça 4, classe 5, nível 6, mulher 7, cidade natal 8,
	// nome 9 (32), atributos atuais 41 e base 49 (8), vida 89/91, vigor 93, magia 102/104, rosto 107, inventário 110
	// (40 × 19), feitiços 870 (quantidade) e 871, ouro 1031, experiência 1033.
	constexpr size_t RaceAt = 4, ClassAt = 5, LevelAt = 6, FemaleAt = 7, HomeAt = 8, NameAt = 9, BaseAttrAt = 49;
	constexpr size_t HpAt = 89, MaxHpAt = 91, SpAt = 102, MaxSpAt = 104, FaceAt = 107, SpellCountAt = 870, SpellsAt = 871, GoldAt = 1031, ExpAt = 1033;
}

arena::original::Summary arena::original::summary(const std::filesystem::path &arenaDir, int slot)
{
	Summary s;
	Engine engine;
	if (slot < 1 || slot > 10 || !readEngine(arenaDir, slot, engine)) return s;
	const std::vector<uint8_t> names = readFile(find(arenaDir, "NAMES.DAT"));
	s.name = names.size() >= static_cast<size_t>(slot) * 48 ? text(names.data() + (slot - 1) * 48, 48) : std::string();
	if (s.name.empty() || s.name == "EMPTY") s.name = text(engine.npc.data() + NameAt, 32);
	s.level = std::clamp<int>(engine.npc[LevelAt], 1, 99);
	s.exists = true;
	return s;
}

bool arena::original::import(Game &game, const std::filesystem::path &arenaDir, int slot, std::string &outMap, std::string &outError)
{
	Engine engine;
	if (!readEngine(arenaDir, slot, engine))
	{
		outError = game.getText().get("Saves.OriginalMissing");
		return false;
	}

	const uint8_t *npc = engine.npc.data();
	const uint8_t *player = engine.player.data();
	const ArenaData &data = game.getData();
	CharacterDraft c;
	c.name = text(npc + NameAt, 32);
	c.raceIndex = npc[RaceAt];
	c.classIndex = npc[ClassAt] & 0x1F;
	c.male = npc[FemaleAt] == 0;
	c.portraitIndex = std::clamp<int>(npc[FaceAt], 0, 9);
	c.level = std::clamp<int>(npc[LevelAt], 1, 99);
	if (c.name.empty() || c.raceIndex >= RaceCount || c.classIndex >= ClassCount)
	{
		outError = game.getText().get("Saves.OriginalInvalid");
		return false;
	}

	const ClassDefinition &def = data.getClass(c.classIndex);
	for (int a = 0; a < AttributeCount; a++) c.attributes[static_cast<size_t>(a)] = std::clamp<int>(npc[BaseAttrAt + a], 1, 100);
	c.maxHealth = std::max(1, static_cast<int>(le16(npc + MaxHpAt)));
	c.health = std::clamp(static_cast<int>(le16(npc + HpAt)), 1, c.maxHealth);
	c.maxStamina = c.attributes[0] + c.attributes[5];
	c.maxSpellPoints = def.castsMagic ? static_cast<int>(le16(npc + MaxSpAt)) : 0;
	c.spellPoints = std::clamp(static_cast<int>(le16(npc + SpAt)), 0, c.maxSpellPoints);

	// Ouro: o do jogo (PlayerData, 4 bytes) quando faz sentido; senão o da ficha.
	const uint32_t gold = le32(player);
	c.gold = gold < 10000000u ? static_cast<int>(gold) : le16(npc + GoldAt);

	// Experiência: a da ficha, nunca abaixo do mínimo do nível atual.
	const uint32_t experience = le32(npc + ExpAt);
	c.experience = std::max(experience < 100000000u ? static_cast<int>(experience) : 0, experienceForLevel(def, c.level));

	// Feitiços conhecidos do livro padrão (os criados pelo jogador ficam no SPELLS.0x, ainda não lido).
	for (int i = 0; def.castsMagic && i < std::min<int>(npc[SpellCountAt], 32); i++)
	{
		const int spell = npc[SpellsAt + i];
		if (data.getSpells().find(spell) && std::find(c.spells.begin(), c.spells.end(), spell) == c.spells.end()) c.spells.push_back(spell);
	}
	c.spellbookKnown = true;
	c.readySpell = c.spells.empty() ? -1 : c.spells.front();
	c.inventory = startingKit(c.classIndex, static_cast<int>(def.armor));

	// Data (PlayerData: ano, mês, dia, hora, minuto a partir do byte 14) quando válida.
	const int month = le16(player + 16), day = le16(player + 18), hour = le16(player + 20), minute = le16(player + 22);
	if (month < 12 && day >= 1 && day <= 30 && hour < 24 && minute < 60)
		c.gameMinutes = ((month * clock::DaysPerMonth) + (day - 1)) * clock::MinutesPerDay + hour * 60.0 + minute;

	// Cidade atual: o bloco da cidade no PlayerData começa pelo nome dela (20 bytes), com a província 36 bytes
	// depois e o número do local 42 bytes depois. Procura um nome de cidade conhecido com esses números.
	const CityLocation *city = nullptr;
	for (size_t at = 0; at + 43 < engine.player.size() && !city; at++)
	{
		const int province = player[at + 36], local = player[at + 42];
		const CityLocation *candidate = province <= CenterProvinceID && local < 32 ? data.getCity(province, local) : nullptr;
		if (candidate && !candidate->name.empty() && text(player + at, 20) == candidate->name) city = candidate;
	}
	if (!city && npc[HomeAt] < 9 * 32) city = data.getCity(npc[HomeAt] >> 5, npc[HomeAt] & 31);
	if (!city) city = data.getCity(c.raceIndex, 0); // capital da província natal

	c.journal.push_back({ c.gameMinutes, std::string(), ui::format(game.getText().get("Journal.Imported"), { city ? city->name : std::string() }) });
	game.getCharacter() = c;
	outMap = city ? "WILD:" + std::to_string(city->province) + ":" + std::to_string(city->localId) : game.getText().get("World.StartDungeonMif");
	return true;
}
