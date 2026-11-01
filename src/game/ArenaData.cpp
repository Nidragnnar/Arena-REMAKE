// Regras da criação de personagem. Offsets e semântica das tabelas baseados na
// pesquisa do OpenTESArena (MIT).

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

#include "../core/KeyValueFile.h"
#include "ArenaData.h"

namespace
{
	constexpr int GenerationIdMask = 0x1F;
	constexpr int SpellcasterMask = 0x20;
	constexpr int CriticalHitMask = 0x40;
	constexpr int ThiefMask = 0x80;

	std::unordered_map<std::string, size_t> loadExeOffsets(const std::filesystem::path &path)
	{
		std::unordered_map<std::string, size_t> offsets;
		arena::parseKeyValueFile(path, [&offsets](const std::string &key, const std::string &value)
		{
			offsets[key] = std::strtoul(value.c_str(), nullptr, 16);
		});

		return offsets;
	}

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

	std::string trimCopy(const std::string &s)
	{
		return arena::trimWhitespace(s);
	}
}

bool arena::ProvinceMask::contains(int x, int y) const
{
	if (!this->rect.contains(x, y))
	{
		return false;
	}

	const int relX = x - this->rect.x;
	const int relY = y - this->rect.y;
	const int stride = (this->rect.w + 7) / 8;
	const size_t index = static_cast<size_t>(relX / 8 + relY * stride);
	return index < this->bits.size() && (this->bits[index] & (1 << (7 - (relX % 8)))) != 0;
}

std::vector<arena::ClassQuestion> arena::parseClassQuestions(const std::string &text)
{
	std::vector<ClassQuestion> questions;
	std::istringstream stream(text);
	std::string line;
	int mode = -1; // -1 descrição, 0..2 alternativas a/b/c

	auto append = [](std::string &target, const std::string &piece)
	{
		if (!target.empty()) target += ' ';
		target += piece;
	};

	while (std::getline(stream, line))
	{
		std::string trimmed = trimCopy(line);
		if (trimmed.empty() || trimmed[0] == '#')
		{
			continue;
		}

		size_t digits = 0;
		while (digits < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[digits]))) digits++;

		if (digits > 0 && digits < trimmed.size() && trimmed[digits] == '.')
		{
			questions.emplace_back();
			mode = -1;
			append(questions.back().text, trimCopy(trimmed.substr(digits + 1)));
		}
		else if (trimmed.size() >= 2 && trimmed[1] == ')' && trimmed[0] >= 'a' && trimmed[0] <= 'c' && !questions.empty())
		{
			mode = trimmed[0] - 'a';
			append(questions.back().choices[static_cast<size_t>(mode)], trimCopy(trimmed.substr(2)));
		}
		else if (!questions.empty())
		{
			std::string &target = (mode < 0) ? questions.back().text : questions.back().choices[static_cast<size_t>(mode)];
			append(target, trimmed);
		}
	}

	// Extrai a marcação de categoria "(5l)", "(5c)", "(5v)" e a remove do texto.
	for (ClassQuestion &question : questions)
	{
		for (size_t i = 0; i < 3; i++)
		{
			std::string &choice = question.choices[i];
			const size_t tag = choice.rfind("(5");
			if (tag != std::string::npos && tag + 3 < choice.size())
			{
				const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(choice[tag + 2])));
				question.categories[i] = (c == 'l') ? ClassCategory::Mage : (c == 'c') ? ClassCategory::Thief : ClassCategory::Warrior;
				choice = trimCopy(choice.substr(0, tag) + choice.substr(std::min(choice.size(), tag + 4)));
			}
		}
	}

	return questions;
}

int arena::rollClassic(int n, std::mt19937 &rng)
{
	auto next = [&rng](int max) { return static_cast<int>(rng() % static_cast<uint32_t>(max)); };
	if (n <= 8)
	{
		return next(n);
	}

	int test = n;
	int dice = 0, sides = 0;
	while (true)
	{
		if (test % 6 == 0) { dice = test / 6; sides = 6; break; }
		if (test % 5 == 0) { dice = test / 5; sides = 5; break; }
		if (test % 4 == 0) { dice = test / 4; sides = 4; break; }
		test++;
	}

	int value = 0;
	do
	{
		value = 0;
		for (int i = 0; i < dice; i++) value += 1 + next(sides);
	} while (value > n);

	return value;
}

