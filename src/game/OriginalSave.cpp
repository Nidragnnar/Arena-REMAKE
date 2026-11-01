// Importação dos saves do Arena original (formato via OpenTESArena, ArenaTypes::SaveEngine).

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

#include "../render/Ui.h"
#include "Game.h"
#include "GameClock.h"
#include "Artifacts.h"
#include "Leveling.h"
#include "MainQuest.h"
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
		// A ficha e os dados do jogo são embaralhados juntos, numa passada só (3663 bytes), como faz o
		// ArenaSaveEditor (que edita saves reais).
		unscramble(bytes.data(), NpcSize + PlayerDataSize);
		out.npc.assign(bytes.begin(), bytes.begin() + NpcSize);
		out.player.assign(bytes.begin() + NpcSize, bytes.begin() + NpcSize + PlayerDataSize);
		return true;
	}

	// NPCData (posições pelo tamanho de cada campo): semente 0, raça 4, classe 5, nível 6, mulher 7, cidade natal 8,
	// nome 9 (32), atributos atuais 41 e base 49 (8), vida 89/91, vigor 93, magia 102/104, rosto 107, inventário 110
	// (40 × 19), feitiços 870 (quantidade) e 871, ouro 1031, experiência 1033.
	constexpr size_t RaceAt = 4, ClassAt = 5, LevelAt = 6, FemaleAt = 7, HomeAt = 8, NameAt = 9, BaseAttrAt = 49;
	constexpr size_t HpAt = 89, MaxHpAt = 91, SpAt = 102, MaxSpAt = 104, FaceAt = 107, InventoryAt = 110, ItemSize = 19;
	constexpr size_t SpellCountAt = 870, SpellsAt = 871, GoldAt = 1031, ExpAt = 1033;
	// PlayerData (wiki do OpenTESArena; as missões do palácio têm 52 bytes, o que fecha os 2609 bytes):
	// cidade atual 1120, missão principal 2484 (10 bandeiras) e a busca de artefato 2587 (AQDATA: artefato atual,
	// bandeiras em +9 e os achados em +10, um bit por artefato na ordem dos nomes do executável).
	constexpr size_t MainQuestAt = 2484, ArtifactQuestAt = 2587;
	// Ordem dos artefatos no executável (e no ARTFACT1.DAT) -> índice no remake (Artifacts.h).
	constexpr int ArtifactOrder[16] = { artifact::LordsMail, artifact::OghmaInfinium, artifact::Chrysamere, artifact::StaffOfMagnus,
		artifact::Volendrung, artifact::SpellBreaker, artifact::NecromancersAmulet, artifact::AurielsShield, artifact::EbonyBlade,
		artifact::WarlocksRing, artifact::AurielsBow, artifact::SkeletonsKey, artifact::RingOfPhynaster, artifact::OrgnumsCoffer,
		artifact::EbonyMail, artifact::RingOfKhajiiti };

	// Item do inventário (19 bytes): SlotID, peso (2), mãos, Param1, Param2, vida (2), vida máxima (2), preço (4),
	// bandeiras "evttccnm" (equipado, em uso, subtipo, classe 0 armadura/1 arma/2 bugiganga/3 poção, não
	// identificado, tem material), X, material, Y, atributo. Armas: Param1/Param2 = dano; armaduras: Param1 = proteção.
	std::vector<Item> decodeInventory(const uint8_t *npc, const ItemTables &tables)
	{
		std::vector<Item> items;
		for (size_t i = 0; i < 40; i++)
		{
			const uint8_t *p = npc + InventoryAt + i * ItemSize;
			const uint8_t flags = p[14];
			if ((flags & 0x40) == 0) continue; // espaço vazio
			const int kind = (flags >> 2) & 3, subtype = (flags >> 4) & 3;
			Item item;
			item.equipped = (flags & 0x80) != 0;
			item.material = (flags & 1) ? std::clamp<int>(p[16], 0, MaterialCount - 1) : 1;
			// X = encantamento (1..13; o 0 não se distingue de "sem encantamento" e fica de fora).
			if ((kind == 0 || kind == 1) && p[15] >= 1 && p[15] < EnchantmentCount) item.enchant = p[15];
			if (kind == 1)
			{
				// Arma: o tipo cujo dano de tabela (com o bônus do material) bate com o gravado; senão o SlotID.
				item.kind = ItemKind::Weapon;
				item.type = p[0] < WeaponTypeCount ? p[0] : -1;
				for (int t = 0; t < WeaponTypeCount; t++)
				{
					Item probe = item;
					probe.type = t;
					if (weaponMinDamage(probe, tables) == p[4] && weaponMaxDamage(probe, tables) == p[5]) { item.type = t; break; }
				}
				if (item.type < 0) continue;
			}
			else if (kind == 0)
			{
				// Armadura: peça pelo SlotID, classe (placas, malha, couro) pelo subtipo.
				item.kind = ItemKind::Armor;
				item.type = p[0];
				item.armorClass = static_cast<ArmorClass>(std::min(subtype, 2));
				if (item.type >= ArmorPieceCount) continue;
				if (item.armorClass != ArmorClass::Plate && !item.isShield()) item.material = 1;
			}
			else if (kind == 3)
			{
				// Poção: o tipo pelo SlotID (como nas armas e armaduras); fora da tabela, o primeiro com o preço gravado
				// (os preços se repetem entre tipos). O campo do peso é a quantidade.
				item.kind = ItemKind::Potion;
				item.type = p[0] < PotionTypeCount ? p[0] : -1;
				for (int t = 0; t < PotionTypeCount && item.type < 0; t++) if (tables.potionPrice[static_cast<size_t>(t)] == static_cast<int>(le32(p + 10))) item.type = t;
				if (item.type < 0) continue;
				for (int n = 1; n < std::clamp<int>(le16(p + 1), 1, 10); n++) items.push_back(item);
			}
			else if (kind == 2 && p[0] < MagicItemTypeCount)
			{
				// Bugiganga (Marca, Cristal, Braçadeiras, Anel): de feitiço (material = grupo, Y = feitiço,
				// mãos = cargas), de atributo (atributo, Y = bônus) ou de armadura (Y = bônus).
				item.kind = ItemKind::Trinket;
				item.type = p[0];
				item.material = 1;
				if (subtype == 0 && p[16] < 3 && p[17] < MagicSpellCounts[p[16]]) { item.magicGroup = p[16]; item.magicIndex = p[17]; item.charges = std::max<int>(1, p[3]); }
				else if (subtype == 1 && p[18] < AttributeCount) { item.magicGroup = 3; item.magicIndex = p[18]; item.charges = std::max<int>(1, p[17]); }
				else if (subtype == 2) { item.magicGroup = 4; item.charges = std::max<int>(1, p[17]); }
				else continue;
			}
			else continue;
			// Estado: a vida gravada da peça (atual / máxima) vira o desgaste na escala do remake.
			const int health = le16(p + 6), maxHealth = le16(p + 8);
			if (wears(item) && maxHealth > 0 && health < maxHealth) item.wear = maxCondition(item) * (maxHealth - health) / maxHealth;
			items.push_back(item);
		}
		return items;
	}
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
	// Atributos na escala interna do original (255 aparece como 100).
	for (int a = 0; a < AttributeCount; a++) c.attributes[static_cast<size_t>(a)] = std::clamp(npc[BaseAttrAt + a] * 100 / 255, 1, 100);
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

	// Feitiços conhecidos do livro padrão.
	for (int i = 0; def.castsMagic && i < std::min<int>(npc[SpellCountAt], 32); i++)
	{
		const int spell = npc[SpellsAt + i];
		if (data.getSpells().find(spell) && std::find(c.spells.begin(), c.spells.end(), spell) == c.spells.end()) c.spells.push_back(spell);
	}

	// Livro do jogador (SPELLS.0x, 32 registros no formato do SPELLSG): os iguais a um feitiço da guilda (nome e
	// efeitos) viram esse feitiço; os outros foram criados pelo jogador e entram como feitiços próprios.
	c.customSpells.clear();
	const std::vector<uint8_t> book = def.castsMagic ? readFile(find(arenaDir, "SPELLS.0" + std::to_string(slot - 1))) : std::vector<uint8_t>();
	for (size_t i = 0; (i + 1) * SpellRecordSize <= book.size() && c.spells.size() < 32; i++)
	{
		Spell spell = parseSpellRecord(book.data() + i * SpellRecordSize, -1);
		if (spell.originalName.empty() || !spell.effects[0].valid()) continue;
		int known = -1;
		for (const Spell &guild : data.getSpells().getSpells())
		{
			bool same = guild.originalName == spell.originalName && guild.target == spell.target;
			for (size_t e = 0; same && e < 3; e++)
				same = guild.effects[e].type == spell.effects[e].type && guild.effects[e].sub == spell.effects[e].sub && guild.effects[e].params == spell.effects[e].params;
			if (same) { known = guild.index; break; }
		}
		if (known < 0)
		{
			spell.index = CustomSpellBase + static_cast<int>(c.customSpells.size());
			known = spell.index;
			c.customSpells.push_back(spell);
		}
		if (std::find(c.spells.begin(), c.spells.end(), known) == c.spells.end()) c.spells.push_back(known);
	}
	setCustomSpells(c.customSpells);
	c.spellbookKnown = true;
	c.readySpell = c.spells.empty() ? -1 : c.spells.front();
	// Inventário do original (armas e armaduras); sem nada reconhecível, o equipamento inicial da classe.
	c.inventory = decodeInventory(npc, data.getItems());
	if (c.inventory.empty()) c.inventory = startingKit(c.classIndex, static_cast<int>(def.armor));

	// Artefatos: os achados entram no inventário (e não voltam a ser oferecidos); a busca em andamento continua.
	const uint8_t *aq = player + ArtifactQuestAt;
	const int found = le16(aq + 10);
	for (int i = 0; i < 16; i++)
	{
		if (((found >> i) & 1) == 0) continue;
		c.artifactsFound |= 1 << ArtifactOrder[i];
		c.inventory.push_back(artifact::makeItem(ArtifactOrder[i]));
	}
	if (aq[0] < 16 && aq[9] != 0 && ((found >> aq[0]) & 1) == 0) c.hunt.artifact = ArtifactOrder[aq[0]];

	// Missão principal (MQDATA): NextStep = pedaços do Cajado já encontrados; as bandeiras dizem em que passo
	// da busca do próximo pedaço o jogador está (aproximação: o significado exato de cada uma não é conhecido).
	const uint8_t *mq = player + MainQuestAt;
	if (mq[1] <= quest::PieceCount)
	{
		c.staffPieces = mq[1];
		c.questStep = mq[8] ? 0 : mq[7] ? 3 : mq[2] ? 2 : mq[4] ? 1 : 0; // pedaço, item da pista, masmorra, visão
	}

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
