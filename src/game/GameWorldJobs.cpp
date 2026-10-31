// Missões secundárias na exploração: propostas do governante e do taverneiro, o estranho que
// vende o paradeiro de artefatos e os alvos colocados no fundo das masmorras.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <queue>

#include "../render/Ui.h"
#include "Artifacts.h"
#include "GameClock.h"
#include "GameWorldScreen.h"
#include "MainQuest.h"

void arena::GameWorldScreen::offerJob(bool ruler)
{
	const CityLocation *city = this->currentCity();
	if (!city) return;
	CharacterDraft &c = this->game.getCharacter();
	const Localization &text = this->game.getText();
	if (c.job.active())
	{
		if (!ruler) this->showMessage(text.get("Jobs.Busy"), 4.0);
		else if (c.job.giver == jobs::code(*city)) this->showMessage(text.get("Jobs.RulerWaiting"), 4.0);
		return;
	}

	// Uma proposta por lugar e por dia (a mesma em todas as tavernas da cidade).
	const int day = clock::dayIndex(c.gameMinutes);
	const std::pair<int, int> key{ jobs::code(*city) * 2 + (ruler ? 1 : 0), day };
	const uint32_t seed = city->seed() ^ (static_cast<uint32_t>(day) * 2654435761u) ^ (ruler ? 0x5EEDu : 0x7A7u);
	const auto job = this->game.heardOffers.contains(key) ? std::optional<SideJob>{} : jobs::offer(this->game, *city, ruler, seed);
	this->game.heardOffers.insert(key);
	if (!job)
	{
		this->showMessage(text.get(ruler ? "Jobs.RulerNoWork" : "Jobs.NoneToday"), 4.0);
		return;
	}

	this->openVision(text.get(ruler ? "Jobs.RulerTitle" : "Jobs.TavernTitle"), jobs::offerText(this->game, *job));
	this->offeredJob = job;
}

void arena::GameWorldScreen::offerArtifact()
{
	const CityLocation *city = this->currentCity();
	if (!city) return;
	CharacterDraft &c = this->game.getCharacter();
	const Localization &text = this->game.getText();
	if (c.hunt.active())
	{
		this->openVision(text.get("Artifacts.Title"), jobs::artifactRevealText(this->game, c.hunt.artifact));
		return;
	}

	// Nem sempre há quem saiba de algo: um estranho por taverna e por dia, como no original.
	const int day = clock::dayIndex(c.gameMinutes);
	const std::pair<int, int> key{ -1 - jobs::code(*city), day };
	const uint32_t seed = (city->seed() * 31u + static_cast<uint32_t>(day) * 7919u) * 2654435761u;
	const bool heard = this->game.heardOffers.contains(key);
	this->game.heardOffers.insert(key);
	if (heard || (seed >> 16) % 100u >= 60u)
	{
		this->showMessage(text.get("Artifacts.Nobody"), 4.0);
		return;
	}

	const int id = jobs::artifactRumor(c, seed >> 8);
	if (id < 0)
	{
		this->showMessage(text.get("Artifacts.AllFound"), 4.0);
		return;
	}

	this->openVision(text.get("Artifacts.Title"), jobs::artifactOfferText(this->game, id));
	this->offeredArtifact = id;
}

void arena::GameWorldScreen::answerOffer(bool accepted)
{
	const Localization &text = this->game.getText();
	CharacterDraft &c = this->game.getCharacter();
	this->visionText.clear();
	if (this->offeredJob)
	{
		const SideJob job = *this->offeredJob;
		this->offeredJob.reset();
		if (!accepted)
		{
			this->showMessage(text.get("Jobs.Declined"), 3.0);
			return;
		}
		jobs::accept(this->game, job);
		this->game.playSound("CLICKS.VOC", 0.6f);
		this->showMessage(text.get("Jobs.Accepted"), 5.0);
		this->persist();
		return;
	}

	if (this->offeredArtifact < 0) return;
	const int id = this->offeredArtifact;
	this->offeredArtifact = -1;
	if (!accepted)
	{
		this->showMessage(text.get("Artifacts.Declined"), 3.0);
		return;
	}

	const int price = jobs::artifactInfoPrice(c);
	if (c.gold < price)
	{
		this->showMessage(ui::format(text.get("Shop.NoGold"), { std::to_string(price) }), 4.0);
		return;
	}

	c.gold -= price;
	c.hunt.artifact = id;
	const CityLocation *dungeon = artifact::dungeon(this->game.getData(), id);
	quest::note(c, ui::format(text.get("Artifacts.Journal"), { text.get("Artifacts.Name" + std::to_string(id)), dungeon ? dungeon->name : std::string(),
		text.get("Provinces.Name" + std::to_string(dungeon ? dungeon->province : 0)) }));
	this->game.playSound("CLICKS.VOC", 0.6f);
	this->openVision(text.get("Artifacts.Title"), jobs::artifactRevealText(this->game, id));
	this->persist();
}