bool arena::ArenaData::load(const AssetManager &assets, const std::filesystem::path &dataDir, std::string &outError)
{
	const ByteBuffer &exe = assets.getExeImage();
	const auto offsets = loadExeOffsets(dataDir / "exe" / "acd_strings.txt");
	auto offsetOf = [&offsets](const char *key) -> size_t
	{
		const auto it = offsets.find(key);
		return it != offsets.end() ? it->second : 0;
	};

	const size_t namesOffset = offsetOf("CharacterClasses.ClassNames");
	const size_t prefOffset = offsetOf("CharacterClasses.PreferredAttributes");
	const size_t armorsOffset = offsetOf("CharacterClasses.AllowedArmors");
	const size_t idsOffset = offsetOf("CharacterClasses.ClassNumbersToIDs");
	const size_t diceOffset = offsetOf("CharacterClasses.HealthDice");
	const size_t expOffset = offsetOf("CharacterClasses.InitialExperienceCaps");
	const size_t thiefOffset = offsetOf("CharacterClasses.ThievingDivisors");
	const size_t magicOffset = offsetOf("CharacterClasses.MagicClassIntelligenceMultipliers");
	const size_t raceAttrOffset = offsetOf("Entities.RaceAttributes");
	if (namesOffset == 0 || raceAttrOffset == 0 || raceAttrOffset + RaceCount * 2 * AttributeCount > exe.size())
	{
		outError = "Tabelas de classes não encontradas no executável (acd_strings.txt).";
		return false;
	}

	const std::vector<std::string> names = readStrings(exe, namesOffset, ClassCount);
	const std::vector<std::string> preferred = readStrings(exe, prefOffset, ClassCount);
	for (int i = 0; i < ClassCount; i++)
	{
		ClassDefinition &def = this->classes[static_cast<size_t>(i)];
		def.originalName = i < static_cast<int>(names.size()) ? names[static_cast<size_t>(i)] : std::string();
		def.originalPreferred = i < static_cast<int>(preferred.size()) ? preferred[static_cast<size_t>(i)] : std::string();
		def.category = static_cast<ClassCategory>(i / 6);
		def.armor = static_cast<ArmorAllowance>(std::min<int>(exe[armorsOffset + i], 3));
		def.healthDie = exe[diceOffset + i];
		def.initialExperienceCap = readLE16(exe.data() + expOffset + i * 2);
		def.thievingDivisor = exe[thiefOffset + i];

		const int classNumber = exe[idsOffset + i];
		const int classId = classNumber & GenerationIdMask;
		def.castsMagic = (classNumber & SpellcasterMask) != 0;
		def.isThief = (classNumber & ThiefMask) != 0;
		def.criticalHit = (classNumber & CriticalHitMask) != 0;

		// Multiplicador de pontos de magia sobre a Inteligência (lógica do executável original).
		if (def.castsMagic)
		{
			def.spellPointsMultiplier = 1.0;
			if (classNumber != 0xE6)
			{
				double bonus = 1.0;
				const uint8_t modifier = exe[magicOffset + classId];
				if (classNumber == 0x23)
				{
					bonus = 2.0;
				}
				else if (modifier != 2)
				{
					if (modifier != 0) def.spellPointsMultiplier += 0.25;
					bonus = 0.5;
				}

				def.spellPointsMultiplier += bonus;
			}
		}
	}

	for (size_t i = 0; i < this->raceAttributes.size(); i++)
	{
		for (size_t a = 0; a < AttributeCount; a++)
		{
			this->raceAttributes[i][a] = exe[raceAttrOffset + i * AttributeCount + a];
		}
	}

	// Criaturas: 23 com nome + o chefe final (Jagar Tharn).
	{
		constexpr int CreatureCount = 24;
		const size_t namesAt = offsetOf("Entities.CreatureNames");
		const size_t animsAt = offsetOf("Entities.CreatureAnimationFilenames");
		const size_t levelsAt = offsetOf("Entities.CreatureLevels");
		const size_t hpAt = offsetOf("Entities.CreatureHitPoints");
		const size_t damageAt = offsetOf("Entities.CreatureDamages");
		const size_t scaleAt = offsetOf("Entities.CreatureScales");
		const size_t yOffsetAt = offsetOf("Entities.CreatureYOffsets");
		const size_t expAt = offsetOf("Entities.CreatureBaseExperience");
		const size_t multAt = offsetOf("Entities.CreatureExperienceMultipliers");
		const size_t corpseAt = offsetOf("Entities.CreatureHasNoCorpse");
		if (namesAt != 0 && animsAt != 0 && expAt + CreatureCount * 4 <= exe.size())
		{
			const std::vector<std::string> creatureNames = readStrings(exe, namesAt, CreatureCount - 1);
			const std::vector<std::string> animNames = readStrings(exe, animsAt, CreatureCount);
			const size_t soundsAt = offsetOf("Entities.CreatureSounds");
			const std::vector<std::string> soundNames = readStrings(exe, offsetOf("Entities.CreatureSoundNames"), 26);
			for (size_t i = 0; i < CreatureCount; i++)
			{
				CreatureDefinition def;
				def.originalName = i < creatureNames.size() ? creatureNames[i] : "Jagar Tharn";
				def.animFilename = i < animNames.size() ? toUpperAscii(animNames[i]) : std::string();
				def.level = exe[levelsAt + i];
				def.minHp = readLE16(exe.data() + hpAt + i * 4);
				def.maxHp = std::max(def.minHp, static_cast<int>(readLE16(exe.data() + hpAt + i * 4 + 2)));
				def.minDamage = exe[damageAt + i * 2];
				def.maxDamage = std::max(def.minDamage, static_cast<int>(exe[damageAt + i * 2 + 1]));
				const int scale = readLE16(exe.data() + scaleAt + i * 2);
				def.scale = (scale == 0) ? 256 : scale;
				def.yOffset = static_cast<int8_t>(exe[yOffsetAt + i]);
				def.baseExperience = static_cast<int>(readLE32(exe.data() + expAt + i * 4));
				def.experienceMultiplier = exe[multAt + i];
				def.hasCorpse = exe[corpseAt + i] == 0;
				const size_t soundIndex = (soundsAt != 0) ? exe[soundsAt + i] : 0;
				def.soundFile = soundIndex < soundNames.size() ? toUpperAscii(soundNames[soundIndex]) : std::string();
				this->creatures.push_back(std::move(def));
			}

			// Inimigos humanos: pedaços de nome por tipo de armadura, modelos e multiplicadores de experiência.
			const auto types = readStrings(exe, offsetOf("Entities.HumanFilenameTypes"), 7);
			const auto templates = readStrings(exe, offsetOf("Entities.HumanFilenameTemplates"), 3);
			for (size_t i = 0; i < 7 && i < types.size(); i++) this->humanTypes[i] = toUpperAscii(types[i]);
			for (size_t i = 0; i < 3 && i < templates.size(); i++) this->humanTemplates[i] = toUpperAscii(templates[i]);
			for (size_t i = 0; i < 3; i++) this->humanExpMultipliers[i] = exe[offsetOf("Entities.HumanEnemyExperienceMultipliers") + i];

			// Doenças (como no OpenTESArena): chance por criatura (o Vampiro tem 5% fixos), as 4 criaturas que
			// transmitem doença com a lista de cada uma (terminada em 0xFF) e as doenças de cura demorada. Os ids do
			// executável começam em 10 (26 = a 17ª doença, Febre do Mago); aqui viram índices de DiseaseNames.
			constexpr int DiseaseIdBase = 10;
			const size_t chancesAt = offsetOf("Entities.CreatureDiseaseChances"), giversAt = offsetOf("Entities.DiseaseGivingCreatureIDs");
			const size_t listsAt = offsetOf("Entities.CreatureDiseaseLists"), longAt = offsetOf("Entities.RandomHealingTimeDiseaseIDs");
			if (chancesAt != 0 && giversAt != 0 && listsAt != 0 && longAt != 0)
			{
				for (size_t i = 0; i < this->creatures.size(); i++) this->creatures[i].diseaseChance = i == 21 ? 5 : static_cast<int8_t>(exe[chancesAt + i]);
				size_t at = listsAt;
				for (size_t g = 0; g < 4; g++)
				{
					const size_t creature = static_cast<size_t>(exe[giversAt + g]) - 1;
					for (; at < exe.size() && exe[at] != 0xFF; at++)
						if (creature < this->creatures.size() && exe[at] >= DiseaseIdBase) this->creatures[creature].diseases.push_back(exe[at] - DiseaseIdBase);
					at++;
				}
				for (size_t i = 0; i < 3; i++) this->longDiseases.push_back(exe[longAt + i] - DiseaseIdBase);
				this->diseaseNames = readStrings(exe, offsetOf("Status.DiseaseNames"), 17);
			}
		}
	}

	const auto classesDat = assets.read("CLASSES.DAT");
	if (!classesDat || classesDat->size() < ClassCount + 66 * 3)
	{
		outError = "CLASSES.DAT ausente ou inválido.";
		return false;
	}

	if (!this->spells.load(assets))
	{
		outError = "Feitiços padrão (SPELLSG.65) não encontrados.";
		return false;
	}

	if (!this->items.load(exe, offsetOf))
	{
		outError = "Tabelas de itens não encontradas no executável.";
		return false;
	}

	for (size_t i = 0; i < ClassCount; i++) this->generationClassIds[i] = (*classesDat)[i];
	for (size_t i = 0; i < 66; i++)
		for (size_t j = 0; j < 3; j++)
			this->generationChoices[i][j] = (*classesDat)[ClassCount + i * 3 + j];

	// Perguntas: tradução PT-BR se existir; senão o QUESTION.TXT original.
	const auto translated = readWholeFile(dataDir / "lang" / "pt_BR_questions.txt");
	if (translated)
	{
		this->questions = parseClassQuestions(std::string(translated->begin(), translated->end()));
	}

	if (this->questions.size() < 10)
	{
		const auto original = assets.read("QUESTION.TXT");
		this->questions = original ? parseClassQuestions(std::string(original->begin(), original->end())) : std::vector<ClassQuestion>{};
	}

	if (this->questions.size() < 10)
	{
		outError = "Perguntas de classe (QUESTION.TXT) não encontradas.";
		return false;
	}

	// Máscaras das províncias dentro do TAMRIEL.MNU.
	const auto tamriel = assets.read("TAMRIEL.MNU");
	constexpr size_t MaskDataOffset = 0x87D5;
	constexpr Rect MaskRects[10] =
	{
		{ 37, 32, 86, 57 }, { 47, 53, 90, 62 }, { 113, 29, 88, 53 }, { 190, 31, 102, 93 },
		{ 31, 131, 65, 52 }, { 100, 118, 61, 55 }, { 144, 119, 50, 57 }, { 204, 116, 67, 67 },
		{ 103, 72, 131, 84 }, { 279, 188, 37, 11 }
	};

	if (!tamriel)
	{
		outError = "TAMRIEL.MNU não encontrado.";
		return false;
	}

	size_t offset = MaskDataOffset;
	for (const Rect &rect : MaskRects)
	{
		ProvinceMask mask;
		mask.rect = rect;
		const size_t byteCount = static_cast<size_t>(((rect.w + 7) / 8) * rect.h);
		if (offset + byteCount > tamriel->size())
		{
			outError = "Máscaras do mapa truncadas.";
			return false;
		}

		mask.bits.assign(tamriel->begin() + offset, tamriel->begin() + offset + byteCount);
		offset += byteCount;
		this->provinceMasks.push_back(std::move(mask));
	}

	// Pedaços de nomes (NAMECHNK.DAT): blocos de uint16 tamanho, uint8 quantidade e textos.
	if (const auto chunks = assets.read("NAMECHNK.DAT"))
	{
		size_t at = 0;
		while (at + 3 <= chunks->size())
		{
			const size_t length = (*chunks)[at] | ((*chunks)[at + 1] << 8);
			const int count = (*chunks)[at + 2];
			if (length == 0) break;
			std::vector<std::string> list;
			size_t p = at + 3;
			for (int i = 0; i < count && p < chunks->size(); i++)
			{
				std::string text;
				while (p < chunks->size() && (*chunks)[p] != 0) text.push_back(static_cast<char>((*chunks)[p++]));
				p++;
				list.push_back(std::move(text));
			}
			this->nameChunks.push_back(std::move(list));
			at += length;
		}
	}

	return this->loadWorldMap(assets, exe, offsetOf, outError);
}

