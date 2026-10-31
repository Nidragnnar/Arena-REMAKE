// Missões secundárias: trabalhos do governante e do taverneiro, e a busca dos artefatos.

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "../render/Ui.h"
#include "Artifacts.h"
#include "Game.h"
#include "GameClock.h"
#include "MainQuest.h"
#include "SideQuests.h"

namespace
{
	using namespace arena;

	constexpr int SubjectItems = 6;      // Jobs.Item0..5 (carta selada, pacote...)
	constexpr int Relations = 4;         // Jobs.Relation0..3 (filha, filho, sobrinha, sobrinho)
	constexpr int LostItems = 4;         // Jobs.Lost0..3 (para o taverneiro)

	// Direção de um lugar a outro no mapa de Tamriel (y cresce para o sul).
	std::string direction(const Localization &text, const CityLocation &from, const CityLocation &to)
	{
		const double dx = to.globalX - from.globalX, dy = to.globalY - from.globalY;
		if (std::fabs(dx) + std::fabs(dy) < 1.0) return text.get("Dialogue.Dir0");
		const double degrees = std::atan2(dx, -dy) * 180.0 / 3.14159265358979;
		const int sector = static_cast<int>(std::lround((degrees < 0 ? degrees + 360.0 : degrees) / 45.0)) % 8;
		return text.get("Dialogue.Dir" + std::to_string(sector));
	}

	std::string dateText(const Localization &text, double minutes)
	{
		return ui::format(text.get("Calendar.Date"), { std::to_string(clock::dayOfMonth(minutes)), text.get("Calendar.Month" + std::to_string(clock::month(minutes))) });
	}

	int daysBetween(const Game &game, const CityLocation &a, const CityLocation &b)
	{
		return std::max(1, game.getData().travelDays(a, b, clock::month(game.getCharacter().gameMinutes)));
	}

	std::string kindKey(SideJob::Kind kind)
	{
		switch (kind)
		{
		case SideJob::Kind::Deliver: return "Deliver";
		case SideJob::Kind::Fetch: return "Fetch";
		case SideJob::Kind::Escort: return "Escort";
		case SideJob::Kind::Rescue: return "Rescue";
		case SideJob::Kind::Monster: return "Monster";
		case SideJob::Kind::TavernDeliver: return "TavernDeliver";
		case SideJob::Kind::TavernFind: return "TavernFind";
		default: return "None";
		}
	}

	// Campos dos textos: alvo, destino, direção, prazo, recompensa, quem pediu.
	std::vector<std::string> fields(const Game &game, const SideJob &job)
	{
		const Localization &text = game.getText();
		const CityLocation *giver = jobs::place(game.getData(), job.giver), *target = jobs::place(game.getData(), job.target);
		return { jobs::targetName(game, job), target ? target->name : std::string(), giver && target ? direction(text, *giver, *target) : std::string(),
			dateText(text, job.deadline), std::to_string(job.reward), giver ? giver->name : std::string() };
	}

	std::string formatJob(const Game &game, const std::string &key, const SideJob &job)
	{
		// Os textos usam a ordem: %1 alvo, %2 destino, %3 direção, %4 prazo, %5 ouro, %6 quem pediu.
		std::string out = game.getText().get(key);
		const std::vector<std::string> values = fields(game, job);
		for (size_t i = 0; i < values.size(); i++)
		{
			const std::string tag = "%" + std::to_string(i + 1);
			for (size_t at = out.find(tag); at != std::string::npos; at = out.find(tag, at + values[i].size())) out.replace(at, tag.size(), values[i]);
		}
		return out;
	}
}

int arena::jobs::code(const CityLocation &place)
{
	return place.province * 100 + place.localId;
}

const arena::CityLocation *arena::jobs::place(const ArenaData &data, int code)
{
	return code < 0 ? nullptr : data.getCity(code / 100, code % 100);
}

std::string arena::jobs::targetName(const Game &game, const SideJob &job)
{
	const Localization &text = game.getText();
	switch (job.kind)
	{
	case SideJob::Kind::Escort:
	case SideJob::Kind::Rescue:
		return ui::format(text.get("Jobs.Relative"), { text.get("Jobs.Relation" + std::to_string(job.subject % Relations)), job.name });
	case SideJob::Kind::Monster:
		return text.getOr("Creatures.Ref" + std::to_string(job.subject), "?");
	case SideJob::Kind::TavernFind:
		return text.get("Jobs.Lost" + std::to_string(job.subject % LostItems));
	default:
		return text.get("Jobs.Item" + std::to_string(job.subject % SubjectItems));
	}
}

