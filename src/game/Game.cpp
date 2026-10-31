#include <algorithm>
#include <ctime>
#include <fstream>
#include <cstdio>

#include "../core/KeyValueFile.h"
#include "../render/Ui.h"
#include "../world/SpriteOverrides.h"
#include "Artifacts.h"
#include "Game.h"
#include "OriginalSave.h"
#include "Cutscene.h"
#include "Screens.h"

bool arena::Game::init(const std::filesystem::path &arenaDir, const std::filesystem::path &dataDir, std::string &outError)
{
	loadSpriteOverrides(dataDir / "mods");
	this->arenaDirectory = arenaDir;
	if (!this->assets.init(arenaDir, dataDir / "mods", outError))
	{
		return false;
	}

	const auto paletteData = this->assets.read("PAL.COL");
	const auto palette = paletteData ? loadColPalette(*paletteData) : std::nullopt;
	if (!palette)
	{
		outError = "PAL.COL não encontrado ou inválido.";
		return false;
	}

	this->defaultPalette = *palette;

	const std::pair<BitmapFont*, const char*> fontFiles[] =
	{
		{ &this->fonts.a, "FONT_A.DAT" }, { &this->fonts.b, "FONT_B.DAT" }, { &this->fonts.c, "FONT_C.DAT" },
		{ &this->fonts.d, "FONT_D.DAT" }, { &this->fonts.s, "FONT_S.DAT" }, { &this->fonts.arena, "ARENAFNT.DAT" },
		{ &this->fonts.character, "CHARFNT.DAT" }, { &this->fonts.teeny, "TEENYFNT.DAT" }, { &this->fonts.four, "FONT4.DAT" }
	};

	for (const auto &[font, name] : fontFiles)
	{
		const auto data = this->assets.read(name);
		if (!data || !font->load(*data))
		{
			outError = std::string("Fonte inválida: ") + name;
			return false;
		}
	}

	this->text.loadExeStringMap(dataDir / "exe" / "acd_strings.txt", this->assets.getExeImage());
	if (!this->text.loadLanguage(dataDir / "lang" / "pt_BR.txt"))
	{
		outError = "Arquivo de idioma pt_BR.txt não encontrado em " + (dataDir / "lang").string();
		return false;
	}

	this->dataDirectory = dataDir;
	if (!this->rules.load(this->assets, dataDir, outError))
	{
		return false;
	}

	// As masmorras aleatórias (locais 34..47) não têm nome no CITYDATA: o original inventa um quando
	// uma missão as revela. Aqui o nome sai da semente delas (sempre o mesmo) com os modelos do idioma.
	std::vector<std::string> dungeonPatterns;
	for (int i = 0; this->text.has("Jobs.DungeonName" + std::to_string(i)); i++) dungeonPatterns.push_back(this->text.get("Jobs.DungeonName" + std::to_string(i)));
	this->rules.nameRandomDungeons(dungeonPatterns);

	this->parchmentTiles = this->loadFrames("PARCH.CIF");

	// Abertura: o livro do INTRO.FLC (qualquer tecla pula) e depois a tela de título.
	this->current = std::make_unique<CutsceneScreen>(*this, std::vector<std::string>{ "INTRO.FLC" },
		[](Game &game) -> std::unique_ptr<Screen> { return std::make_unique<TitleScreen>(game); });
	this->fadeTimer = FadeDuration;
	return true;
}

void arena::Game::changeScreen(std::unique_ptr<Screen> next)
{
	if (this->pending)
	{
		return; // já existe uma transição em andamento
	}

	this->pending = std::move(next);
	this->fadingOut = true;
	this->fadeTimer = FadeDuration;
}

void arena::Game::handleInput(const InputEvent &event)
{
	if (this->current && !this->pending)
	{
		this->current->handleInput(event);
	}
}

void arena::Game::update(double dt)
{
	if (this->fadeTimer > 0.0)
	{
		this->fadeTimer -= dt;
		if (this->fadeTimer <= 0.0 && this->fadingOut)
		{
			this->current = std::move(this->pending);
			this->fadingOut = false;
			this->fadeTimer = FadeDuration;
		}
	}

	if (this->current)
	{
		this->current->update(dt);
	}
}