std::string arena::ArenaData::npcName(int race, bool male, ArenaRandom &random) const
{
	// Regras do original (via OpenTESArena): índice da lista de pedaços, texto fixo, ou índice
	// com chance (%). Raças 0..7 = províncias; 8 = Província Imperial.
	struct Rule { int index; const char *text; int chance; };
	static const std::vector<Rule> Rules[9][2] =
	{
		{ { { 0, nullptr, -1 }, { 1, nullptr, -1 }, { -1, " ", -1 }, { 4, nullptr, -1 }, { 5, nullptr, -1 } }, { { 2, nullptr, -1 }, { 3, nullptr, -1 }, { -1, " ", -1 }, { 4, nullptr, -1 }, { 5, nullptr, -1 } } },
		{ { { 6, nullptr, -1 }, { 7, nullptr, -1 }, { 8, nullptr, -1 }, { 9, nullptr, 75 } }, { { 6, nullptr, -1 }, { 7, nullptr, -1 }, { 8, nullptr, -1 }, { 9, nullptr, 75 }, { 10, nullptr, -1 } } },
		{ { { 11, nullptr, -1 }, { 12, nullptr, -1 }, { -1, " ", -1 }, { 15, nullptr, -1 }, { 16, nullptr, -1 }, { -1, "sen", -1 } }, { { 13, nullptr, -1 }, { 14, nullptr, -1 }, { -1, " ", -1 }, { 15, nullptr, -1 }, { 16, nullptr, -1 }, { -1, "sen", -1 } } },
		{ { { 17, nullptr, -1 }, { 18, nullptr, -1 }, { -1, " ", -1 }, { 21, nullptr, -1 }, { 22, nullptr, -1 } }, { { 19, nullptr, -1 }, { 20, nullptr, -1 }, { -1, " ", -1 }, { 21, nullptr, -1 }, { 22, nullptr, -1 } } },
		{ { { 23, nullptr, -1 }, { 24, nullptr, -1 }, { -1, " ", -1 }, { 27, nullptr, -1 }, { 28, nullptr, -1 } }, { { 25, nullptr, -1 }, { 26, nullptr, -1 }, { -1, " ", -1 }, { 27, nullptr, -1 }, { 28, nullptr, -1 } } },
		{ { { 29, nullptr, -1 }, { 30, nullptr, -1 }, { -1, " ", -1 }, { 33, nullptr, -1 }, { 34, nullptr, -1 } }, { { 31, nullptr, -1 }, { 32, nullptr, -1 }, { -1, " ", -1 }, { 33, nullptr, -1 }, { 34, nullptr, -1 } } },
		{ { { 35, nullptr, -1 }, { 36, nullptr, -1 }, { -1, " ", -1 }, { 39, nullptr, -1 }, { 40, nullptr, -1 } }, { { 37, nullptr, -1 }, { 38, nullptr, -1 }, { -1, " ", -1 }, { 39, nullptr, -1 }, { 40, nullptr, -1 } } },
		{ { { 41, nullptr, -1 }, { 42, nullptr, -1 }, { -1, " ", -1 }, { 45, nullptr, -1 }, { 46, nullptr, -1 } }, { { 43, nullptr, -1 }, { 44, nullptr, -1 }, { -1, " ", -1 }, { 45, nullptr, -1 }, { 46, nullptr, -1 } } },
		{ { { 47, nullptr, -1 }, { 48, nullptr, 75 }, { 49, nullptr, -1 } }, { { 47, nullptr, -1 }, { 48, nullptr, 75 }, { 49, nullptr, -1 } } }
	};

	std::string name;
	for (const Rule &rule : Rules[std::clamp(race, 0, 8)][male ? 0 : 1])
	{
		if (rule.index < 0) { name += rule.text; continue; }
		if (rule.index >= static_cast<int>(this->nameChunks.size()) || this->nameChunks[static_cast<size_t>(rule.index)].empty()) continue;
		if (rule.chance >= 0 && (random.next() % 100) > rule.chance) continue;
		const auto &list = this->nameChunks[static_cast<size_t>(rule.index)];
		name += list[static_cast<size_t>(random.next()) % list.size()];
	}

	return name;
}