std::optional<arena::SideJob> arena::jobs::offer(const Game &game, const CityLocation &here, bool ruler, uint32_t seed)
{
	const ArenaData &data = game.getData();
	const CharacterDraft &c = game.getCharacter();
	std::mt19937 rng(seed);
	if (rng() % 100u >= (ruler ? 70u : 60u)) return std::nullopt; // nem todo dia há trabalho

	// Destinos na mesma província: outras cidades e as masmorras com nome (fora as da missão principal).
	std::vector<const CityLocation *> cities, dungeons;
	for (const CityLocation &p : data.getCities())
	{
		if (p.province != here.province || p.name.empty() || &p == &here) continue;
		if (p.type == CityLocation::Type::Dungeon) { if (!p.mainQuest()) dungeons.push_back(&p); }
		else cities.push_back(&p);
	}
	if (cities.empty()) return std::nullopt;

	SideJob job;
	static const SideJob::Kind RulerKinds[] = { SideJob::Kind::Deliver, SideJob::Kind::Fetch, SideJob::Kind::Escort, SideJob::Kind::Rescue, SideJob::Kind::Monster };
	static const SideJob::Kind TavernKinds[] = { SideJob::Kind::TavernDeliver, SideJob::Kind::TavernFind };
	job.kind = ruler ? RulerKinds[rng() % 5u] : TavernKinds[rng() % 2u];
	if (job.inDungeon() && dungeons.empty()) job.kind = ruler ? SideJob::Kind::Deliver : SideJob::Kind::TavernDeliver;
	job.giver = code(here);

	const double now = c.gameMinutes;
	const int level = std::max(1, c.level);
	if (job.inDungeon())
	{
		const CityLocation &dungeon = *dungeons[rng() % dungeons.size()];
		const int days = daysBetween(game, here, dungeon);
		job.target = code(dungeon);
		job.deadline = std::floor(now / clock::MinutesPerDay + 2 * days + 6) * clock::MinutesPerDay + 18 * 60;
		job.reward = (ruler ? 150 : 60) + (ruler ? 60 : 25) * level + 20 * days;
	}
	else
	{
		const CityLocation &city = *cities[rng() % cities.size()];
		const int days = daysBetween(game, here, city);
		const bool roundTrip = job.kind == SideJob::Kind::Fetch;
		job.target = code(city);
		job.deadline = std::floor(now / clock::MinutesPerDay + (roundTrip ? 2 * days + 4 : days + 3)) * clock::MinutesPerDay + 18 * 60;
		job.reward = (ruler ? 60 : 25) + (ruler ? 25 : 10) * level + (ruler ? 30 : 15) * days * (roundTrip ? 2 : 1);
	}

	switch (job.kind)
	{
	case SideJob::Kind::Escort:
	case SideJob::Kind::Rescue:
	{
		job.subject = static_cast<int>(rng() % Relations);
		ArenaRandom names(seed ^ 0x9E3779B9u);
		job.name = data.npcName(here.province, job.subject % 2 == 1, names);
		break;
	}
	case SideJob::Kind::Monster:
	{
		// Uma criatura forte para o nível (até 3 acima), entre as que andam pelas masmorras.
		std::vector<int> kinds;
		for (int k = 2; k <= 22; k++)
			if (const CreatureDefinition *def = data.getCreature(k); def && def->level >= level - 1 && def->level <= level + 3) kinds.push_back(k);
		if (kinds.empty()) kinds.push_back(std::clamp(level + 2, 2, 22));
		job.subject = kinds[rng() % kinds.size()];
		break;
	}
	case SideJob::Kind::TavernFind: job.subject = static_cast<int>(rng() % LostItems); break;
	default: job.subject = static_cast<int>(rng() % SubjectItems); break;
	}

	return job;
}

std::string arena::jobs::offerText(const Game &game, const SideJob &job)
{
	const Localization &text = game.getText();
	const std::string greeting = ui::format(text.get(job.fromRuler() ? "Jobs.RulerGreeting" : "Jobs.TavernGreeting"), { game.getCharacter().name });
	return greeting + "\n\n" + formatJob(game, "Jobs.Offer" + kindKey(job.kind), job);
}

void arena::jobs::accept(Game &game, const SideJob &job)
{
	CharacterDraft &c = game.getCharacter();
	c.job = job;
	quest::note(c, formatJob(game, "Jobs.Journal" + kindKey(job.kind), job), "");
}