void arena::Game::render(Canvas &canvas, float &outBrightness) const
{
	if (this->current)
	{
		this->current->render(canvas);
	}

	const double t = std::max(0.0, this->fadeTimer) / FadeDuration;
	outBrightness = static_cast<float>(this->fadingOut ? t : 1.0 - t);
}

arena::IndexedImage arena::Game::loadImage(const std::string &name) const
{
	const auto data = this->assets.read(name);
	if (auto image = data ? loadImg(name, *data) : std::nullopt)
	{
		return std::move(*image);
	}

	IndexedImage blank;
	blank.width = Canvas::Width;
	blank.height = Canvas::Height;
	blank.pixels.assign(static_cast<size_t>(blank.width * blank.height), 0);
	return blank;
}

std::vector<arena::IndexedImage> arena::Game::loadFrames(const std::string &name) const
{
	const auto data = this->assets.read(name);
	if (!data) return {};
	// Animações .CFA (flecha do arco) ou quadros .CIF.
	if (name.size() > 4 && toUpperAscii(name.substr(name.size() - 4)) == ".CFA") return loadCfa(*data);
	return loadCif(name, *data);
}

std::optional<arena::Palette> arena::Game::loadPalette(const std::string &name) const
{
	const auto data = this->assets.read(name);
	if (!data)
	{
		return std::nullopt;
	}

	if (auto col = loadColPalette(*data))
	{
		return col;
	}

	// .MNU/.IMG com paleta embutida.
	const auto image = loadImg(name, *data);
	return image ? image->palette : std::nullopt;
}

const arena::IndexedImage &arena::Game::getParchment(int width, int height)
{
	const auto key = std::make_pair(width, height);
	auto it = this->parchmentCache.find(key);
	if (it == this->parchmentCache.end())
	{
		it = this->parchmentCache.emplace(key, ui::makeParchment(this->parchmentTiles, width, height)).first;
	}

	return it->second;
}

std::string arena::Game::localized(const std::string &key, const std::string &fallback) const
{
	if (!this->character.male && this->text.has(key + "_F"))
	{
		return this->text.get(key + "_F");
	}

	return this->text.getOr(key, fallback);
}

std::string arena::Game::className(int index) const
{
	return this->localized("Classes.Name" + std::to_string(index), this->rules.getClass(index).originalName);
}

std::string arena::Game::raceName(int index, bool plural) const
{
	return this->localized(std::string(plural ? "Races.Plural" : "Races.Singular") + std::to_string(index), "?");
}

std::string arena::Game::provinceName(int index) const
{
	return this->text.getOr("Provinces.Name" + std::to_string(index), "?");
}

std::string arena::Game::attributeName(int index, bool shortName) const
{
	return this->text.getOr(std::string(shortName ? "Attributes.Short" : "Attributes.Name") + std::to_string(index), "?");
}

const std::string &arena::Game::gendered(const std::string &key) const
{
	if (!this->character.male && this->text.has(key + "_F"))
	{
		return this->text.get(key + "_F");
	}

	return this->text.get(key);
}

void arena::Game::renderWorld(std::vector<uint32_t> &pixels, int width, int height) const
{
	if (!this->current)
	{
		return;
	}

	this->current->renderWorld(pixels, width, height);

	// Mesmo fade das telas 2D.
	const double t = std::max(0.0, this->fadeTimer) / FadeDuration;
	const float brightness = static_cast<float>(this->fadingOut ? t : 1.0 - t);
	if (this->gpu)
	{
		// Na GPU o fade é aplicado na composição final (cena + HUD).
		this->gpu->setWorldBrightness(std::clamp(brightness, 0.0f, 1.0f));
	}
	else if (brightness < 0.999f)
	{
		const uint32_t scale = static_cast<uint32_t>(std::clamp(brightness, 0.0f, 1.0f) * 256.0f);
		for (uint32_t &p : pixels)
		{
			const uint32_t r = ((p & 0xFF) * scale) >> 8;
			const uint32_t g = (((p >> 8) & 0xFF) * scale) >> 8;
			const uint32_t b = (((p >> 16) & 0xFF) * scale) >> 8;
			p = 0xFF000000u | (b << 16) | (g << 8) | r;
		}
	}
}