bool arena::ArenaData::loadWorldMap(const AssetManager &assets, const ByteBuffer &exe, const std::function<size_t(const char *)> &offsetOf, std::string &outError)
{
	// Tabelas de geração de cidades.
	const size_t coastalAt = offsetOf("CityGeneration.CoastalCityList");
	const size_t templatesAt = offsetOf("CityGeneration.CityTemplateFilenames");
	const size_t startsAt = offsetOf("CityGeneration.StartingPositions");
	const size_t reservedAt = offsetOf("CityGeneration.ReservedBlockLists");
	if (coastalAt == 0 || templatesAt == 0 || startsAt == 0 || reservedAt == 0 || reservedAt + 64 > exe.size())
	{
		outError = "Tabelas de cidades não encontradas no executável (acd_strings.txt).";
		return false;
	}

	this->cityGen.coastalCities.assign(exe.begin() + static_cast<std::ptrdiff_t>(coastalAt), exe.begin() + static_cast<std::ptrdiff_t>(coastalAt + 58));
	this->cityGen.templateNames = readStrings(exe, templatesAt, 7);
	for (int i = 0; i < 22; i++) this->cityGen.startingPositions.emplace_back(exe[startsAt + i * 2], exe[startsAt + i * 2 + 1]);
	size_t at = reservedAt;
	for (int i = 0; i < 8; i++)
	{
		std::vector<uint8_t> list;
		while (at < exe.size() && exe[at] != 0) list.push_back(exe[at++]);
		at++;
		this->cityGen.reservedBlocks.push_back(std::move(list));
	}

	// Ermo: listas de blocos (tamanho + ids) e velocidades de viagem por terreno.
	const char *wildKeys[5] = { "Wilderness.NormalBlocks", "Wilderness.VillageBlocks", "Wilderness.DungeonBlocks", "Wilderness.TavernBlocks", "Wilderness.TempleBlocks" };
	for (size_t i = 0; i < 5; i++)
	{
		const size_t wildAt = offsetOf(wildKeys[i]);
		if (wildAt == 0 || wildAt >= exe.size()) continue;
		const size_t n = exe[wildAt];
		this->cityGen.wildBlocks[i].assign(exe.begin() + static_cast<std::ptrdiff_t>(wildAt + 1), exe.begin() + static_cast<std::ptrdiff_t>(wildAt + 1 + n));
	}

	const size_t climateSpeedAt = offsetOf("Locations.ClimateSpeedTables"), weatherSpeedAt = offsetOf("Locations.WeatherSpeedTables");
	for (size_t t = 0; t < 7 && climateSpeedAt != 0; t++)
		for (size_t m = 0; m < 12; m++) this->climateSpeed[t][m] = exe[climateSpeedAt + t * 12 + m];
	for (size_t t = 0; t < 7 && weatherSpeedAt != 0; t++)
		for (size_t w = 0; w < 8; w++) this->weatherSpeed[t][w] = exe[weatherSpeedAt + t * 8 + w];

	// Locais: 9 províncias de 1228 bytes (nome, retângulo global e 48 locais de 25 bytes).
	const auto cityData = assets.read("CITYDATA.65");
	const auto terrainData = assets.read("TERRAIN.IMG");
	const auto terrainImage = terrainData ? loadImg("TERRAIN.IMG", *terrainData) : std::nullopt;
	if (!cityData || cityData->size() < 9 * 1228)
	{
		outError = "CITYDATA.65 não encontrado.";
		return false;
	}

	// Clima pelo terreno do mapa-múndi (com o deslocamento de 12 pixels do original e busca em
	// cruz quando o ponto cai no mar).
	auto terrainAt = [&terrainImage](int x, int y) -> int
	{
		if (!terrainImage || terrainImage->width != 320 || terrainImage->height != 200) return 254;
		const int count = 320 * 200;
		int i = x + y * 320 - 12;
		i = ((i % count) + count) % count;
		return terrainImage->pixels[static_cast<size_t>(i)];
	};
	auto climateAt = [&terrainAt](int x, int y)
	{
		constexpr int Sea = 248;
		int value = terrainAt(x, y);
		for (int dist = 1; value == Sea && dist < 200; dist++)
		{
			for (const int candidate : { terrainAt(x, y + dist), terrainAt(x, y - dist), terrainAt(x + dist, y), terrainAt(x - dist, y) })
			{
				if (candidate != Sea) { value = candidate; break; }
			}
		}

		if (value == 249 || value == 250) return CityLocation::Climate::Mountain;
		if (value == 252 || value == 253) return CityLocation::Climate::Desert;
		return CityLocation::Climate::Temperate;
	};

	if (terrainImage && terrainImage->width == 320 && terrainImage->height == 200) this->terrain = terrainImage->pixels;

	auto text = [](const uint8_t *p, size_t n) { size_t e = 0; while (e < n && p[e] != 0) e++; return std::string(reinterpret_cast<const char *>(p), e); };
	for (int province = 0; province < 9; province++)
	{
		const uint8_t *base = cityData->data() + static_cast<size_t>(province) * 1228;
		const int rectX = base[20] | (base[21] << 8), rectY = base[22] | (base[23] << 8);
		const int rectW = base[24] | (base[25] << 8), rectH = base[26] | (base[27] << 8);
		this->provinceOrigins[static_cast<size_t>(province)] = { rectX, rectY };
		for (int local = 0; local < 48; local++)
		{
			const uint8_t *loc = base + 28 + static_cast<size_t>(local) * 25;
			CityLocation city;
			city.name = text(loc, 20);
			city.province = province;
			city.localId = local;
			city.globalId = (province << 5) + local;
			city.localX = loc[20] | (loc[21] << 8);
			city.localY = loc[22] | (loc[23] << 8);
			city.globalX = ((city.localX * ((rectW * 100) / 320)) / 100) + rectX;
			city.globalY = ((city.localY * ((rectH * 100) / 200)) / 100) + rectY;
			city.type = local < 8 ? CityLocation::Type::CityState : local < 16 ? CityLocation::Type::Town : local < 32 ? CityLocation::Type::Village : CityLocation::Type::Dungeon;
			city.coastal = city.globalId < 256 && std::find(this->cityGen.coastalCities.begin(), this->cityGen.coastalCities.end(), static_cast<uint8_t>(city.globalId)) != this->cityGen.coastalCities.end();
			city.climate = climateAt(city.globalX, city.globalY);
			this->cities.push_back(std::move(city));
		}
	}

	return true;
}