arena::jobs::Outcome arena::jobs::arrive(Game &game, const CityLocation &here, bool ruler)
{
	CharacterDraft &c = game.getCharacter();
	SideJob &job = c.job;
	const Localization &text = game.getText();
	if (!job.active() || job.fromRuler() != ruler) return {};

	const int at = code(here);
	const bool late = c.gameMinutes > job.deadline;
	const std::string key = kindKey(job.kind);
	const std::string title = text.get(ruler ? "Jobs.RulerTitle" : "Jobs.TavernTitle");
	auto finish = [&](bool paid)
	{
		Outcome out{ title, formatJob(game, (paid ? "Jobs.Done" : "Jobs.Failed") + key, job) };
		quest::note(c, formatJob(game, (paid ? "Jobs.JournalDone" : "Jobs.JournalFailed"), job), "");
		if (paid)
		{
			c.gold += job.reward;
			game.playSound("FANFARE2.VOC");
		}
		job = SideJob{};
		return out;
	};

	switch (job.kind)
	{
	case SideJob::Kind::Deliver:
	case SideJob::Kind::Escort:
	case SideJob::Kind::TavernDeliver:
		if (at == job.target) return finish(!late);
		if (at == job.giver && late) return finish(false);
		break;
	case SideJob::Kind::Fetch:
		if (at == job.target && job.step == 0 && !late)
		{
			job.step = 1;
			quest::note(c, formatJob(game, "Jobs.JournalFetched", job), "");
			return { title, formatJob(game, "Jobs.HandedFetch", job) };
		}
		if (at == job.giver && (job.step == 1 || late)) return finish(!late);
		break;
	default: // resgate, caçada e achado: o pagamento é na volta
		if (at == job.giver && (job.step == 1 || late)) return finish(job.step == 1 && !late);
		break;
	}
	return {};
}

std::string arena::jobs::targetDone(Game &game)
{
	CharacterDraft &c = game.getCharacter();
	c.job.step = 1;
	const std::string body = formatJob(game, "Jobs.Found" + kindKey(c.job.kind), c.job);
	quest::note(c, formatJob(game, "Jobs.JournalFound", c.job), "");
	return body;
}

int arena::jobs::artifactRumor(const CharacterDraft &character, uint32_t seed)
{
	std::vector<int> left;
	for (int a = 0; a < artifact::Count; a++) if (!artifact::owned(character, a) && a != artifact::MaceOfMolagBal) left.push_back(a); // a Maça não é do Arena
	if (left.empty()) return -1;
	return left[seed % left.size()];
}

int arena::jobs::artifactInfoPrice(const CharacterDraft &character)
{
	return 200 + 100 * std::max(1, character.level);
}

std::string arena::jobs::artifactOfferText(const Game &game, int id)
{
	const Localization &text = game.getText();
	return ui::format(text.get("Artifacts.Offer"), { text.get("Artifacts.Name" + std::to_string(id)), text.get("Artifacts.Lore" + std::to_string(id)),
		std::to_string(artifactInfoPrice(game.getCharacter())) });
}

std::string arena::jobs::artifactRevealText(const Game &game, int id)
{
	const Localization &text = game.getText();
	const CityLocation *dungeon = artifact::dungeon(game.getData(), id);
	return ui::format(text.get("Artifacts.Reveal"), { text.get("Provinces.Name" + std::to_string(dungeon ? dungeon->province : 0)), dungeon ? dungeon->name : std::string(),
		text.get("Artifacts.Name" + std::to_string(id)) });
}

std::string arena::jobs::objective(const Game &game)
{
	const CharacterDraft &c = game.getCharacter();
	const Localization &text = game.getText();
	std::string out;
	if (c.job.active())
	{
		const bool late = c.gameMinutes > c.job.deadline;
		const std::string key = c.job.kind == SideJob::Kind::Fetch && c.job.step == 1 ? "Jobs.ObjFetchBack"
			: c.job.inDungeon() && c.job.step == 1 ? "Jobs.ObjReturn" : "Jobs.Obj" + kindKey(c.job.kind);
		out = formatJob(game, late ? "Jobs.ObjLate" : key, c.job);
	}
	if (c.hunt.active())
	{
		const CityLocation *dungeon = artifact::dungeon(game.getData(), c.hunt.artifact);
		if (!out.empty()) out += "\n";
		out += ui::format(text.get("Artifacts.Objective"), { text.get("Artifacts.Name" + std::to_string(c.hunt.artifact)), dungeon ? dungeon->name : std::string(),
			text.get("Provinces.Name" + std::to_string(dungeon ? dungeon->province : 0)) });
	}
	return out;
}