void arena::Game::enableAudio(float soundVolume, float musicVolume)
{
	this->mixer.setMasterVolume(soundVolume);
	this->music = std::make_unique<MusicPlayer>();
	this->music->setVolume(musicVolume);
}

void arena::Game::playSound(const std::string &name, float volume, float pan)
{
	const std::string key = toUpperAscii(name);
	auto it = this->soundCache.find(key);
	if (it == this->soundCache.end())
	{
		const auto data = this->assets.read(key);
		auto sound = data ? loadVoc(*data) : std::nullopt;
		it = this->soundCache.emplace(key, sound ? std::make_shared<const SoundBuffer>(std::move(*sound)) : nullptr).first;
	}

	if (it->second)
	{
		this->mixer.play(it->second, volume, pan);
	}
}

void arena::Game::playMusic(const std::string &name)
{
	const std::string key = toUpperAscii(name);
	if (!this->music || key == this->currentMusic)
	{
		return;
	}

	this->currentMusic = key;
	const auto data = this->assets.read(key);
	if (data)
	{
		this->music->play(loadXmi(*data), true);
	}
}

void arena::Game::stopMusic()
{
	if (this->music)
	{
		this->music->stop();
		this->currentMusic.clear();
	}
}

bool arena::Game::hasSavedCharacter() const
{
	std::error_code ec;
	return std::filesystem::exists(this->getSavePath(), ec);
}