arena::CreatureDefinition arena::ArenaData::humanEnemy(int classIndex, bool male, int level) const
{
	const ClassDefinition &cls = this->getClass(std::clamp(classIndex, 0, ClassCount - 1));
	level = std::max(1, level);
	// Tipo de sprite pela armadura da classe (como o OpenTESArena): placas, malha, couro; sem armadura,
	// mago, monge (12) ou bárbaro (15), senão "sem armadura". Placas só existem no masculino.
	int type = 3;
	if (cls.armor == ArmorAllowance::All) type = 0;
	else if (cls.armor == ArmorAllowance::LeatherAndChain) type = 1;
	else if (cls.armor == ArmorAllowance::LeatherOnly) type = 2;
	else if (cls.castsMagic) type = 4;
	else if (classIndex == 12) type = 5;
	else if (classIndex == 15) type = 6;
	const bool appearsMale = male || type == 0;
	auto name = [&](const std::string &pattern, char direction)
	{
		std::string out = pattern;
		const size_t xxx = out.find("XXX");
		if (xxx != std::string::npos) out.replace(xxx, 3, this->humanTypes[static_cast<size_t>(type)]);
		if (!out.empty()) out[0] = appearsMale ? '0' : '1';
		if (direction != 0) { const size_t at = out.find('@'); if (at != std::string::npos) out[at] = direction; }
		return out;
	};

	CreatureDefinition def;
	def.originalName = cls.originalName;
	def.animFilename = name(this->humanTemplates[0], 0);  // '@' = direção 1..5
	def.attackFilename = name(this->humanTemplates[1], '1');
	def.humanClass = classIndex;
	def.male = male;
	def.level = level;
	def.minHp = 15 + cls.healthDie * level / 2;
	def.maxHp = 15 + cls.healthDie * level;
	const int category = static_cast<int>(cls.category);
	def.minDamage = 1 + (cls.category == ClassCategory::Warrior ? 1 : 0);
	def.maxDamage = cls.category == ClassCategory::Warrior ? 6 + 2 * level : cls.category == ClassCategory::Thief ? 5 + level * 3 / 2 : 3 + level;
	def.baseExperience = level * level * this->humanExpMultipliers[static_cast<size_t>(std::clamp(category, 0, 2))];
	def.experienceMultiplier = 0;
	def.hasCorpse = true;
	def.spellElement = cls.castsMagic ? static_cast<int>(SpellElement::Fire) : -1; // magos lançam bolas de fogo
	return def;
}