void arena::GameWorldScreen::placeDungeonTargets()
{
	// Só nas masmorras geradas, no último andar: o alvo do trabalho e/ou o artefato procurado.
	unsigned long long seed = 0;
	int width = 0, depth = 0, artifactFlag = 0, floor = 0;
	if (std::sscanf(this->mifName.c_str(), "IN:DNG:%llu:%d:%d:%d:%d", &seed, &width, &depth, &artifactFlag, &floor) != 5) return;
	if (this->dungeonLevel != this->dungeonFloors - 1) return;

	const CharacterDraft &c = this->game.getCharacter();
	const ArenaData &data = this->game.getData();
	auto isHere = [seed](const CityLocation *p) { return p && static_cast<uint32_t>(seed) == p->dungeonSeed(); };
	const bool wantJob = c.job.active() && c.job.inDungeon() && c.job.step == 0 && isHere(jobs::place(data, c.job.target));
	const bool wantArtifact = c.hunt.active() && isHere(artifact::dungeon(data, c.hunt.artifact));
	if (!wantJob && !wantArtifact) return;

	// Células alcançáveis a partir da escada, pela distância a pé (portas contam como passagem).
	const int w = this->level.getWidth(), d = this->level.getDepth();
	std::vector<int> distance(static_cast<size_t>(w * d), -1);
	std::vector<std::pair<int, int>> order;
	std::queue<std::pair<int, int>> open;
	const int sx = static_cast<int>(std::floor(this->x)), sz = static_cast<int>(std::floor(this->z));
	if (!this->level.inBounds(sx, sz)) return;
	distance[static_cast<size_t>(sx + sz * w)] = 0;
	open.push({ sx, sz });
	while (!open.empty())
	{
		const auto [cx, cz] = open.front();
		open.pop();
		order.push_back({ cx, cz });
		const int dirX[4] = { 1, -1, 0, 0 }, dirZ[4] = { 0, 0, 1, -1 };
		for (int k = 0; k < 4; k++)
		{
			const int nx = cx + dirX[k], nz = cz + dirZ[k];
			if (!this->level.inBounds(nx, nz) || distance[static_cast<size_t>(nx + nz * w)] >= 0) continue;
			const Cell &cell = this->level.cell(nx, nz);
			if (cell.chasm || (this->level.blocksMovement(nx, nz) && cell.type != CellType::Door)) continue;
			distance[static_cast<size_t>(nx + nz * w)] = distance[static_cast<size_t>(cx + cz * w)] + 1;
			open.push({ nx, nz });
		}
	}

	// O mais longe possível (sem ser porta); o segundo alvo fica a três quartos do caminho.
	std::vector<std::pair<int, int>> spots;
	for (const auto &cell : order) if (this->level.cell(cell.first, cell.second).type != CellType::Door) spots.push_back(cell);
	if (spots.size() < 4) return;
	const auto far = spots.back(), near = spots[spots.size() * 3 / 4];

	if (wantArtifact) this->level.addFlat(this->game.getAssets(), this->palette, "CHESTC.IMG", far.first + 0.5f, far.second + 0.5f, ArtifactItem);
	if (!wantJob) return;
	const auto spot = wantArtifact ? near : far;
	const SideJob &job = c.job;
	if (job.kind == SideJob::Kind::Monster)
	{
		// A fera do governante: mais forte que as do mesmo tipo e já atenta ao jogador.
		const CreatureDefinition *base = data.getCreature(job.subject);
		if (!base) return;
		CreatureDefinition beast = *base;
		beast.originalName = jobs::targetName(this->game, job);
		beast.minHp *= 3;
		beast.maxHp *= 3;
		beast.minDamage += 2;
		beast.maxDamage += 4;
		beast.level += 2;
		beast.baseExperience *= 2;
		const int kind = this->creatures.addDefinition(beast);
		this->definitionText[kind] = job.subject;
		this->jobCreature = this->creatures.spawn(this->game.getAssets(), this->palette, kind, spot.first + 0.5f, spot.second + 0.5f, this->game.getRandom());
		return;
	}

	const std::string image = job.kind == SideJob::Kind::Rescue ? (job.subject % 2 == 0 ? "PRINCESS.IMG" : "PRINCE.IMG") : "BAGAGE1.IMG";
	if (!this->level.addFlat(this->game.getAssets(), this->palette, image, spot.first + 0.5f, spot.second + 0.5f, JobTargetItem))
		std::printf("alvo do trabalho: %s não encontrado\n", image.c_str());
}

bool arena::GameWorldScreen::tryTakeDungeonTarget(size_t sprite)
{
	CharacterDraft &c = this->game.getCharacter();
	const Localization &text = this->game.getText();
	const int itemId = this->level.getSprites()[sprite].itemId;
	this->removeSpriteAt(sprite);

	if (itemId == JobTargetItem && c.job.active() && c.job.inDungeon() && c.job.step == 0)
	{
		this->game.playSound("FANFARE2.VOC");
		this->openVision(text.get("Jobs.FoundTitle"), jobs::targetDone(this->game));
		this->persist();
		return true;
	}

	if (itemId == ArtifactItem && c.hunt.active())
	{
		const int id = c.hunt.artifact;
		c.inventory.push_back(artifact::makeItem(id));
		c.artifactsFound |= 1 << id;
		c.hunt = ArtifactHunt{};
		const std::string name = text.get("Artifacts.Name" + std::to_string(id));
		quest::note(c, ui::format(text.get("Artifacts.JournalFound"), { name, this->locationName }), "Artifacts.Lore" + std::to_string(id));
		this->game.playSound("FANFARE2.VOC");
		this->openVision(text.get("Artifacts.FoundTitle"), ui::format(text.get("Artifacts.Found"), { name, text.get("Artifacts.Power" + std::to_string(id)) }));
		this->persist();
		return true;
	}

	this->showMessage(text.get("Quest.NothingLeft"), 3.0);
	return true;
}