bool arena::Game::loadSavedCharacter(const std::filesystem::path &path)
{
	std::vector<std::pair<std::string, std::string>> world;
	CharacterDraft loaded;
	int questVersion = 1;
	const bool ok = parseKeyValueFile(path, [&loaded, &world, &questVersion](const std::string &key, const std::string &value)
	{
		if (key.rfind("World.", 0) == 0 || key.rfind("Creatures.", 0) == 0 || key.rfind("Loot.", 0) == 0)
		{
			world.emplace_back(key, value);
			return;
		}

		const int number = std::atoi(value.c_str());
		if (key == "Character.Name") loaded.name = value;
		else if (key == "Character.Male") loaded.male = number != 0;
		else if (key == "Character.Class") loaded.classIndex = number;
		else if (key == "Character.Race") loaded.raceIndex = number;
		else if (key == "Character.Portrait") loaded.portraitIndex = number;
		else if (key == "Character.MaxHealth") loaded.maxHealth = number;
		else if (key == "Character.MaxStamina") loaded.maxStamina = number;
		else if (key == "Character.MaxSpellPoints") loaded.maxSpellPoints = number;
		else if (key == "Character.Gold") loaded.gold = number;
		else if (key == "Character.Disease") loaded.disease = number;
		else if (key == "Character.DiseaseEnd") loaded.diseaseEnd = std::atof(value.c_str());
		else if (key.rfind("Crimes.City", 0) == 0) loaded.wanted[std::atoi(key.c_str() + 11)] = std::atof(value.c_str());
		else if (key == "Character.Experience") loaded.experience = number;
		else if (key == "Character.Level") loaded.level = std::max(1, number);
		else if (key == "Character.LevelPoints") loaded.levelPoints = std::max(0, number);
		else if (key == "Character.Time") loaded.gameMinutes = std::max(0.0, std::atof(value.c_str()));
		else if (key == "Character.Health") loaded.health = number;
		else if (key == "Character.SpellPoints") loaded.spellPoints = number;
		else if (key == "Spellbook.Ready") loaded.readySpell = number;
		else if (key == "Quest.StaffPieces") loaded.staffPieces = std::clamp(number, 0, 8);
		else if (key == "Quest.Step") loaded.questStep = std::clamp(number, 0, 4);
		else if (key == "Quest.Giver") loaded.questGiver = number;
		else if (key == "Quest.Version") questVersion = number;
		else if (key == "SideJob.Kind") loaded.job.kind = static_cast<SideJob::Kind>(std::clamp(number, 0, static_cast<int>(SideJob::Kind::TavernFind)));
		else if (key == "SideJob.Giver") loaded.job.giver = number;
		else if (key == "SideJob.Target") loaded.job.target = number;
		else if (key == "SideJob.Reward") loaded.job.reward = number;
		else if (key == "SideJob.Deadline") loaded.job.deadline = std::atof(value.c_str());
		else if (key == "SideJob.Step") loaded.job.step = number;
		else if (key == "SideJob.Subject") loaded.job.subject = number;
		else if (key == "SideJob.Name") loaded.job.name = value;
		else if (key == "Artifacts.Found") loaded.artifactsFound = number;
		else if (key == "Artifacts.Hunt") loaded.hunt.artifact = number;
		else if (key == "Artifacts.CofferDay") loaded.cofferDay = number;
		else if (key.rfind("CustomSpells.Spell", 0) == 0)
		{
			// índice|nome|alvo|elemento|efeito|sub|p0,p1,p2,p3,p4,p5|ouro
			std::vector<std::string> parts;
			for (size_t at = 0;;)
			{
				const size_t bar = value.find('|', at);
				parts.push_back(value.substr(at, bar == std::string::npos ? std::string::npos : bar - at));
				if (bar == std::string::npos) break;
				at = bar + 1;
			}
			if (parts.size() == 8)
			{
				Spell spell;
				spell.index = std::atoi(parts[0].c_str());
				spell.originalName = parts[1];
				spell.target = static_cast<SpellTarget>(std::atoi(parts[2].c_str()));
				spell.element = static_cast<SpellElement>(std::atoi(parts[3].c_str()));
				spell.effects[0].type = std::atoi(parts[4].c_str());
				spell.effects[0].sub = std::atoi(parts[5].c_str());
				std::sscanf(parts[6].c_str(), "%d,%d,%d,%d,%d,%d", &spell.effects[0].params[0], &spell.effects[0].params[1], &spell.effects[0].params[2],
					&spell.effects[0].params[3], &spell.effects[0].params[4], &spell.effects[0].params[5]);
				spell.goldCost = std::atoi(parts[7].c_str());
				if (spell.index >= CustomSpellBase && spell.effects[0].valid()) loaded.customSpells.push_back(spell);
			}
		}
		else if (key.rfind("Journal.Entry", 0) == 0)
		{
			// minutos|chave do texto completo|texto
			const size_t a = value.find('|'), b = a == std::string::npos ? a : value.find('|', a + 1);
			if (b != std::string::npos) loaded.journal.push_back({ std::atof(value.c_str()), value.substr(a + 1, b - a - 1), value.substr(b + 1) });
		}
		else if (key == "Spellbook.Known")
		{
			loaded.spellbookKnown = true;
			for (size_t at = 0; at < value.size();)
			{
				const size_t comma = value.find(',', at);
				const std::string part = value.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
				if (!part.empty()) loaded.spells.push_back(std::atoi(part.c_str()));
				if (comma == std::string::npos) break;
				at = comma + 1;
			}
		}
		else if (key.rfind("Inventory.Item", 0) == 0)
		{
			// Item = tipo, id, classe de armadura, material, equipado[, artefato]
			int kind = 0, type = 0, armorClass = 0, material = 0, equipped = 0, artifactId = -1;
			if (std::sscanf(value.c_str(), "%d,%d,%d,%d,%d,%d", &kind, &type, &armorClass, &material, &equipped, &artifactId) >= 5)
			{
				Item item;
				item.kind = static_cast<ItemKind>(std::clamp(kind, 0, 3));
				item.artifact = artifactId;
				item.type = type;
				item.armorClass = static_cast<ArmorClass>(std::clamp(armorClass, 0, 2));
				item.material = std::clamp(material, 0, MaterialCount - 1);
				item.equipped = equipped != 0;
				loaded.inventory.push_back(item);
			}
		}
		else if (key.rfind("Character.Attribute", 0) == 0)
		{
			const int index = std::atoi(key.c_str() + 19);
			if (index >= 0 && index < AttributeCount) loaded.attributes[static_cast<size_t>(index)] = number;
		}
	});

	if (!ok || loaded.name.empty() || loaded.classIndex < 0 || loaded.classIndex >= ClassCount || loaded.raceIndex < 0 || loaded.raceIndex >= RaceCount)
	{
		return false;
	}

	// Missão da versão 1 (sem a entrega ao governante): passo 3 era "masmorra do pedaço conhecida".
	if (questVersion < 2 && loaded.staffPieces < 8 && loaded.questStep == 3) loaded.questStep = 4;

	// Fichas antigas (antes do inventário) recebem o equipamento inicial da classe.
	if (loaded.inventory.empty())
	{
		loaded.inventory = startingKit(loaded.classIndex, static_cast<int>(this->rules.getClass(loaded.classIndex).armor));
	}

	// Fichas sem livro de feitiços recebem o livro inicial da classe.
	if (!loaded.spellbookKnown)
	{
		loaded.spells = starterSpells(loaded.classIndex, this->rules.getClass(loaded.classIndex).castsMagic);
		loaded.readySpell = loaded.spells.empty() ? -1 : loaded.spells.front();
	}

	this->character = loaded;
	this->importedMap.clear();
	artifact::syncSpells(this->character);
	setCustomSpells(this->character.customSpells);
	this->pendingWorld = std::move(world);
	return true;
}