void arena::ArenaData::nameRandomDungeons(const std::vector<std::string> &patterns)
{
	if (patterns.empty()) return;
	for (CityLocation &place : this->cities)
	{
		if (place.type != CityLocation::Type::Dungeon || !place.name.empty()) continue;
		ArenaRandom random(place.dungeonSeed());
		const std::string person = this->npcName(place.province, true, random);
		const std::string family = person.substr(person.rfind(' ') == std::string::npos ? 0 : person.rfind(' ') + 1);
		const std::string &pattern = patterns[random.next() % patterns.size()];
		const size_t at = pattern.find("%s");
		place.name = at == std::string::npos ? pattern : pattern.substr(0, at) + family + pattern.substr(at + 2);
	}
}

int arena::ArenaData::travelDays(const CityLocation &from, const CityLocation &to, int month) const
{
	// Soma o tempo de cada ponto da linha (Bresenham) pela velocidade do terreno naquele mês.
	int x0 = from.globalX, y0 = from.globalY;
	const int x1 = to.globalX, y1 = to.globalY;
	const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
	const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
	int err = dx + dy, total = 0;
	for (;;)
	{
		const int monthIndex = (std::max(1, month) - 1 + total / 3000) % 12;
		int terrainIndex = 0;
		if (!this->terrain.empty() && x0 >= 0 && y0 >= 0 && x0 < 320 && y0 < 200) terrainIndex = std::clamp(this->terrain[static_cast<size_t>(y0 * 320 + x0)] - 248, 0, 6);
		const int climate = std::max(1, static_cast<int>(this->climateSpeed[static_cast<size_t>(terrainIndex)][static_cast<size_t>(monthIndex)]));
		const int weather = this->weatherSpeed[static_cast<size_t>(terrainIndex)][0];
		const int speed = std::max(1, climate * (weather == 0 ? 100 : weather) / 100);
		total += 2000 / speed;
		if (x0 == x1 && y0 == y1) break;
		const int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}

	int days = std::clamp(total / 100, 1, 2000);
	if (days > 20) days += static_cast<int>((from.seed() ^ to.seed()) % 10u) - 5;
	return days;
}