arena::Game::SaveSummary arena::Game::readSaveSummary(const std::filesystem::path &path) const
{
	SaveSummary summary;
	summary.exists = parseKeyValueFile(path, [&summary](const std::string &key, const std::string &value)
	{
		if (key == "Slot.Name" || (key == "Character.Name" && summary.name.empty())) summary.name = value;
		else if (key == "Slot.Date") summary.date = value;
		else if (key == "Slot.Location") summary.location = value;
		else if (key == "Character.Level") summary.level = std::max(1, std::atoi(value.c_str()));
	});
	return summary;
}

bool arena::Game::saveCharacter(const std::filesystem::path &path, const std::string &world, const std::string &location) const
{
	const CharacterDraft &c = this->character;
	std::error_code ec;
	std::filesystem::create_directories(this->getSavePath().parent_path(), ec);
	std::ofstream file(path, std::ios::binary);
	if (!file)
	{
		return false;
	}

	// Resumo para as listas de salvar/carregar.
	const std::time_t now = std::time(nullptr);
	char date[32] = {};
	std::tm local{};
#ifdef _WIN32
	localtime_s(&local, &now);
#else
	localtime_r(&now, &local);
#endif
	std::strftime(date, sizeof(date), "%d/%m/%Y %H:%M", &local);
	file << "# Jogo salvo do Arena Remake\n[Slot]\nName=" << c.name << "\nDate=" << date << "\nLocation=" << location << "\n\n[Character]\n"
		<< "Name=" << c.name << "\nMale=" << (c.male ? 1 : 0) << "\nClass=" << c.classIndex << "\nRace=" << c.raceIndex
		<< "\nPortrait=" << c.portraitIndex << "\nMaxHealth=" << c.maxHealth << "\nMaxStamina=" << c.maxStamina
		<< "\nMaxSpellPoints=" << c.maxSpellPoints << "\nGold=" << c.gold << "\nLevel=" << c.level << "\nLevelPoints=" << c.levelPoints << "\nTime=" << static_cast<long long>(c.gameMinutes) << "\nExperience=" << c.experience
		<< "\nHealth=" << c.health << "\nSpellPoints=" << c.spellPoints << "\nDisease=" << c.disease << "\nDiseaseEnd=" << static_cast<long long>(c.diseaseEnd) << "\n";
	for (int i = 0; i < AttributeCount; i++)
	{
		file << "Attribute" << i << "=" << c.attributes[static_cast<size_t>(i)] << "\n";
	}

	file << "\n[Inventory]\n# tipo (0 arma, 1 armadura, 2 poção, 3 artefato), id, classe (0 placas, 1 malha, 2 couro), material, equipado, artefato\n";
	for (size_t i = 0; i < c.inventory.size(); i++)
	{
		const Item &item = c.inventory[i];
		file << "Item" << i << "=" << static_cast<int>(item.kind) << "," << item.type << "," << static_cast<int>(item.armorClass)
			<< "," << item.material << "," << (item.equipped ? 1 : 0) << "," << item.artifact << "\n";
	}

	file << "\n[Spellbook]\n# feitiços conhecidos (índices do SPELLSG.65) e o preparado para a tecla Q\nKnown=";
	for (size_t i = 0; i < c.spells.size(); i++) file << (i ? "," : "") << c.spells[i];
	file << "\nReady=" << c.readySpell << "\n";
	file << "\n[Quest]\n# missão principal: pedaços do Cajado do Caos e passo da busca do próximo\nStaffPieces=" << c.staffPieces << "\nStep=" << c.questStep << "\nGiver=" << c.questGiver << "\nVersion=2\n";
	file << "\n[CustomSpells]\n# feitiços criados: índice|nome|alvo|elemento|efeito|sub|parâmetros|ouro\n";
	for (size_t i = 0; i < c.customSpells.size(); i++)
	{
		const Spell &s = c.customSpells[i];
		const auto &p = s.effects[0].params;
		file << "Spell" << i << "=" << s.index << "|" << s.originalName << "|" << static_cast<int>(s.target) << "|" << static_cast<int>(s.element) << "|" << s.effects[0].type << "|" << s.effects[0].sub
			<< "|" << p[0] << "," << p[1] << "," << p[2] << "," << p[3] << "," << p[4] << "," << p[5] << "|" << s.goldCost << "\n";
	}
	const SideJob &job = c.job;
	file << "\n[SideJob]\n# trabalho pago: tipo (0 nenhum, 1 entrega, 2 busca, 3 escolta, 4 resgate, 5 caçada, 6 entrega da taverna, 7 achado), lugares (província × 100 + local)\n"
		<< "Kind=" << static_cast<int>(job.kind) << "\nGiver=" << job.giver << "\nTarget=" << job.target << "\nReward=" << job.reward
		<< "\nDeadline=" << static_cast<long long>(job.deadline) << "\nStep=" << job.step << "\nSubject=" << job.subject << "\nName=" << job.name << "\n";
	file << "\n[Artifacts]\n# achados (bit por artefato), artefato procurado e último dia do cofre\nFound=" << c.artifactsFound << "\nHunt=" << c.hunt.artifact << "\nCofferDay=" << c.cofferDay << "\n";
	file << "\n[Crimes]\n# cidade (id global) = minutos do jogo até a guarda esquecer o crime\n";
	for (const auto &[city, until] : c.wanted) if (until > c.gameMinutes) file << "City" << city << "=" << static_cast<long long>(until) << "\n";
	file << "\n[Journal]\n# diário: minutos de jogo|texto completo para reler|resumo\n";
	for (size_t i = 0; i < c.journal.size(); i++)
		file << "Entry" << i << "=" << static_cast<long long>(c.journal[i].minutes) << "|" << c.journal[i].detail << "|" << c.journal[i].text << "\n";
	if (!world.empty()) file << "\n" << world;

	return static_cast<bool>(file);
}

const arena::Palette &arena::Game::getPalette() const
{
	return this->current ? this->current->getPalette() : this->defaultPalette;
}

void arena::Game::saveSetting(const std::string &section, const std::string &key, const std::string &value) const
{
	const std::filesystem::path path = this->dataDirectory.parent_path() / "settings.ini";
	std::vector<std::string> lines;
	{
		std::ifstream in(path);
		for (std::string line; std::getline(in, line);) lines.push_back(line);
	}

	// Troca a linha da chave dentro da seção; se não houver, acrescenta no fim da seção (ou do arquivo).
	const std::string header = "[" + section + "]";
	size_t sectionAt = lines.size(), insertAt = lines.size();
	for (size_t i = 0; i < lines.size(); i++)
	{
		std::string trimmed = lines[i];
		if (!trimmed.empty() && trimmed.back() == '\r') trimmed.pop_back();
		if (trimmed == header) { sectionAt = i; insertAt = i + 1; continue; }
		if (sectionAt == lines.size()) continue;
		if (!trimmed.empty() && trimmed[0] == '[') break;
		if (trimmed.rfind(key + "=", 0) == 0) { lines[i] = key + "=" + value; insertAt = lines.size() + 1; break; }
		if (!trimmed.empty()) insertAt = i + 1;
	}

	if (insertAt <= lines.size())
	{
		if (sectionAt == lines.size()) { lines.push_back(""); lines.push_back(header); insertAt = lines.size(); }
		lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAt), key + "=" + value);
	}

	std::ofstream out(path, std::ios::trunc);
	for (const std::string &line : lines) out << line << "\n";
}

bool arena::Game::importOriginalSave(int slot, std::string &outError)
{
	std::string map;
	if (!original::import(*this, this->arenaDirectory, slot, map, outError)) return false;
	this->pendingWorld.clear();
	this->importedMap = map;
	setCustomSpells({});
	return true;
}