const arena::CityLocation *arena::ArenaData::getCity(int province, int localId) const
{
	for (const CityLocation &city : this->cities)
		if (city.province == province && city.localId == localId) return &city;
	return nullptr;
}

int arena::ArenaData::classFromAnswers(int warrior, int thief, int mage) const
{
	for (size_t i = 0; i < this->generationChoices.size(); i++)
	{
		const auto &choice = this->generationChoices[i];
		if (choice[0] == warrior && choice[1] == thief && choice[2] == mage)
		{
			const size_t generationIndex = (i < 48) ? (i / 4) : (12 + (i - 48) / 3);
			const int classId = this->generationClassIds[generationIndex] & GenerationIdMask;
			return (classId < ClassCount) ? classId : -1;
		}
	}

	return -1;
}

int arena::ArenaData::provinceAt(int x, int y) const
{
	for (size_t i = 0; i < this->provinceMasks.size(); i++)
	{
		if (i == CenterProvinceID || i == 9)
		{
			continue;
		}

		if (this->provinceMasks[i].contains(x, y))
		{
			return static_cast<int>(i);
		}
	}

	return -1;
}

std::array<int, arena::AttributeCount> arena::ArenaData::getBaseAttributes(int raceIndex, bool male) const
{
	std::array<int, AttributeCount> out{};
	const auto &raw = this->raceAttributes[static_cast<size_t>(raceIndex * 2 + (male ? 0 : 1))];
	for (size_t i = 0; i < AttributeCount; i++)
	{
		out[i] = static_cast<int>(std::lround(raw[i] / (255.0 / 100.0)));
	}

	return out;
}
