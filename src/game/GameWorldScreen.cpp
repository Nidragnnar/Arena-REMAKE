#include <algorithm>
#include <chrono>
#include <cmath>

#include "../render/Ui.h"
#include "../render3d/Hud.h"
#include "Artifacts.h"
#include "GameWorldScreen.h"
#include "CityGen.h"
#include "GameClock.h"
#include "WildGen.h"
#include "DungeonGen.h"
#include "MainQuest.h"
#include "Leveling.h"
#include "Screens.h"

namespace
{
	constexpr float Pi = 3.14159265f;
	constexpr float PlayerRadius = 0.2f;
	constexpr float EyeHeight = 0.48f;
	constexpr float WalkSpeed = 2.2f;   // voxels por segundo
	constexpr float RunSpeed = 4.0f;
	constexpr float TurnSpeed = 2.4f;   // radianos por segundo (setas)
	constexpr float MouseSensitivity = 0.0035f;
	constexpr float ChasmDepth = 0.3f;
	constexpr float LiquidLevel = 0.12f; // superfície da água/lava abaixo do chão
	constexpr int DrawRadius = 22;
	constexpr int ExteriorDrawRadius = 44;

	using arena::Vec3;
}

arena::GameWorldScreen::GameWorldScreen(Game &game, const std::string &mifName) : Screen(game)
{
	this->palette = game.getDefaultPalette();
	this->mifName = mifName;
	this->locationName = game.getText().get("World.StartDungeonName");
	int province = 0, localId = 0;
	if (mifName.rfind("IN:", 0) == 0)
	{
		// Interior de um prédio da cidade; guarda a volta para a rua.
		std::vector<std::string> parts;
		size_t from = 3;
		for (size_t at = mifName.find('@', from); ; at = mifName.find('@', from))
		{
			parts.push_back(mifName.substr(from, at == std::string::npos ? std::string::npos : at - from));
			if (at == std::string::npos) break;
			from = at + 1;
		}

		if (parts.size() >= 5)
		{
			this->returnMap = parts[1];
			this->returnX = static_cast<float>(std::atof(parts[2].c_str()));
			this->returnZ = static_cast<float>(std::atof(parts[3].c_str()));
			this->returnYaw = static_cast<float>(std::atof(parts[4].c_str()));
		}

		this->loaded = this->loadMap(parts.empty() ? std::string() : parts[0]);
		this->locationName = parts.size() >= 6 ? parts[5] : this->locationName;
		this->escaped = true;
	}
	else if (std::sscanf(mifName.c_str(), "WILD:%d:%d", &province, &localId) == 2)
	{
		// "WILD:p:l" = janela em volta da cidade; "WILD:p:l:bx:by" = janela deslocada (ver shiftWilderness).
		int firstX = WildWindowFirst, firstY = WildWindowFirst;
		if (std::sscanf(mifName.c_str(), "WILD:%d:%d:%d:%d", &province, &localId, &firstX, &firstY) == 4)
		{
			this->wildFirstX = std::clamp(firstX, 0, WildLastFirst);
			this->wildFirstY = std::clamp(firstY, 0, WildLastFirst);
		}
		// Ermo em volta da cidade (blocos do original sorteados pelo nome da cidade).
		const CityLocation *city = game.getData().getCity(province, localId);
		MifFile mif;
		const Weather w = weatherFor(city, game.getCharacter().gameMinutes);
		if (!city) this->loadError = "Ermo " + mifName + " não existe.";
		else if (generateWilderness(game.getAssets(), game.getData(), *city, w == Snow ? 'S' : w == Clear ? 'N' : 'R', mif, this->loadError, this->wildFirstX, this->wildFirstY))
		{
			this->loaded = this->level.build(game.getAssets(), this->palette, mif, true, this->loadError);
			this->locationName = ui::format(game.getText().get("Wild.Name"), { city->name });
			this->escaped = true;
		}
	}
	else if (std::sscanf(mifName.c_str(), "CITY:%d:%d", &province, &localId) == 2)
	{
		// Cidade gerada como no original (molde + quarteirões pela semente do local).
		const CityLocation *city = game.getData().getCity(province, localId);
		MifFile mif;
		std::string inf;
		if (!city) this->loadError = "Cidade " + mifName + " não existe.";
		else if (generateCity(game.getAssets(), game.getData().getCityGen(), *city, mif, inf, this->loadError))
		{
			this->loaded = this->level.build(game.getAssets(), this->palette, mif, true, this->loadError);
			this->locationName = city->name;
			this->escaped = true;
			if (this->loaded) this->citizens.init(game.getAssets(), this->palette, game.getData(), this->level, *city, game.getRandom());
		}
	}
	else
	{
		this->loaded = this->level.load(game.getAssets(), this->palette, mifName, this->loadError);
	}

	if (!this->loaded)
	{
		return;
	}

	this->x = this->level.getStartX();
	this->z = this->level.getStartZ();

	// Olha para a direção com o corredor livre mais longo a partir da entrada.
	int best = 0;
	for (int dir = 0; dir < 4; dir++)
	{
		const int dx = (dir == 0) - (dir == 1);
		const int dz = (dir == 2) - (dir == 3);
		int run = 0;
		for (int step = 1; step < 12 && !this->level.blocksMovement(static_cast<int>(this->x) + dx * step, static_cast<int>(this->z) + dz * step); step++) run++;
		if (run > best)
		{
			best = run;
			this->yaw = std::atan2(static_cast<float>(dz), static_cast<float>(dx));
		}
	}

	const CharacterDraft &saved = game.getCharacter();
	this->health = (saved.health > 0) ? std::min(saved.health, saved.maxHealth) : std::max(1, saved.maxHealth);
	this->experience = saved.experience;
	this->spellPoints = (saved.spellPoints >= 0) ? std::min(saved.spellPoints, saved.maxSpellPoints) : saved.maxSpellPoints;
	this->loadSpellArt();
	if (this->isDungeon() && this->level.getCreatureSpawns().empty())
	{
		this->spawnBeside(true); // perto da entrada (para não nascer monstro em cima)

		// Masmorras geradas (sem monstros no mapa): como no original, os monstros são sorteados ao entrar (mais fortes com o
		// nível do jogador). A semente vem do nome do mapa, para o save reencontrar os mesmos.
		uint32_t seed = 2166136261u;
		for (const char ch : this->mifName.substr(0, this->mifName.find('@'))) seed = (seed ^ static_cast<uint8_t>(ch)) * 16777619u;
		ArenaRandom random(seed);
		const int playerLevel = game.getCharacter().level;
		const int strongest = std::min(22, 3 + playerLevel * 2);
		int floorCells = 0;
		for (int cx = 0; cx < this->level.getWidth(); cx++)
			for (int cz = 0; cz < this->level.getDepth(); cz++) if (!this->level.blocksMovement(cx, cz) && !this->level.cell(cx, cz).chasm) floorCells++;
		const int wanted = std::clamp(floorCells / 90, 4, 24);
		for (int tries = 0, placed = 0; placed < wanted && tries < wanted * 50; tries++)
		{
			const int cx = random.next() % this->level.getWidth(), cz = random.next() % this->level.getDepth();
			if (this->level.blocksMovement(cx, cz) || this->level.cell(cx, cz).chasm) continue;
			const float dx = cx + 0.5f - this->x, dz = cz + 0.5f - this->z;
			if (dx * dx + dz * dz < 8.0f * 8.0f) continue; // longe da entrada
			const int kind = random.next() % (strongest + 1);
			if (!game.getData().getCreature(kind)) continue;
			this->level.addCreatureSpawn({ cx + 0.5f, cz + 0.5f, kind });
			placed++;
		}
	}

	this->creatures.init(game.getAssets(), this->palette, game.getData(), this->level, game.getRandom());
	if (this->isDungeon()) this->placeDungeonTargets();
	if (this->level.isExterior() && !this->isWild())
	{
		// Procurado nesta cidade? Os guardas vêm assim que o jogador aparece (ver update).
		CharacterDraft &hero = game.getCharacter();
		const CityLocation *city = this->currentCity();
		const auto crime = city ? hero.wanted.find(city->globalId) : hero.wanted.end();
		if (crime != hero.wanted.end() && crime->second > hero.gameMinutes) this->guardsOnArrival = true;
		else if (crime != hero.wanted.end()) hero.wanted.erase(crime);
	}
	this->setupWeapon();

	// Música: dia de sol nas cidades; masmorra (DUNGEON1 tem problemas no original; sorteia entre 2 e 5).
	if (this->level.isExterior())
	{
		this->nightMusic = clock::isNight(game.getCharacter().gameMinutes);
		const Weather w = this->weather();
		game.playMusic(this->nightMusic ? "NIGHT.XMI" : w == Rain ? "RAINING.XMI" : w == Snow ? "SNOWING.XMI" : w == Overcast ? "OVERCAST.XMI" : "SUNNYDAY.XMI");
		this->citizens.setNight(this->nightMusic);
	}
	else if (this->isDungeon())
	{
		game.playMusic("DUNGEON" + std::to_string(2 + static_cast<int>(game.getRandom()() % 4u)) + ".XMI");
	}
	else if (!this->returnMap.empty())
	{
		const std::string upperMif = this->level.getInfName();
		const char *music = upperMif.rfind("TAV", 0) == 0 || upperMif.rfind("NEWTAV", 0) == 0 ? "TAVERN.XMI"
			: upperMif.rfind("TEMPLE", 0) == 0 ? "TEMPLE.XMI" : upperMif.rfind("EQUIP", 0) == 0 ? "EQUIPMNT.XMI"
			: upperMif.rfind("MAGE", 0) == 0 || upperMif.rfind("MG", 0) == 0 ? "MAGIC_2.XMI" : upperMif.rfind("PAL", 0) == 0 || upperMif.rfind("IMPPAL", 0) == 0 ? "PALACE.XMI" : "SNEAKING.XMI";
		game.playMusic(music);
	}
	else game.playMusic("DUNGEON" + std::to_string(2 + static_cast<int>(game.getRandom()() % 4u)) + ".XMI");

	// Jogo carregado: restaura posição, portas, criaturas, saques...
	const auto world = game.takePendingWorld();
	if (!world.empty())
	{
		this->restoreWorld(world);
		this->helpTimer = 0.0;
		return;
	}

	// Saindo de um prédio: aparece na rua, na frente da porta.
	const std::string spawn = game.takePendingSpawn();
	if (this->isDungeon() && (spawn.empty() || spawn.rfind("STAIR:", 0) == 0))
	{
		// Entrada ou troca de andar: ao lado da escada certa.
		this->spawnBeside(spawn != "STAIR:down");
		const std::string note = game.takePendingNote();
		this->showMessage(!note.empty() ? note : ui::format(game.getText().get("Dungeon.Entered"), { this->locationName }), 4.0);
		this->helpTimer = 0.0;
		this->palaceArrival();
		this->persist();
		return;
	}

	if (!spawn.empty())
	{
		this->debugCommand("goto", spawn);
		this->helpTimer = 0.0;
		this->persist();
		return;
	}

	if (this->isWild() && world.empty())
	{
		// Aparece do lado de fora do portão da cidade mais próximo.
		float bestOut = 1e9f;
		const int dirX[4] = { 1, -1, 0, 0 }, dirZ[4] = { 0, 0, 1, -1 };
		for (int cx = 0; cx < this->level.getWidth(); cx++)
			for (int cz = 0; cz < this->level.getDepth(); cz++)
			{
				const int m = this->level.cell(cx, cz).menu;
				if (m != 6 && m != 7) continue;
				for (int d = 0; d < 4; d++)
				{
					const int ox = cx + dirX[d] * 2, oz = cz + dirZ[d] * 2;
					if (this->level.blocksMovement(ox, oz) || this->level.blocksMovement(cx + dirX[d], cz + dirZ[d])) continue;
					// Fora da cidade = longe do centro dela.
					const float out = std::fabs(ox + 0.5f - this->wildCityX() - WildChunk) + std::fabs(oz + 0.5f - this->wildCityZ() - WildChunk);
					if (-out >= bestOut) continue;
					bestOut = -out;
					this->x = ox + 0.5f; this->z = oz + 0.5f;
					this->yaw = std::atan2(static_cast<float>(dirZ[d]), static_cast<float>(dirX[d]));
				}
			}
	}

	// Chegada numa cidade ou num prédio: anuncia e já grava o jogo neste lugar.
	if (this->level.isExterior() || !this->returnMap.empty())
	{
		if (auto vision = game.takePendingVision(); !vision.second.empty()) this->openVision(vision.first, vision.second);
		const std::string note = game.takePendingNote();
		this->showMessage(!note.empty() ? note : ui::format(game.getText().get(this->level.isExterior() ? "City.Arrived" : "City.Entered"), { this->locationName }), 4.0);
		this->helpTimer = 0.0;

		// Palácio: o governante paga (ou cobra) o trabalho atual, ou propõe um novo.
		const CityLocation *city = this->isPalace() && this->visionText.empty() ? this->currentCity() : nullptr;
		if (city)
		{
			const jobs::Outcome outcome = jobs::arrive(game, *city, true);
			if (!outcome.text.empty()) this->openVision(outcome.title, outcome.text);
			else this->offerJob(true);
		}
		this->persist();
		return;
	}

	// O texto de abertura da cela fica num gatilho ao lado da entrada: dispara já ao começar.
	this->lastCellX = static_cast<int>(std::floor(this->x));
	this->lastCellZ = static_cast<int>(std::floor(this->z));
	for (int dx = -1; dx <= 1; dx++)
		for (int dz = -1; dz <= 1; dz++)
			this->checkTriggers(this->lastCellX + dx, this->lastCellZ + dz);
}

void arena::GameWorldScreen::removeSpriteAt(size_t index)
{
	const auto &sprites = this->level.getSprites();
	if (index >= sprites.size()) return;
	this->removedSprites.emplace_back(sprites[index].x, sprites[index].z, sprites[index].name);
	this->level.removeSprite(index);
}

void arena::GameWorldScreen::playAt(const std::string &sound, float worldX, float worldZ, float volume)
{
	// Volume pela distância e pan pelo lado em relação à câmera.
	const float dx = worldX - this->x, dz = worldZ - this->z;
	const float dist = std::sqrt(dx * dx + dz * dz);
	const float gain = volume * std::clamp(1.0f - dist / 14.0f, 0.0f, 1.0f);
	const float rx = -std::sin(this->yaw), rz = std::cos(this->yaw);
	const float pan = (dist > 0.01f) ? std::clamp((dx * rx + dz * rz) / dist, -1.0f, 1.0f) * 0.8f : 0.0f;
	this->game.playSound(sound, gain, pan);
}

void arena::GameWorldScreen::checkTriggers(int cellX, int cellZ)
{
	const Localization &text = this->game.getText();
	const auto &triggers = this->level.getTriggers();
	for (size_t i = 0; i < triggers.size(); i++)
	{
		const Trigger &trigger = triggers[i];
		if (trigger.x != cellX || trigger.z != cellZ) continue;

		if (trigger.soundIndex >= 0)
		{
			const std::string sound = this->level.getSoundFile(trigger.soundIndex);
			if (!sound.empty()) this->game.playSound(sound, 0.7f);
		}

		const InfText *entry = this->level.getText(trigger.textIndex);
		if (entry == nullptr || this->firedTriggers.contains(i)) continue;

		// Cada gatilho de texto aparece uma vez (os "~" do original e os repetidos pelo mapa).
		this->firedTriggers.insert(i);
		if (entry->keyId)
		{
			this->keys.insert(*entry->keyId);
			this->game.playSound("CLANK.VOC", 0.8f);
			this->showMessage(text.get("World.KeyGained"), 4.0);
		}
		else if (!entry->text.empty() && !entry->riddle)
		{
			const std::string key = "Triggers." + this->level.getInfName() + "." + std::to_string(trigger.textIndex);
			this->showMessage(text.getOr(key, entry->text), 9.0);
		}
	}
}

void arena::GameWorldScreen::rest()
{
	const Localization &text = this->game.getText();
	for (const Creature &c : this->creatures.getCreatures())
	{
		const bool alive = c.state != Creature::State::Dying && c.state != Creature::State::Dead;
		const float dx = c.x - this->x, dz = c.z - this->z;
		if (alive && c.awake && dx * dx + dz * dz < 10.0f * 10.0f)
		{
			this->showMessage(text.get("World.RestEnemies"), 3.0);
			return;
		}
	}

	this->game.getCharacter().gameMinutes += 120.0; // duas horas de descanso
	this->health = std::max(1, this->game.getCharacter().maxHealth);
	// Feiticeiros (classe 3) não recuperam magia descansando, como no original.
	if (this->game.getCharacter().classIndex != 3) this->spellPoints = this->game.getCharacter().maxSpellPoints;
	this->persist();
	this->showMessage(text.get("World.Rested"), 3.0);
	this->tryDream();
}

void arena::GameWorldScreen::setupWeapon()
{
	// Arma equipada no inventário (sem arma: punhos, mais fortes para o Monge).
	const CharacterDraft &character = this->game.getCharacter();
	const ItemTables &tables = this->game.getData().getItems();
	this->weapon = Weapon{};
	this->weaponMaterialBonus = 0;
	const auto equipped = std::find_if(character.inventory.begin(), character.inventory.end(),
		[](const Item &item) { return item.equipped && item.kind == ItemKind::Weapon; });
	std::string file;
	if (equipped != character.inventory.end())
	{
		file = weaponAnimationFile(equipped->type, tables);
		this->weapon.name = itemName(this->game, *equipped);
		this->weapon.minDamage = weaponMinDamage(*equipped, tables);
		this->weapon.maxDamage = weaponMaxDamage(*equipped, tables);
		this->weaponMaterialBonus = tables.materialBonus[static_cast<size_t>(equipped->material)];
	}
	else
	{
		file = weaponAnimationFile(-1, tables);
		this->weapon.name = this->game.getText().get("Weapons.Fists");
		this->weapon.fists = true;
		this->weapon.minDamage = 1;
		this->weapon.maxDamage = (character.classIndex == 12) ? 8 : 3;
	}

	this->weapon.bow = equipped != character.inventory.end() && equipped->type >= 16;
	this->weapon.idleFrame = this->weapon.bow ? 0 : this->weapon.fists ? 12 : 32;
	const std::vector<IndexedImage> images = this->game.loadFrames(file);
	for (size_t i = 0; i < images.size(); i++)
	{
		const IndexedImage &image = images[i];
		// O .CFA do arco não tem posição: embaixo, no centro da área 3D original (320x147).
		const int offsetX = this->weapon.bow ? 160 - image.width / 2 : image.offsetX, offsetY = this->weapon.bow ? 147 - image.height : image.offsetY;
		this->weapon.frames.push_back({ static_cast<int>(this->weapon.textures.size()), offsetX, offsetY });
		this->weapon.textures.push_back(toTexture(image, this->palette, file, static_cast<int>(i), static_cast<int>(images.size())));
	}
}

arena::PlayerCombatStats arena::GameWorldScreen::combatStats() const
{
	PlayerCombatStats stats;
	stats.x = this->x;
	stats.z = this->z;
	stats.alive = this->health > 0;
	stats.invisible = this->hasPlayerEffect(SpellEffectType::Invisibility);
	const CharacterDraft &character = this->game.getCharacter();
	stats.level = character.level;
	stats.classIndex = character.classIndex;
	const ItemTables &tables = this->game.getData().getItems();
	stats.defenseBonus = hitBonusFor(character.attributes[3] + this->fortifyBonus(3));
	for (const Item &item : character.inventory)
	{
		if (!item.equipped || item.kind != ItemKind::Armor) continue;
		if (item.isShield()) stats.shieldArmor = armorRating(item, tables);
		else stats.partArmor[static_cast<size_t>(bodyPartOf(item))] = armorRating(item, tables);
	}
	// Itens mágicos de proteção (grupo 4) somam ao corpo inteiro, como o escudo.
	for (const Item &item : character.inventory) if (item.equipped && item.isMagicItem() && item.magicGroup == 4) stats.shieldArmor += item.charges;

	return stats;
}

std::string arena::GameWorldScreen::currentLocation() const
{
	if (this->level.isExterior() || !this->returnMap.empty()) return this->locationName; // cidade, ermo ou prédio
	return this->game.getText().get(this->escaped ? "Escape.Location" : "World.StartDungeonName");
}

void arena::GameWorldScreen::persist()
{
	CharacterDraft &character = this->game.getCharacter();
	character.health = this->health;
	character.spellPoints = this->spellPoints;
	character.experience = this->experience;
	const std::string location = this->currentLocation();
	this->game.saveCharacter(this->game.getSavePath(), this->serializeWorld(), location);
}

bool arena::GameWorldScreen::tryOpenLoot()
{
	const Localization &text = this->game.getText();
	const float fx = std::cos(this->yaw), fz = std::sin(this->yaw);
	auto inFront = [&](float sx, float sz, float reach)
	{
		const float dx = sx - this->x, dz = sz - this->z;
		const float along = dx * fx + dz * fz;
		return along > -0.2f && along < reach && std::fabs(dx * fz - dz * fx) < 0.55f;
	};

	// Corpos de criaturas mortas.
	const auto &all = this->creatures.getCreatures();
	for (size_t i = 0; i < all.size(); i++)
	{
		const Creature &c = all[i];
		if (c.state != Creature::State::Dead || c.gone || !inFront(c.x, c.z, 1.8f)) continue;
		auto it = this->corpseLoot.find(static_cast<int>(i));
		if (it == this->corpseLoot.end()) continue;
		this->lootMenu.open(&it->second, ui::format(text.get("Loot.TitleCorpse"),
			{ text.getOr("Creatures.Name" + std::to_string(this->textIndexOf(c.definition)), this->creatures.definitionOf(static_cast<int>(i)).originalName) }));
		this->openContainer = { -1, -1 };
		this->overlay = Overlay::Loot;
		return true;
	}

	// Baús, pilhas de tesouro/equipamento e corpos do mapa (*ITEM 2..7).
	for (const Sprite &sprite : this->level.getSprites())
	{
		if (sprite.itemId < 2 || sprite.itemId > 7 || !inFront(sprite.x, sprite.z, 1.8f)) continue;
		const std::pair<int, int> key{ static_cast<int>(std::floor(sprite.x)), static_cast<int>(std::floor(sprite.z)) };
		auto it = this->containerLoot.find(key);
		if (it == this->containerLoot.end())
		{
			const LootSource source = (sprite.itemId == 2) ? LootSource::DeadBody : (sprite.itemId == 3) ? LootSource::EquipmentPile
				: (sprite.itemId == 7) ? LootSource::Chest : LootSource::TreasurePile;
			it = this->containerLoot.emplace(key, generateLoot(source, -1, this->game.getData().getItems(), this->game.getRandom(), this->game.getCharacter().level)).first;
		}

		const char *titleKey = (sprite.itemId == 2) ? "Loot.TitleBody" : (sprite.itemId == 3) ? "Loot.TitleEquipment" : (sprite.itemId == 7) ? "Loot.TitleChest" : "Loot.TitleTreasure";
		this->lootMenu.open(&it->second, text.get(titleKey));
		this->openContainer = key;
		this->overlay = Overlay::Loot;
		return true;
	}

	return false;
}

void arena::GameWorldScreen::applyOverlayResult(const OverlayResult &result)
{
	if (result.inventoryChanged) artifact::syncSpells(this->game.getCharacter(), this->game.getData().getItems()); // poderes de itens equipados

	if (result.spellPointsDelta != 0)
	{
		this->spellPoints = std::clamp(this->spellPoints + result.spellPointsDelta, 0, this->game.getCharacter().maxSpellPoints);
	}

	if (result.healthDelta != 0)
	{
		this->health = std::clamp(this->health + result.healthDelta, 1, std::max(1, this->game.getCharacter().maxHealth));
	}

	if (!result.message.empty())
	{
		this->showMessage(result.message, 3.0);
	}

	if (result.inventoryChanged)
	{
		this->setupWeapon();
		this->persist();
	}

	if (result.close)
	{
		// Pilhas esvaziadas somem do mapa (baús e corpos ficam).
		if (this->overlay == Overlay::Loot && this->openContainer.first >= 0 && this->lootMenu.isEmpty())
		{
			const auto &sprites = this->level.getSprites();
			for (size_t i = 0; i < sprites.size(); i++)
			{
				const bool pile = sprites[i].itemId >= 3 && sprites[i].itemId <= 6;
				if (pile && static_cast<int>(std::floor(sprites[i].x)) == this->openContainer.first && static_cast<int>(std::floor(sprites[i].z)) == this->openContainer.second)
				{
					this->removeSpriteAt(i);
					break;
				}
			}
		}

		this->overlay = Overlay::None;
		this->heldKeys.clear();
		this->persist();
	}

	if (result.castSpell >= 0) this->castFreeSpell(result.castSpell);
}

void arena::GameWorldScreen::drawOverlay(std::vector<uint32_t> &pixels, int width, int height)
{
	hud::Frame frame{ pixels, width, height };
	kit::Painter painter(frame, this->game.getFonts());
	if (this->overlay == Overlay::Character)
	{
		this->characterMenu.render(painter, this->game, this->menuState());
	}
	else if (this->overlay == Overlay::LevelUp)
	{
		this->levelUpMenu.render(painter, this->game);
	}
	else if (this->overlay == Overlay::SaveLoad)
	{
		this->saveLoadMenu.render(painter, this->game);
	}
	else if (this->overlay == Overlay::Service)
	{
		this->serviceMenu.render(painter, this->game);
	}
	else if (this->overlay == Overlay::Dialogue)
	{
		this->dialogueMenu.render(painter, this->game);
	}
	else if (this->overlay == Overlay::Journal)
	{
		this->journalMenu.render(painter, this->game);
	}
	else if (this->overlay == Overlay::Travel)
	{
		this->travelMenu.render(painter, this->game);
	}
	else
	{
		painter.fill(Rect{ 0, 0, width, height }, hud::rgb(0, 0, 0, 120));
		this->lootMenu.render(painter, this->game);
	}
}

void arena::GameWorldScreen::checkLevelUp()
{
	// Experiência suficiente: sobe (um ou mais níveis) e abre a distribuição de pontos, como no original.
	CharacterDraft &c = this->game.getCharacter();
	if (this->overlay != Overlay::None || this->health <= 0) return;
	c.experience = this->experience;
	const ClassDefinition &def = this->game.getData().getClass(std::max(0, c.classIndex));
	int gained = 0;
	for (int g = tryLevelUp(c, def, this->game.getRandom()); g > 0; g = tryLevelUp(c, def, this->game.getRandom())) gained += g;
	if (gained == 0 && c.levelPoints == 0) return;

	this->health = std::min(c.maxHealth, this->health + gained);
	if (gained > 0)
	{
		this->game.playSound("FANFARE1.VOC", 0.9f);
		this->showMessage(ui::format(this->game.getText().get("Level.Reached"), { std::to_string(c.level) }), 4.0);
	}

	this->levelUpMenu.open(gained);
	this->overlay = Overlay::LevelUp;
	this->swinging = false;
	this->heldKeys.clear();
	this->holdLeft = this->holdRight = this->holdUp = this->holdDown = false;
	this->persist();
}

void arena::GameWorldScreen::openCharacterMenu(CharacterMenu::Tab tab)
{
	this->characterMenu.open(this->game, tab);
	this->overlay = Overlay::Character;
	this->swinging = false;
	this->heldKeys.clear();
	this->holdLeft = this->holdRight = this->holdUp = this->holdDown = false;
}

std::vector<std::string> arena::GameWorldScreen::describeEffects() const
{
	const Localization &text = this->game.getText();
	std::vector<std::string> lines;
	if (this->shieldPoints > 0) lines.push_back(ui::format(text.get("Effects.Shield"), { std::to_string(this->shieldPoints) }));
	if (this->paralysisTimer > 0.0) lines.push_back(ui::format(text.get("Effects.Paralyzed"), { std::to_string(static_cast<int>(std::ceil(this->paralysisTimer))) }));
	if (this->game.getCharacter().disease >= 0) lines.push_back(ui::format(text.get("Effects.Disease"), { this->diseaseName(this->game.getCharacter().disease) }));
	for (const PlayerEffect &e : this->playerEffects)
	{
		const std::string seconds = std::to_string(static_cast<int>(std::ceil(e.remaining)));
		switch (e.type)
		{
		case SpellEffectType::Light: lines.push_back(ui::format(text.get("Effects.Light"), { seconds })); break;
		case SpellEffectType::Invisibility: lines.push_back(ui::format(text.get("Effects.Invisible"), { seconds })); break;
		case SpellEffectType::Regenerate: lines.push_back(ui::format(text.get("Effects.Regenerate"), { seconds })); break;
		case SpellEffectType::Fortify:
			lines.push_back(ui::format(text.get("Effects.Fortify"), { this->game.attributeName(std::clamp(e.sub, 0, AttributeCount - 1), false),
				std::to_string(e.magnitude), seconds }));
			break;
		case SpellEffectType::ElementalResistance:
			lines.push_back(ui::format(text.get("Effects.Resist"), { text.get("Spells.Resist" + std::to_string(std::clamp(e.sub, 0, 4))), seconds }));
			break;
		default: break;
		}
	}

	return lines;
}

arena::MenuState arena::GameWorldScreen::menuState() const
{
	MenuState state;
	state.health = this->health;
	state.spellPoints = this->spellPoints;
	state.experience = this->experience;
	state.effects = this->describeEffects();
	return state;
}

void arena::GameWorldScreen::screenButtonAction(char action)
{
	if (action == 'v')
	{
		this->visionText.clear();
		return;
	}
	if (action == 'y' || action == 'n')
	{
		this->answerOffer(action == 'y');
		return;
	}

	this->chapterOpen = false;
	switch (action)
	{
	case 'x': break; // continuar explorando a prisão
	case 't':
		// O portão leva a uma cidade da província natal (sorteada ao atravessar).
		if (this->nextCity >= 0)
		{
			const int province = std::clamp(this->game.getCharacter().raceIndex, 0, 7);
			this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, "CITY:" + std::to_string(province) + ":" + std::to_string(this->nextCity)));
		}
		break;
	case 'c': this->paused = false; break;
	case 'p': this->paused = false; this->openCharacterMenu(CharacterMenu::Tab::Character); break;
	case 'b': this->paused = false; this->openCharacterMenu(CharacterMenu::Tab::Spells); break;
	case 'h': this->paused = false; this->helpTimer = 1e9; break;
	case 'j': this->paused = false; this->openJournal(); break;
	case 'u':
	{
		// Tamanho da interface: 75% -> 100% -> 125% -> 150% -> 200% -> 75%; fica gravado no settings.ini.
		static const float Steps[] = { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
		const float current = kit::interfaceScale();
		float next = Steps[0];
		for (const float step : Steps) if (step > current + 0.01f) { next = step; break; }
		kit::setInterfaceScale(next);
		char value[16];
		std::snprintf(value, sizeof(value), "%.2f", next);
		this->game.saveSetting("Video", "UiScale", value);
		this->paused = true; // continua na pausa para ver o novo tamanho
		break;
	}
	case 's':
	case 'l':
		this->paused = false;
		this->saveLoadMenu.open(this->game, action == 's' ? SaveLoadMenu::Mode::Save : SaveLoadMenu::Mode::Load);
		this->overlay = Overlay::SaveLoad;
		break;
	case 'r': this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, this->game.getText().get("World.StartDungeonMif"))); break;
	case 'm':
		if (this->health > 0) this->persist();
		this->game.changeScreen(std::make_unique<MainMenuScreen>(this->game));
		break;
	default: break;
	}
}

bool arena::GameWorldScreen::debugCommand(const std::string &name, const std::string &arg)
{
	if (name == "goto" && this->loaded)
	{
		// goto:x,z[,graus] posiciona o jogador (coordenadas do mundo) e opcionalmente a direção.
		float gx = 0, gz = 0, degrees = 0;
		const int count = std::sscanf(arg.c_str(), "%f,%f,%f", &gx, &gz, &degrees);
		if (count < 2) return false;
		this->x = gx;
		this->z = gz;
		if (count >= 3) this->yaw = degrees * Pi / 180.0f;
		this->lastCellX = static_cast<int>(std::floor(gx));
		this->lastCellZ = static_cast<int>(std::floor(gz));
		this->eyeOffset = this->level.floorHeightAt(this->lastCellX, this->lastCellZ);
		return true;
	}

	if (name == "gotosprite" && this->loaded)
	{
		// gotosprite:NOME,N fica diante do N-ésimo sprite cujo arquivo contém NOME (ex.: BRAZIER,0).
		const size_t comma = arg.find(',');
		const std::string pattern = arg.substr(0, comma);
		int wanted = (comma == std::string::npos) ? 0 : std::atoi(arg.c_str() + comma + 1);
		const int dirX[4] = { 1, -1, 0, 0 }, dirZ[4] = { 0, 0, 1, -1 };
		for (const Sprite &sprite : this->level.getSprites())
		{
			if (toUpperAscii(sprite.name).find(toUpperAscii(pattern)) == std::string::npos || wanted-- > 0) continue;
			for (const float reach : { 1.6f, 1.2f, 0.9f })
				for (int d = 0; d < 4; d++)
				{
					const float gx = sprite.x + dirX[d] * reach, gz = sprite.z + dirZ[d] * reach;
					if (this->collides(gx, gz)) continue;
					char where[96];
					std::snprintf(where, sizeof(where), "%.2f,%.2f,%.1f", gx, gz, std::atan2(static_cast<float>(-dirZ[d]), static_cast<float>(-dirX[d])) * 180.0f / Pi);
					return this->debugCommand("goto", where);
				}
			std::printf("gotosprite: %s em %.1f,%.1f cercado\n", sprite.name.c_str(), sprite.x, sprite.z);
		}

		std::printf("gotosprite: nenhum sprite \"%s\" acessível; sprites do nível:", pattern.c_str());
		std::set<std::string> names;
		for (const Sprite &sprite : this->level.getSprites()) names.insert(sprite.name);
		for (const std::string &spriteName : names) std::printf(" %s", spriteName.c_str());
		std::printf("\n");
		return false;
	}

	if (name == "cells" && this->loaded)
	{
		// cells:raio imprime o tipo, chão e andares das células em volta (depuração).
		const int r = std::max(1, std::atoi(arg.c_str()));
		const int px = static_cast<int>(this->x), pz = static_cast<int>(this->z);
		for (int cx = px - r; cx <= px + r; cx++)
		{
			std::string line;
			for (int cz = pz - r; cz <= pz + r; cz++)
			{
				const Cell &c = this->level.cell(cx, cz);
				char item[48];
				std::snprintf(item, sizeof(item), "%d/%d/%d%s%s ", static_cast<int>(c.type), c.floorTexture, c.upperStories, c.chasm ? "~" : "", (cx == px && cz == pz) ? "@" : "");
				line += item;
			}
			std::printf("%3d: %s\n", cx, line.c_str());
		}
		return true;
	}

	if (name == "gotodoor" && this->loaded)
	{
		// gotodoor:menu[,n] fica diante da n-ésima porta com esse *MENU, olhando para ela.
		int menu = 0, wanted = 0;
		std::sscanf(arg.c_str(), "%d,%d", &menu, &wanted);
		const int dirX[4] = { 1, -1, 0, 0 }, dirZ[4] = { 0, 0, 1, -1 };
		for (int cx = 0; cx < this->level.getWidth(); cx++)
			for (int cz = 0; cz < this->level.getDepth(); cz++)
			{
				if (this->level.cell(cx, cz).menu != menu) continue;
				for (int d = 0; d < 4; d++)
				{
					if (this->level.blocksMovement(cx + dirX[d], cz + dirZ[d]) || wanted-- > 0) continue;
					char where[96];
					std::snprintf(where, sizeof(where), "%.2f,%.2f,%.1f", cx + 0.5f + dirX[d] * 1.2f, cz + 0.5f + dirZ[d] * 1.2f,
						std::atan2(static_cast<float>(-dirZ[d]), static_cast<float>(-dirX[d])) * 180.0f / Pi);
					std::printf("gotodoor: célula %d,%d\n", cx, cz);
					return this->debugCommand("goto", where);
				}
			}
		return false;
	}

	if (name == "gotoclerk" && this->loaded)
	{
		// gotoclerk fica de frente para o atendente do prédio (taverneiro, ferreiro, sacerdote, mago).
		static const char *Clerks[] = { "BARTEND.DFA", "SMITH.DFA", "PREST1.DFA", "PREST2.DFA", "WIZZ1.DFA" };
		const int dirX[4] = { 1, -1, 0, 0 }, dirZ[4] = { 0, 0, 1, -1 };
		for (const Sprite &sprite : this->level.getSprites())
		{
			if (std::find_if(std::begin(Clerks), std::end(Clerks), [&sprite](const char *n) { return sprite.name == n; }) == std::end(Clerks)) continue;
			for (int d = 0; d < 4; d++)
				for (int step = 1; step <= 2; step++)
				{
					const float gx = sprite.x + dirX[d] * step, gz = sprite.z + dirZ[d] * step;
					if (this->collides(gx, gz)) continue;
					char where[96];
					std::snprintf(where, sizeof(where), "%.2f,%.2f,%.1f", gx, gz, std::atan2(static_cast<float>(-dirZ[d]), static_cast<float>(-dirX[d])) * 180.0f / Pi);
					return this->debugCommand("goto", where);
				}
		}
		return false;
	}

	if (name == "citizeninfo" && this->loaded)
	{
		const int i = std::atoi(arg.c_str());
		const auto [cx, cz] = this->citizens.position(i);
		std::printf("morador %d: %.2f,%.2f ângulo %.0f | câmera %.2f,%.2f yaw %.0f\n", i, cx, cz, this->citizens.angleOf(i) * 180.0f / Pi, this->x, this->z, this->yaw * 180.0f / Pi);
		return true;
	}

	if (name == "gotocitizen" && this->loaded)
	{
		// gotocitizen:n fica numa célula livre ao lado do n-ésimo morador, olhando para ele.
		const int index = std::atoi(arg.c_str());
		if (index < 0 || index >= static_cast<int>(this->citizens.count())) return false;
		const auto [cx, cz] = this->citizens.position(index);
		const int dirX[4] = { 1, -1, 0, 0 }, dirZ[4] = { 0, 0, 1, -1 };
		for (int step = 2; step <= 3; step++)
			for (int d = 0; d < 4; d++)
			{
				const float gx = cx + dirX[d] * step, gz = cz + dirZ[d] * step;
				if (this->collides(gx, gz)) continue;
				char where[96];
				std::snprintf(where, sizeof(where), "%.2f,%.2f,%.1f", gx, gz, std::atan2(static_cast<float>(-dirZ[d]), static_cast<float>(-dirX[d])) * 180.0f / Pi);
				return this->debugCommand("goto", where);
			}
		return false;
	}

	if (name == "setday")
	{
		// setday:n muda o dia (o tempo da cidade é sorteado por dia), mantendo a hora.
		double &minutes = this->game.getCharacter().gameMinutes;
		minutes = std::atof(arg.c_str()) * clock::MinutesPerDay + clock::minuteOfDay(minutes);
		std::printf("tempo do dia %s: %d\n", arg.c_str(), static_cast<int>(this->weather()));
		this->updateDayNight(true);
		return true;
	}

	if (name == "settime")
	{
		// settime:horas (ex.: 18.5) muda a hora do dia, mantendo a data.
		double &minutes = this->game.getCharacter().gameMinutes;
		minutes = std::floor(minutes / clock::MinutesPerDay) * clock::MinutesPerDay + std::atof(arg.c_str()) * 60.0;
		this->skyDaylight = -1.0f;
		this->updateDayNight(false);
		return true;
	}

	if (name == "gotostair" && this->loaded)
	{
		// gotostair:up|down fica ao lado da escada, olhando para ela.
		this->spawnBeside(arg != "down");
		this->yaw += Pi;
		std::printf("escada %s: %.1f,%.1f | criaturas %zu\n", arg.c_str(), this->x, this->z, this->creatures.getCreatures().size());
		return true;
	}

	if (name == "menus" && this->loaded)
	{
		// menus lista os tipos de porta (*MENU) do mapa e quantas há de cada (depuração).
		std::map<int, int> counts;
		for (int cx = 0; cx < this->level.getWidth(); cx++)
			for (int cz = 0; cz < this->level.getDepth(); cz++)
				if (this->level.cell(cx, cz).menu >= 0) counts[this->level.cell(cx, cz).menu]++;
		std::printf("portas:");
		for (const auto &[menu, count] : counts) std::printf(" %d(x%d)", menu, count);
		std::printf("\n");
		const int only = arg.empty() ? -1 : std::atoi(arg.c_str());
		for (int cx = 0; only >= 0 && cx < this->level.getWidth(); cx++)
			for (int cz = 0; cz < this->level.getDepth(); cz++)
				if (this->level.cell(cx, cz).menu == only) std::printf("  menu %d em %d,%d (textura %d)\n", only, cx, cz, this->level.cell(cx, cz).wallTexture);
		return true;
	}

	if (name == "stairs" && this->loaded)
	{
		// stairs lista as saídas e descidas do andar (depuração).
		std::printf("andar %d em %.1f,%.1f:", this->dungeonLevel, this->x, this->z);
		for (int cx = 0; cx < this->level.getWidth(); cx++)
			for (int cz = 0; cz < this->level.getDepth(); cz++)
			{
				const Cell &c = this->level.cell(cx, cz);
				if (c.isExit || c.isLevelDown) std::printf(" %s(%d,%d)", c.isExit ? "sobe" : "desce", cx, cz);
			}
		std::printf("\n");
		return true;
	}

	if (name == "wildcount")
	{
		int alive = 0;
		std::string kinds;
		for (const Creature &c : this->creatures.getCreatures()) if (!c.gone && c.state != Creature::State::Dead) { alive++; kinds += std::to_string(c.definition) + " "; }
		std::printf("ermo: %d criaturas ativas (%s)\n", alive, kinds.c_str());
		return true;
	}

	if (name == "quest")
	{
		// quest:pedaços,passo ajusta a missão principal (vazio só mostra) e lista as masmorras conhecidas.
		CharacterDraft &character = this->game.getCharacter();
		int pieces = character.staffPieces, step = character.questStep;
		if (std::sscanf(arg.c_str(), "%d,%d", &pieces, &step) == 2) { character.staffPieces = pieces; character.questStep = step; }
		std::printf("missão: %d pedaço(s), passo %d, nível %d; conhecidas:", character.staffPieces, character.questStep, character.level);
		for (const CityLocation &c : this->game.getData().getCities())
			if (c.mainQuest() && quest::dungeonKnown(character, c)) std::printf(" %s", c.name.c_str());
		std::printf("\n");
		return true;
	}

	if (name == "castspell" && this->loaded)
	{
		// castspell:N aprende (se preciso) e conjura o feitiço N com magia cheia (teste).
		CharacterDraft &character = this->game.getCharacter();
		const int index = std::atoi(arg.c_str());
		if (std::find(character.spells.begin(), character.spells.end(), index) == character.spells.end()) character.spells.push_back(index);
		character.readySpell = index;
		this->spellPoints = std::max(this->spellPoints, 500);
		this->castReadySpell();
		return true;
	}

	if (name == "spells")
	{
		// spells lista os feitiços do SPELLSG.65 com alvo e efeitos (tipo/sub) (depuração).
		for (const Spell &spell : this->game.getData().getSpells().getSpells())
		{
			std::printf("%2d %-24s alvo %d:", spell.index, spell.originalName.c_str(), static_cast<int>(spell.target));
			for (const SpellEffect &e : spell.effects) if (e.valid()) std::printf(" %d/%d", e.type, e.sub);
			std::printf("\n");
		}
		return true;
	}

	if (name == "dlevel" && this->isDungeon())
	{
		// dlevel:+1 / dlevel:-1 troca de andar como pela escada (teste).
		this->changeDungeonLevel(std::atoi(arg.c_str()));
		return true;
	}

	if (name == "gopalace")
	{
		// gopalace entra no Palácio Imperial como a viagem com o Cajado completo (teste).
		this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, std::string("IN:MIFL:") + quest::PalaceMif + "@WILD:8:0@-1@-1@0@" + this->game.getText().get("Quest.PalaceName")));
		return true;
	}

	if (name == "godungeon")
	{
		// godungeon:provincia,local viaja direto para a masmorra (teste).
		int province = 0, local = 0;
		std::sscanf(arg.c_str(), "%d,%d", &province, &local);
		if (const CityLocation *d = this->game.getData().getCity(province, local)) this->travelToDungeon(*d, 0);
		return true;
	}

	if (name == "job")
	{
		// job mostra o trabalho atual; job:clear o desfaz e job:late vence o prazo (teste).
		CharacterDraft &character = this->game.getCharacter();
		if (arg == "clear") character.job = SideJob{};
		if (arg == "late") character.job.deadline = character.gameMinutes - 1.0;
		if (const int wanted = std::atoi(arg.c_str()); wanted > 0 && this->currentCity())
		{
			// job:N aceita um trabalho do tipo N oferecido nesta cidade (1 entrega .. 7 achado).
			for (uint32_t seed = 1; seed < 4000; seed++)
			{
				const auto offer = jobs::offer(this->game, *this->currentCity(), wanted <= 5, seed);
				if (offer && static_cast<int>(offer->kind) == wanted) { jobs::accept(this->game, *offer); break; }
			}
		}
		const SideJob &job = character.job;
		const CityLocation *target = jobs::place(this->game.getData(), job.target), *giver = jobs::place(this->game.getData(), job.giver);
		std::printf("trabalho: tipo %d, de %s para %s (%d,%d), passo %d, %d de ouro, prazo dia %d (hoje %d) | artefato procurado %d, achados %x\n",
			static_cast<int>(job.kind), giver ? giver->name.c_str() : "-", target ? target->name.c_str() : "-", target ? target->province : -1, target ? target->localId : -1,
			job.step, job.reward, clock::dayIndex(job.deadline), clock::dayIndex(character.gameMinutes), character.hunt.artifact, character.artifactsFound);
		return true;
	}

	if (name == "hunt")
	{
		// hunt:N marca o artefato N como procurado (teste) e mostra a masmorra dele.
		CharacterDraft &character = this->game.getCharacter();
		character.hunt.artifact = std::clamp(std::atoi(arg.c_str()), 0, artifact::Count - 1);
		const CityLocation *d = artifact::dungeon(this->game.getData(), character.hunt.artifact);
		std::printf("artefato %d em %s (%d,%d)\n", character.hunt.artifact, d ? d->name.c_str() : "-", d ? d->province : -1, d ? d->localId : -1);
		return true;
	}

	if (name == "artifact")
	{
		// artifact:N põe o artefato N no inventário, equipado (teste).
		CharacterDraft &character = this->game.getCharacter();
		Item item = artifact::makeItem(std::clamp(std::atoi(arg.c_str()), 0, artifact::Count - 1));
		item.equipped = true;
		character.inventory.push_back(item);
		artifact::syncSpells(character, this->game.getData().getItems());
		if (const int power = artifact::spellOf(item.artifact); power >= 0) character.readySpell = power;
		return true;
	}

	if (name == "armor")
	{
		// armor imprime as tabelas de armadura do executável (depuração).
		const ItemTables &t = this->game.getData().getItems();
		for (int c = 0; c < 3; c++) { std::printf("classe %d:", c); for (int i = 0; i < ArmorPieceCount; i++) std::printf(" %d", t.armorQuality[static_cast<size_t>(c)][static_cast<size_t>(i)]); std::printf("\n"); }
		std::printf("escudos:"); for (int v : t.shieldArmorClass) std::printf(" %d", v);
		std::printf("\nmateriais:"); for (int v : t.materialBonus) std::printf(" %d", v);
		std::printf("\npoções:"); for (int v : t.potionPrice) std::printf(" %d", v);
		for (int i = 0; i < MagicItemTypeCount; i++) std::printf("\nmágico %d: %s chance %d preço %d cargas %d..%d", i, t.magicItemNames[static_cast<size_t>(i)].c_str(), t.magicItemChance[static_cast<size_t>(i)], t.magicItemPrice[static_cast<size_t>(i)], t.magicItemCharges[static_cast<size_t>(i)].first, t.magicItemCharges[static_cast<size_t>(i)].second);
		std::printf("\nchances de encantamento: %d %d %d", t.enchantmentChance[0], t.enchantmentChance[1], t.enchantmentChance[2]);
		for (size_t g = 0; g < 2; g++) for (const auto &e : t.enchantments[g]) std::printf("\n  %s: %s -> feitiço %d, qualidade %d, +%d", g ? "armadura" : "arma", e.name.c_str(), e.spell, e.quality, e.bonusPrice);
		for (size_t g = 0; g < 3; g++) for (const auto &s : t.magicSpells[g]) std::printf("\n  grupo %zu: %s -> feitiço %d, qualidade %d, %d/carga", g, s.name.c_str(), s.spell, s.quality, s.pricePerCharge);
		std::printf("\n");
		return true;
	}

	if (name == "crime" && this->currentCity())
	{
		// crime marca o jogador como procurado nesta cidade, como ao matar um morador (teste).
		CharacterDraft &hero = this->game.getCharacter();
		hero.wanted[this->currentCity()->globalId] = hero.gameMinutes + WantedDays * clock::MinutesPerDay;
		std::printf("procurado em %s\n", this->currentCity()->name.c_str());
		return true;
	}

	if (name == "importoriginal")
	{
		// importoriginal:N importa o N-ésimo save do Arena original e começa por ele (teste).
		std::string error;
		if (!this->game.importOriginalSave(std::max(1, std::atoi(arg.c_str())), error)) { std::printf("importar: %s\n", error.c_str()); return false; }
		this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, this->game.startingMap()));
		return true;
	}

	if (name == "diseases")
	{
		// diseases lista as doenças do executável e quem as transmite (depuração).
		const auto &names = this->game.getData().getDiseaseNames();
		for (size_t i = 0; i < names.size(); i++) std::printf("doença %zu: %s%s\n", i, names[i].c_str(), this->game.getData().longDisease(static_cast<int>(i)) ? " (demorada)" : "");
		for (int id = 0; id < 40; id++) if (this->game.getData().longDisease(id)) std::printf("demorada: %d\n", id);
		for (int k = 0; const CreatureDefinition *def = this->game.getData().getCreature(k); k++)
		{
			std::printf("criatura %d %s: %d%%", k, def->originalName.c_str(), def->diseaseChance);
			for (int d : def->diseases) std::printf(" %d", d);
			std::printf("\n");
		}
		if (!arg.empty()) { this->game.getCharacter().disease = std::atoi(arg.c_str()); this->game.getCharacter().diseaseEnd = this->game.getCharacter().gameMinutes + 1440.0; }
		return true;
	}

	if (name == "enchanted")
	{
		// enchanted:K,T,E põe no inventário, equipada, uma arma (K 0) ou armadura (K 1) do tipo T com o encantamento E (teste).
		int kind = 0, type = 0, enchant = 0;
		if (std::sscanf(arg.c_str(), "%d,%d,%d", &kind, &type, &enchant) != 3) return false;
		Item item;
		item.kind = kind == 0 ? ItemKind::Weapon : ItemKind::Armor;
		item.type = type;
		item.armorClass = ArmorClass::Plate;
		item.enchant = enchant;
		CharacterDraft &character = this->game.getCharacter();
		for (Item &other : character.inventory)
			if (other.equipped && other.kind == item.kind && (item.kind == ItemKind::Weapon || bodyPartOf(other) == bodyPartOf(item))) other.equipped = false;
		item.equipped = true;
		character.inventory.push_back(item);
		artifact::syncSpells(character, this->game.getData().getItems());
		if (const int spell = enchantmentSpell(item, this->game.getData().getItems()); spell >= 0) character.readySpell = spell;
		this->setupWeapon();
		return true;
	}

	if (name == "gold")
	{
		// gold:N muda o ouro (teste).
		this->game.getCharacter().gold = std::max(0, std::atoi(arg.c_str()));
		return true;
	}

	if (name == "hp" && this->loaded)
	{
		// hp:N muda a vida atual (teste).
		this->health = std::max(1, std::atoi(arg.c_str()));
		return true;
	}

	if (name == "spawn" && this->loaded)
	{
		// spawn:N cria a criatura N a uns passos à frente, já atenta ao jogador (teste).
		const float fx = std::cos(this->yaw), fz = std::sin(this->yaw);
		for (float d = 4.0f; d >= 2.0f; d -= 0.5f)
		{
			const float sx = this->x + fx * d, sz = this->z + fz * d;
			if (this->level.blocksMovement(static_cast<int>(std::floor(sx)), static_cast<int>(std::floor(sz)))) continue;
			const int index = this->creatures.spawn(this->game.getAssets(), this->palette, std::atoi(arg.c_str()), sx, sz, this->game.getRandom());
			if (index >= 0) this->creatures.wake(index);
			return index >= 0;
		}
		return false;
	}

	if (name == "killjob" &&this->loaded && this->jobCreature >= 0 && this->creatures.isAlive(this->jobCreature))
	{
		// killjob mata a fera caçada como um golpe do jogador (teste).
		const int maxHp = this->creatures.getCreatures()[static_cast<size_t>(this->jobCreature)].maxHp;
		if (this->creatures.damage(this->jobCreature, maxHp + 1)) this->rewardKill(this->jobCreature, maxHp);
		return true;
	}

	if (name == "gotojob" && this->loaded && this->jobCreature >= 0)
	{
		// gotojob fica perto da fera caçada (teste).
		const Creature &beast = this->creatures.getCreatures()[static_cast<size_t>(this->jobCreature)];
		for (int d = 0; d < 8; d++)
		{
			const float angle = d * Pi / 4.0f;
			const float gx = beast.x + std::cos(angle) * 2.0f, gz = beast.z + std::sin(angle) * 2.0f;
			if (this->collides(gx, gz)) continue;
			char where[96];
			std::snprintf(where, sizeof(where), "%.2f,%.2f,%.1f", gx, gz, (angle + Pi) * 180.0f / Pi);
			return this->debugCommand("goto", where);
		}
		return false;
	}

	if (name == "gocity")
	{
		// gocity:provincia,local abre a cidade gerada (teste).
		int province = 0, local = 0;
		std::sscanf(arg.c_str(), "%d,%d", &province, &local);
		this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, "CITY:" + std::to_string(province) + ":" + std::to_string(local)));
		return true;
	}

	if (name == "saveslot" && this->loaded) return this->saveToSlot(std::max(1, std::atoi(arg.c_str())));
	if (name == "loadslot" && this->game.loadSavedCharacter(this->game.getSlotPath(std::max(1, std::atoi(arg.c_str())))))
	{
		this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, this->game.startingMap()));
		return true;
	}

	if (name == "gotocreature" && this->loaded)
	{
		// gotocreature:N fica a ~2,5 voxels da N-ésima criatura viva, olhando para ela.
		int wanted = std::atoi(arg.c_str());
		const auto &all = this->creatures.getCreatures();
		for (size_t i = 0; i < all.size(); i++)
		{
			if (!this->creatures.isAlive(static_cast<int>(i)) || wanted-- > 0) continue;
			for (int d = 0; d < 8; d++)
			{
				const float angle = d * Pi / 4.0f;
				const float gx = all[i].x + std::cos(angle) * 2.5f, gz = all[i].z + std::sin(angle) * 2.5f;
				if (this->collides(gx, gz)) continue;
				char where[96];
				std::snprintf(where, sizeof(where), "%.2f,%.2f,%.1f", gx, gz, (angle + Pi) * 180.0f / Pi);
				return this->debugCommand("goto", where);
			}
		}

		return false;
	}

	if (name == "gotowater" && this->loaded)
	{
		// gotowater:N fica na borda do N-ésimo fosso/água olhando para ele.
		int wanted = std::atoi(arg.c_str());
		const int dirX[4] = { 1, -1, 0, 0 }, dirZ[4] = { 0, 0, 1, -1 };
		for (int cx = 0; cx < this->level.getWidth(); cx++)
		{
			for (int cz = 0; cz < this->level.getDepth(); cz++)
			{
				if (!this->level.cell(cx, cz).chasm) continue;
				for (int d = 0; d < 4; d++)
				{
					const Cell &n = this->level.cell(cx + dirX[d], cz + dirZ[d]);
					if (n.chasm || n.type != CellType::Empty || wanted-- > 0) continue;
					const float gx = cx + 0.5f + dirX[d] * 1.3f, gz = cz + 0.5f + dirZ[d] * 1.3f;
					const float degrees = std::atan2(static_cast<float>(-dirZ[d]), static_cast<float>(-dirX[d])) * 180.0f / Pi;
					std::printf("gotowater: célula %d,%d\n", cx, cz);
					char where[96];
					std::snprintf(where, sizeof(where), "%.2f,%.2f,%.1f", gx, gz, degrees);
					return this->debugCommand("goto", where);
				}
			}
		}

		return false;
	}

	if (name == "gotoexit" && this->loaded)
	{
		// gotoexit fica diante do Portão de Transição, olhando para ele.
		const int dirX[4] = { 1, -1, 0, 0 }, dirZ[4] = { 0, 0, 1, -1 };
		for (int cx = 0; cx < this->level.getWidth(); cx++)
		{
			for (int cz = 0; cz < this->level.getDepth(); cz++)
			{
				if (!this->level.cell(cx, cz).isExit) continue;
				for (int d = 0; d < 4; d++)
				{
					if (this->level.cell(cx + dirX[d], cz + dirZ[d]).type != CellType::Empty) continue;
					const float gx = cx + 0.5f + dirX[d] * 1.0f, gz = cz + 0.5f + dirZ[d] * 1.0f;
					const float degrees = std::atan2(static_cast<float>(-dirZ[d]), static_cast<float>(-dirX[d])) * 180.0f / Pi;
					std::printf("gotoexit: célula %d,%d\n", cx, cz);
					char where[96];
					std::snprintf(where, sizeof(where), "%.2f,%.2f,%.1f", gx, gz, degrees);
					return this->debugCommand("goto", where);
				}
			}
		}

		return false;
	}

	return false;
}

std::string arena::GameWorldScreen::creatureRef(int creature, bool capitalized) const
{
	const int definition = this->creatures.getCreatures()[static_cast<size_t>(creature)].definition;
	std::string ref = this->game.getText().getOr("Creatures.Ref" + std::to_string(this->textIndexOf(definition)),
		this->creatures.definitionOf(creature).originalName);
	if (capitalized && !ref.empty() && ref[0] >= 'a' && ref[0] <= 'z') ref[0] = static_cast<char>(ref[0] - 'a' + 'A');
	return ref;
}

void arena::GameWorldScreen::startSwing()
{
	if (this->swinging || this->weapon.frames.empty() || this->paralysisTimer > 0.0)
	{
		return;
	}

	// Golpe conforme o movimento recente do mouse, como no original (arrastar = direção).
	static const std::vector<int> MeleeRight = { 15, 16, 17, 18, 19 }, MeleeLeft = { 10, 11, 12, 13, 14 },
		MeleeDown = { 0, 1, 2, 3, 4 }, MeleeDownRight = { 20, 21, 22, 23, 24 }, MeleeDownLeft = { 5, 6, 7, 8, 9 },
		MeleeForward = { 25, 26, 27, 28, 29 }, FistsA = { 0, 1, 2, 3, 4 }, FistsB = { 5, 6, 7, 8, 9 };
	const bool forward = this->heldKeys.contains('W') || this->holdUp;
	if (this->weapon.bow)
	{
		static const std::vector<int> Shot = { 1, 1, 1, 1, 0, 0, 0, 0 };
		this->swingFrames = Shot;
	}
	else if (this->weapon.fists)
	{
		this->swingFrames = (this->swingCycle++ % 2 == 0) ? FistsA : FistsB;
	}
	else if (this->recentMouseX > 40.0f) this->swingFrames = MeleeRight;
	else if (this->recentMouseX < -40.0f) this->swingFrames = MeleeLeft;
	else if (forward) this->swingFrames = MeleeForward;
	else
	{
		const std::vector<int> *cycle[] = { &MeleeDown, &MeleeDownRight, &MeleeDownLeft };
		this->swingFrames = *cycle[this->swingCycle++ % 3];
	}

	this->swinging = true;
	this->swingResolved = false;
	this->swingTime = 0.0;
	this->game.playSound(this->weapon.bow ? "ARROWFR.VOC" : "SWISH.VOC", 0.7f);
}

void arena::GameWorldScreen::resolveSwing()
{
	if (this->weapon.bow)
	{
		this->shootArrow();
		return;
	}

	const Localization &text = this->game.getText();
	const CharacterDraft &character = this->game.getCharacter();
	const int target = this->creatures.findTarget(this->x, this->z, this->yaw, 1.45f, 0.72f);
	if (target < 0)
	{
		this->tryAttackCitizen();
		return;
	}

	std::mt19937 &rng = this->game.getRandom();
	const CreatureDefinition &def = this->creatures.definitionOf(target);
	const int hitBonus = hitBonusFor(character.attributes[3] + this->fortifyBonus(3)) + this->weaponMaterialBonus / 5;
	const int chance = meleeHitChance256(character.level, character.raceIndex, hitBonus, def.level, -1, 0, 0);
	if (static_cast<int>(rng() % 256u) >= chance)
	{
		this->showMessage(ui::format(text.get("World.PlayerMiss"), { this->creatureRef(target, false) }), 2.0);
		return;
	}

	const int roll = this->weapon.minDamage + static_cast<int>(rng() % static_cast<uint32_t>(this->weapon.maxDamage - this->weapon.minDamage + 1));
	const int amount = std::max(1, roll + damageBonusFor(character.attributes[0] + this->fortifyBonus(0)));
	const Creature &victim = this->creatures.getCreatures()[static_cast<size_t>(target)];
	const int maxHp = victim.maxHp;
	this->playAt("EHIT.VOC", victim.x, victim.z);
	// Maça de Molag Bal: o dano causado volta como magia.
	const int stolen = std::min(amount, victim.hp) * artifact::powersOf(character).manaSteal / 100;
	this->spellPoints = std::min(character.maxSpellPoints, this->spellPoints + stolen);
	if (this->creatures.damage(target, amount))
	{
		this->rewardKill(target, maxHp);
	}
	else
	{
		this->showMessage(ui::format(text.get("World.PlayerHit"), { this->creatureRef(target, false), std::to_string(amount) }), 2.0);
	}
}

void arena::GameWorldScreen::handleCombatEvents(const std::vector<CombatEvent> &events)
{
	const Localization &text = this->game.getText();
	for (const CombatEvent &event : events)
	{
		switch (event.type)
		{
		case CombatEvent::Type::CreatureSound:
		case CombatEvent::Type::CreatureAttackStart:
		{
			const Creature &c = this->creatures.getCreatures()[static_cast<size_t>(event.creature)];
			const std::string &sound = this->creatures.definitionOf(event.creature).soundFile;
			if (!sound.empty() && (event.type == CombatEvent::Type::CreatureSound || this->game.getRandom()() % 3u == 0)) this->playAt(sound, c.x, c.z, 0.8f);
			break;
		}
		case CombatEvent::Type::CreatureHitPlayer:
		{
			const int amount = event.amount;
			if (this->shieldPoints > 0)
			{
				const int absorbed = std::min(this->shieldPoints, amount);
				this->shieldPoints -= absorbed;
				this->showMessage(text.get(this->shieldPoints > 0 ? "Spells.ShieldAbsorb" : "Spells.ShieldGone"), 2.5);
				if (absorbed >= amount) break;
				this->health = std::max(0, this->health - (amount - absorbed));
			}
			else
			{
				this->health = std::max(0, this->health - amount);
			}
			this->hurtTimer = 0.4;
			this->game.playSound(this->health > 0 ? "UHIT.VOC" : (this->game.getCharacter().male ? "MDIE.VOC" : "FDIE.VOC"));
			this->showMessage(ui::format(text.get("World.CreatureHit"), { this->creatureRef(event.creature, true), std::to_string(amount) }), 2.5);
			if (this->health > 0) this->creatureAfflicts(event.creature);
			if (this->health <= 0)
			{
				this->swinging = false;
				this->heldKeys.clear();
				this->showMessage(text.get("World.DeathMessage"), 1e9);
			}
			break;
		}
		case CombatEvent::Type::CreatureCast:
			this->creatureCasts(event.creature, event.amount);
			break;
		case CombatEvent::Type::CreatureMissedPlayer:
			this->showMessage(ui::format(text.get("World.CreatureMiss"), { this->creatureRef(event.creature, true) }), 2.0);
			break;
		case CombatEvent::Type::CreatureNoticed:
		{
			if (this->messageTimer < 1.0) this->showMessage(ui::format(text.get("World.CreatureNoticed"), { this->creatureRef(event.creature, true) }), 2.0);
			const Creature &c = this->creatures.getCreatures()[static_cast<size_t>(event.creature)];
			const std::string &sound = this->creatures.definitionOf(event.creature).soundFile;
			if (!sound.empty()) this->playAt(sound, c.x, c.z, 0.9f);
			break;
		}
		}
	}
}

void arena::GameWorldScreen::showMessage(const std::string &text, double seconds)
{
	this->message = text;
	this->messageTimer = seconds;
}

bool arena::GameWorldScreen::collides(float px, float pz) const
{
	const int minX = static_cast<int>(std::floor(px - PlayerRadius));
	const int maxX = static_cast<int>(std::floor(px + PlayerRadius));
	const int minZ = static_cast<int>(std::floor(pz - PlayerRadius));
	const int maxZ = static_cast<int>(std::floor(pz + PlayerRadius));
	for (int cx = minX; cx <= maxX; cx++)
	{
		for (int cz = minZ; cz <= maxZ; cz++)
		{
			if (this->level.blocksMovement(cx, cz)) return true;
		}
	}

	for (const Sprite &sprite : this->level.getSprites())
	{
		if (!sprite.collider) continue;
		const float dx = px - sprite.x, dz = pz - sprite.z;
		if (dx * dx + dz * dz < (PlayerRadius + 0.25f) * (PlayerRadius + 0.25f)) return true;
	}

	return this->creatures.blocks(px, pz, PlayerRadius) || this->citizens.blocks(px, pz, PlayerRadius);
}

void arena::GameWorldScreen::movePlayer(float dx, float dz)
{
	// Encostar numa porta de prédio (ou na saída do interior) atravessa, como no original.
	if (this->level.isExterior() || !this->returnMap.empty())
	{
		const int cx = static_cast<int>(std::floor(this->x + dx * 4.0f + (dx > 0 ? PlayerRadius : dx < 0 ? -PlayerRadius : 0.0f)));
		const int cz = static_cast<int>(std::floor(this->z + dz * 4.0f + (dz > 0 ? PlayerRadius : dz < 0 ? -PlayerRadius : 0.0f)));
		const Cell &c = this->level.cell(cx, cz);
		if (c.menu >= 0 && (c.type == CellType::Wall || c.type == CellType::Edge) && (std::fabs(dx) + std::fabs(dz)) > 0.0f)
		{
			if (!this->returnMap.empty()) { this->leaveBuilding(); return; }
			if (this->enterBuilding(cx, cz, c)) return;
		}
	}

	// Eixos separados para deslizar ao longo das paredes.
	if (!this->collides(this->x + dx, this->z)) this->x += dx;
	if (!this->collides(this->x, this->z + dz)) this->z += dz;
}

void arena::GameWorldScreen::interact()
{
	const Localization &text = this->game.getText();
	const float fx = std::cos(this->yaw), fz = std::sin(this->yaw);

	// Itens (chave) logo à frente.
	const auto &sprites = this->level.getSprites();
	for (size_t i = 0; i < sprites.size(); i++)
	{
		const float dx = sprites[i].x - this->x, dz = sprites[i].z - this->z;
		const float along = dx * fx + dz * fz;
		const float side = std::fabs(dx * fz - dz * fx);
		if (along > 0.0f && along < 1.4f && side < 0.45f && sprites[i].name == "KEY.IMG")
		{
			this->removeSpriteAt(i);
			this->showMessage(text.get("World.KeyFound"));
			return;
		}

		if (along > 0.0f && along < 1.9f && side < 0.6f && sprites[i].itemId == 13 && this->tryTakeQuestItem(i)) return;
		if (along > 0.0f && along < 1.9f && side < 0.6f && (sprites[i].itemId == JobTargetItem || sprites[i].itemId == ArtifactItem) && this->tryTakeDungeonTarget(i)) return;
	}

	if (this->tryTalkToClerk() || this->tryTalkToCitizen()) return;

	if (this->tryOpenLoot())
	{
		this->heldKeys.clear();
		this->holdLeft = this->holdRight = this->holdUp = this->holdDown = false;
		return;
	}

	for (float d = 0.1f; d < 1.6f; d += 0.05f)
	{
		const int cx = static_cast<int>(std::floor(this->x + fx * d));
		const int cz = static_cast<int>(std::floor(this->z + fz * d));
		const Cell &c = this->level.cell(cx, cz);
		if (c.type == CellType::Door)
		{
			const int lock = this->level.lockLevel(cx, cz);
			bool unlockedNow = false;
			if (lock > 0)
			{
				const CharacterDraft &character = this->game.getCharacter();
				const ClassDefinition &def = this->game.getData().getClass(std::max(0, character.classIndex));
				if (this->keys.contains(lock) || artifact::powersOf(character).skeletonKey)
				{
					// Com a chave (ou a Chave do Esqueleto, que abre qualquer porta): destranca e já abre.
					this->level.unlock(cx, cz);
					this->game.playSound("LOCK.VOC");
					unlockedNow = true;
				}
				else if (def.isThief)
				{
					// Arrombar: (Int + Agi) / divisor de furto da classe, contra a dificuldade da trava.
					const int ability = (character.attributes[1] + character.attributes[3]) / std::max(1, def.thievingDivisor) * 2;
					const int chance = std::clamp(ability / std::max(1, lock / 4), 5, 95);
					if (static_cast<int>(this->game.getRandom()() % 100u) < chance)
					{
						this->level.unlock(cx, cz);
						this->game.playSound("LOCK.VOC");
						this->showMessage(text.get("World.LockPicked"), 3.0);
					}
					else
					{
						this->game.playSound("CLICKS.VOC");
						this->showMessage(text.get("World.LockPickFailed"), 2.5);
					}
					return;
				}
				else
				{
					this->game.playSound("CLANK.VOC");
					this->showMessage(text.get("World.DoorLocked"), 3.0);
					return;
				}
			}

			const bool opening = this->level.doorOpenness(cx, cz) < 0.5f;
			this->level.toggleDoor(cx, cz);
			this->playAt(opening ? "OPENDOOR.VOC" : "CLOSDOOR.VOC", cx + 0.5f, cz + 0.5f);
			this->showMessage(text.get(unlockedNow ? "World.DoorUnlocked" : opening ? "World.DoorOpens" : "World.DoorCloses"), unlockedNow ? 3.5 : 2.0);
			return;
		}

		if (c.menu >= 0 && (c.type == CellType::Wall || c.type == CellType::Edge))
		{
			if (!this->returnMap.empty()) this->leaveBuilding();
			else if (this->level.isExterior()) this->enterBuilding(cx, cz, c);
			return;
		}

		if (c.type == CellType::Wall && this->isDungeon() && (c.isExit || c.isLevelDown))
		{
			if (c.isLevelDown) this->changeDungeonLevel(+1);
			else if (this->dungeonLevel > 0) this->changeDungeonLevel(-1);
			else this->leaveBuilding();
			return;
		}

		if (c.type == CellType::Wall)
		{
			if (c.isExit && this->isPrison() && this->gateTimer <= 0.0 && !this->chapterOpen)
			{
				// Portão de Transição: clarão de passagem e fim do capítulo da prisão.
				this->game.playSound("BURST5.VOC");
				this->game.playSound("THUNDER.VOC");
				this->gateTimer = GateDuration;
				this->heldKeys.clear();
			}
			return;
		}
	}
}

void arena::GameWorldScreen::handleInput(const InputEvent &event)
{
	if (!this->loaded)
	{
		if (event.type == InputEvent::Type::KeyDown || event.isClick())
		{
			this->game.changeScreen(std::make_unique<MainMenuScreen>(this->game));
		}

		return;
	}

	const bool down = event.type == InputEvent::Type::KeyDown;
	if (event.type == InputEvent::Type::KeyDown || event.type == InputEvent::Type::KeyUp)
	{
		switch (event.key)
		{
		case InputEvent::Key::Left: this->holdLeft = down; break;
		case InputEvent::Key::Right: this->holdRight = down; break;
		case InputEvent::Key::Up: this->holdUp = down; break;
		case InputEvent::Key::Down: this->holdDown = down; break;
		case InputEvent::Key::Shift: this->holdShift = down; break;
		case InputEvent::Key::Character:
			if (down) this->heldKeys.insert(event.character);
			else this->heldKeys.erase(event.character);
			break;
		default: break;
		}
	}

	if (this->cutscene.active())
	{
		if (event.isKey(InputEvent::Key::Escape)) this->cutscene.stop();
		else if (event.type == InputEvent::Type::KeyDown || event.isClick()) this->cutscene.skip();
		this->heldKeys.clear();
		this->holdLeft = this->holdRight = this->holdUp = this->holdDown = false;
		return;
	}

	// Mouse sobre os botões de pausa/morte (interface em resolução nativa).
	eventToPixels(event, this->uiFrameW, this->uiFrameH, this->uiMouseX, this->uiMouseY);
	if (this->gateTimer > 0.0) return;
	if (!this->visionText.empty() && this->overlay == Overlay::None)
	{
		if (this->offerPending() && (event.isKey(InputEvent::Key::Enter) || event.isKey(InputEvent::Key::Escape))) { this->screenButtonAction(event.isKey(InputEvent::Key::Enter) ? 'y' : 'n'); return; }
		if (event.isKey(InputEvent::Key::Enter) || event.isKey(InputEvent::Key::Escape)) { this->screenButtonAction('v'); return; }
		if (!event.isClick()) return;
	}
	if (this->chapterOpen && this->overlay == Overlay::None)
	{
		if (event.isKey(InputEvent::Key::Enter)) { this->screenButtonAction('t'); return; }
		if (event.isKey(InputEvent::Key::Escape)) { this->screenButtonAction('x'); return; }
		if (!event.isClick()) return;
	}

	if ((this->paused || this->health <= 0 || this->chapterOpen || !this->visionText.empty()) && this->overlay == Overlay::None && event.isClick())
	{
		for (const auto &[rect, action] : this->screenButtons)
		{
			if (kit::hit(rect, this->uiMouseX, this->uiMouseY))
			{
				this->screenButtonAction(action);
				return;
			}
		}
	}

	if (this->health <= 0)
	{
		// Morto: recomeça na entrada da prisão ou volta ao menu.
		if (event.isKey(InputEvent::Key::Enter)) this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, this->game.getText().get("World.StartDungeonMif")));
		else if (event.isHotkey('S') || event.isKey(InputEvent::Key::Escape)) this->game.changeScreen(std::make_unique<MainMenuScreen>(this->game));
		return;
	}

	if (this->overlay != Overlay::None)
	{
		if (event.type == InputEvent::Type::KeyUp) return;
		if (this->overlay == Overlay::Travel)
		{
			const TravelMenu::Choice choice = this->travelMenu.handleInput(event, this->game);
			if (choice.destination && choice.destination->type == CityLocation::Type::Dungeon)
			{
				this->travelToDungeon(*choice.destination, choice.days);
				this->overlay = Overlay::None;
				return;
			}

			const CharacterDraft &hero = this->game.getCharacter();
			if (choice.destination && choice.destination->province == CenterProvinceID && hero.staffPieces >= quest::PieceCount && hero.questStep >= 1 && hero.questStep < 4)
			{
				// Com o Cajado completo, a viagem à Cidade Imperial leva direto ao Palácio (IMPPAL.MIF).
				this->persist();
				this->game.getCharacter().gameMinutes += choice.days * clock::MinutesPerDay;
				this->game.setPendingNote(this->game.getText().get("Quest.PalaceArrived"));
				this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, std::string("IN:MIFL:") + quest::PalaceMif + "@WILD:" + std::to_string(choice.destination->province) + ":"
					+ std::to_string(choice.destination->localId) + "@-1@-1@0@" + this->game.getText().get("Quest.PalaceName")));
				return;
			}

			if (choice.destination)
			{
				// Os dias passam na viagem; chega ao portão da cidade de destino (pelo ermo dela).
				this->persist();
				this->game.getCharacter().gameMinutes += choice.days * clock::MinutesPerDay;
				this->game.setPendingNote(ui::format(this->game.getText().get(choice.days == 1 ? "Travel.ArrivedDay" : "Travel.Arrived"), choice.days == 1 ? std::initializer_list<std::string>{ choice.destination->name } : std::initializer_list<std::string>{ std::to_string(choice.days), choice.destination->name }));
				this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, "WILD:" + std::to_string(choice.destination->province) + ":" + std::to_string(choice.destination->localId)));
			}
			if (choice.close) this->overlay = Overlay::None;
			return;
		}

		if (this->overlay == Overlay::Journal)
		{
			if (this->journalMenu.handleInput(event, this->game).close) this->overlay = Overlay::None;
			return;
		}

		if (this->overlay == Overlay::Dialogue)
		{
			const OverlayResult result = this->dialogueMenu.handleInput(event, this->game);
			if (this->dialogueMenu.questRumorHeard && this->game.getCharacter().questStep == 1)
			{
				this->game.getCharacter().questStep = 2; // a masmorra da pista aparece no mapa de viagem
				if (const CityLocation *here = this->currentCity()) this->game.getCharacter().questGiver = here->globalId;
				const int questProvince = quest::targetProvince(this->game.getCharacter());
				const CityLocation *piece = this->game.getData().getCity(questProvince, quest::PieceDungeon), *clue = this->game.getData().getCity(questProvince, quest::ClueDungeon);
				if (piece && clue) quest::note(this->game.getCharacter(), ui::format(this->game.getText().get("Journal.Rumor"), { this->locationName, this->game.getText().get("Quest.Item" + std::to_string(this->game.getCharacter().staffPieces)), clue->name }));
				this->persist();
			}
			if (result.close) this->overlay = Overlay::None;
			return;
		}

		if (this->overlay == Overlay::Service)
		{
			const OverlayResult result = this->serviceMenu.handleInput(event, this->game);
			if (result.rested)
			{
				// Noite no quarto: vida e magia cheias.
				const CharacterDraft &character = this->game.getCharacter();
				this->health = character.maxHealth;
				this->spellPoints = character.maxSpellPoints;
				this->game.getCharacter().gameMinutes += clock::untilMorning(character.gameMinutes);
			}

			if (result.close) this->overlay = Overlay::None;
			this->applyOverlayResult(result);
			if (!result.inventoryChanged && (result.rested || result.healthDelta != 0)) this->persist();
			if (result.rested) this->tryDream();
			if (result.request == 1) this->offerJob(false);
			else if (result.request == 2) this->offerArtifact();
			return;
		}

		if (this->overlay == Overlay::SaveLoad)
		{
			const SaveLoadMenu::Choice choice = this->saveLoadMenu.handleInput(event, this->game);
			if (choice.slot > 0 && this->saveLoadMenu.getMode() == SaveLoadMenu::Mode::Save)
			{
				this->saveToSlot(choice.slot);
				this->overlay = Overlay::None;
			}
			else if (choice.slot > 0 && choice.original && !this->game.importOriginalSave(choice.slot, this->loadError))
			{
				this->showMessage(this->loadError, 4.0);
				this->loadError.clear();
			}
			else if (choice.slot > 0 && (choice.original || this->game.loadSavedCharacter(this->game.getSlotPath(choice.slot))))
			{
				this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, this->game.startingMap()));
			}
			else if (choice.close)
			{
				this->overlay = Overlay::None;
				if (this->health > 0) this->paused = true;
			}
			return;
		}

		const OverlayResult result = (this->overlay == Overlay::Character) ? this->characterMenu.handleInput(event, this->game, this->menuState())
			: (this->overlay == Overlay::LevelUp) ? this->levelUpMenu.handleInput(event, this->game)
			: this->lootMenu.handleInput(event, this->game);
		this->applyOverlayResult(result);
		return;
	}

	if (this->paused)
	{
		if (event.isKey(InputEvent::Key::Escape)) this->paused = false;
		else if (event.isHotkey('C')) this->screenButtonAction('p');
		else if (event.isHotkey('B')) this->screenButtonAction('b');
		else if (event.isHotkey('H')) this->screenButtonAction('h');
		else if (event.isHotkey('S')) this->screenButtonAction('m');
		return;
	}

	if (event.isKey(InputEvent::Key::Escape))
	{
		this->paused = true;
		this->heldKeys.clear();
		this->holdLeft = this->holdRight = this->holdUp = this->holdDown = false;
	}
	else if (event.isClick() || event.isHotkey('F'))
	{
		// Clique (e F) só ataca; usar portas, conversar e saquear fica com o E.
		this->startSwing();
	}
	else if (event.isHotkey('E') || event.isKey(InputEvent::Key::Enter))
	{
		this->interact();
	}
	else if (event.isHotkey('C'))
	{
		this->openCharacterMenu(CharacterMenu::Tab::Character);
	}
	else if (event.isHotkey('I'))
	{
		this->openCharacterMenu(CharacterMenu::Tab::Inventory);
	}
	else if (event.isHotkey('Q'))
	{
		this->castReadySpell();
	}
	else if (event.isHotkey('B'))
	{
		this->openCharacterMenu(CharacterMenu::Tab::Spells);
	}
	else if (event.isHotkey('T'))
	{
		// Viagem: das cidades e do ermo (das masmorras e prédios, só saindo antes).
		const CityLocation *city = this->currentCity();
		if (!city || !this->level.isExterior()) this->showMessage(this->game.getText().get(city ? "Travel.GoOutside" : "Travel.NotHere"), 3.0);
		else
		{
			this->travelMenu.open(this->game, city, clock::month(this->game.getCharacter().gameMinutes));
			this->overlay = Overlay::Travel;
			this->heldKeys.clear();
			this->holdLeft = this->holdRight = this->holdUp = this->holdDown = false;
		}
	}
	else if (event.isHotkey('M'))
	{
		this->showMinimap = !this->showMinimap;
	}
	else if (event.isHotkey('R'))
	{
		this->rest();
	}
	else if (event.isHotkey('J'))
	{
		this->openJournal();
	}
	else if (event.isHotkey('H'))
	{
		this->helpTimer = (this->helpTimer > 0.0) ? 0.0 : 1e9;
	}
	else if (event.type == InputEvent::Type::MouseMove)
	{
		this->yaw += event.dx * MouseSensitivity;
		this->pitch = std::clamp(this->pitch - event.dy * MouseSensitivity, -1.2f, 1.2f);
		this->recentMouseX += event.dx;
	}
}

void arena::GameWorldScreen::update(double dt)
{
	if (this->cutscene.active())
	{
		this->cutscene.update(dt);
		return;
	}

	this->time += dt;
	this->messageTimer -= dt;
	this->helpTimer -= dt;
	this->hurtTimer -= dt;
	if (this->gateTimer > 0.0)
	{
		this->gateTimer -= dt;
		if (this->gateTimer <= 0.0)
		{
			this->escaped = true;
			quest::note(this->game.getCharacter(), this->game.getText().get("Journal.Escaped"));
			this->chapterOpen = true;
			this->nextCity = static_cast<int>(this->game.getRandom()() % 32u);
			this->game.playSound("FANFARE2.VOC");
			this->persist();
		}
		return;
	}

	if (!this->loaded || this->paused || this->overlay != Overlay::None || this->chapterOpen || !this->visionText.empty())
	{
		return;
	}

	this->game.getCharacter().gameMinutes += dt * clock::GameMinutesPerRealSecond;

	// Salvamento automático: a cada minuto explorando (o "Continuar" do menu volta a este ponto).
	this->autosaveTimer -= dt;
	if (this->autosaveTimer <= 0.0 && this->health > 0)
	{
		this->autosaveTimer = 60.0;
		this->persist();
	}
	this->updateDayNight(false);

	this->level.update(dt);
	const float fdt = static_cast<float>(dt);

	// Combate: golpe do jogador (5 quadros a 16 fps; acerto no meio do movimento).
	this->recentMouseX *= static_cast<float>(std::pow(0.02, dt));
	if (this->swinging)
	{
		this->swingTime += dt;
		const double length = this->swingFrames.size() / 16.0;
		if (!this->swingResolved && this->swingTime >= length * 0.45)
		{
			this->swingResolved = true;
			this->resolveSwing();
		}

		if (this->swingTime >= length) this->swinging = false;
	}

	std::vector<CombatEvent> events;
	const PlayerCombatStats stats = this->combatStats();
	this->creatures.update(dt, this->level, stats, this->game.getRandom(), events);
	this->citizens.update(dt, this->level, this->x, this->z, this->game.getRandom());
	if (this->isWild()) { this->updateWildCreatures(dt); this->shiftWilderness(); }
	this->handleCombatEvents(events);
	this->updateMagic(dt);
	this->updateArrows(dt);

	this->checkLevelUp();
	if (this->health <= 0)
	{
		return;
	}
	this->updateDisease(dt);
	if (this->guardsOnArrival)
	{
		this->guardsOnArrival = false;
		this->callGuards();
		this->showMessage(this->game.getText().get("World.Wanted"), 4.0);
	}
	if (this->paralysisTimer > 0.0)
	{
		// Paralisado: só dá para olhar em volta até passar.
		this->paralysisTimer -= dt;
		if (this->paralysisTimer <= 0.0) this->showMessage(this->game.getText().get("World.ParalysisGone"), 2.5);
		return;
	}
	if (this->holdLeft) this->yaw -= TurnSpeed * fdt;
	if (this->holdRight) this->yaw += TurnSpeed * fdt;

	float forwardInput = 0.0f, strafeInput = 0.0f;
	if (this->heldKeys.contains('W') || this->holdUp) forwardInput += 1.0f;
	if (this->heldKeys.contains('S') || this->holdDown) forwardInput -= 1.0f;
	if (this->heldKeys.contains('D')) strafeInput += 1.0f;
	if (this->heldKeys.contains('A')) strafeInput -= 1.0f;

	const float length = std::sqrt(forwardInput * forwardInput + strafeInput * strafeInput);
	if (length > 0.0f)
	{
		const float speed = (this->holdShift ? RunSpeed : WalkSpeed) * fdt / length;
		const float fx = std::cos(this->yaw), fz = std::sin(this->yaw);
		// Direita = forward x up = (-fz, fx) no plano XZ.
		const float rx = -fz, rz = fx;
		const float oldX = this->x, oldZ = this->z;
		this->movePlayer((fx * forwardInput + rx * strafeInput) * speed, (fz * forwardInput + rz * strafeInput) * speed);
		this->bob += dt * (this->holdShift ? 11.0 : 8.0);

		// Passos alternados (ou água) conforme a distância percorrida.
		this->stepDistance += std::sqrt((this->x - oldX) * (this->x - oldX) + (this->z - oldZ) * (this->z - oldZ));
		if (this->stepDistance > 0.65f)
		{
			this->stepDistance = 0.0f;
			this->leftFoot = !this->leftFoot;
			const bool inWater = this->level.cell(static_cast<int>(std::floor(this->x)), static_cast<int>(std::floor(this->z))).chasm && !this->hasPlayerEffect(SpellEffectType::Levitate);
			this->game.playSound(inWater ? "SWIMMING.VOC" : (this->leftFoot ? "DIRTL.VOC" : "DIRTR.VOC"), inWater ? 0.5f : 0.35f);
		}
	}

	// Entrou numa nova célula: gatilhos de texto/som.
	const int cellX = static_cast<int>(std::floor(this->x));
	const int cellZ = static_cast<int>(std::floor(this->z));
	if (cellX != this->lastCellX || cellZ != this->lastCellZ)
	{
		if (this->level.cell(cellX, cellZ).chasm && !this->level.cell(this->lastCellX, this->lastCellZ).chasm && !this->hasPlayerEffect(SpellEffectType::Levitate)) this->game.playSound("SPLASH.VOC", 0.7f);
		this->lastCellX = cellX;
		this->lastCellZ = cellZ;
		this->checkTriggers(cellX, cellZ);
	}

	// Suaviza a altura dos olhos ao subir em plataformas ou entrar na água.
	const float floorTarget = this->level.floorHeightAt(static_cast<int>(std::floor(this->x)), static_cast<int>(std::floor(this->z)));
	const float target = this->hasPlayerEffect(SpellEffectType::Levitate) ? std::max(floorTarget, 0.15f) : floorTarget;
	this->eyeOffset += (target - this->eyeOffset) * std::min(1.0f, fdt * 10.0f);
}

void arena::GameWorldScreen::render(Canvas &canvas)
{
	canvas.clear(0);
}

void arena::GameWorldScreen::drawLevel()
{
	const auto &textures = this->level.getTextures();
	const float h = this->level.getWallHeight();
	const Vec3 forward = this->renderer.getForward();
	const Vec3 right = this->renderer.getRight();

	auto tex = [&textures](int id) -> const Texture & { return textures[static_cast<size_t>(std::max(0, id))]; };

	// Face vertical entre (ax,az) e (bx,bz); a ordem é ajustada para a normal desejada.
	auto isWall = [this](int cx, int cz) { return this->level.cell(cx, cz).type == CellType::Wall; };

	// Face vertical entre (ax,az) e (bx,bz); a ordem é ajustada para a normal desejada.
	// wallCorners: escurece as pontas que encostam em outra parede (cantos internos).
	auto wallFace = [&](float ax, float az, float bx, float bz, float nx, float nz, float yb, float yt, int texture,
		float vTop, float vBottom, bool doubleSided, Surface surface = Surface{}, bool wallCorners = false)
	{
		// Direita vista de fora = (nz, -nx).
		if ((bx - ax) * nz + (bz - az) * -nx < 0.0f)
		{
			std::swap(ax, bx);
			std::swap(az, bz);
		}

		if (wallCorners)
		{
			const float ox = (ax + bx) * 0.5f + nx * 0.5f, oz = (az + bz) * 0.5f + nz * 0.5f;
			const float tx = bx - ax, tz = bz - az;
			if (isWall(static_cast<int>(std::floor(ox - tx)), static_cast<int>(std::floor(oz - tz)))) surface.aoMask |= 1;
			if (isWall(static_cast<int>(std::floor(ox + tx)), static_cast<int>(std::floor(oz + tz)))) surface.aoMask |= 2;
		}

		const Vec3 v[4] = { { ax, yt, az }, { bx, yt, bz }, { bx, yb, bz }, { ax, yb, az } };
		const float uv[4][2] = { { 0, vTop }, { 1, vTop }, { 1, vBottom }, { 0, vBottom } };
		this->renderer.drawQuad(v, uv, tex(texture), doubleSided, surface);
	};

	auto flat = [&](float x0, float z0, float y, int texture, Surface surface = Surface{})
	{
		const Vec3 v[4] = { { x0, y, z0 }, { x0 + 1, y, z0 }, { x0 + 1, y, z0 + 1 }, { x0, y, z0 + 1 } };
		const float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
		this->renderer.drawQuad(v, uv, tex(texture), true, surface);
	};

	// Paredes comuns: escurecem junto ao chão, ao teto e nos cantos.
	Surface wallSurface;
	wallSurface.aoFloorY = 0.0f;
	wallSurface.aoCeilingY = h;

	const int px = static_cast<int>(std::floor(this->x));
	const int pz = static_cast<int>(std::floor(this->z));
	const int dirX[4] = { 1, -1, 0, 0 };
	const int dirZ[4] = { 0, 0, 1, -1 };
	const bool exterior = this->level.isExterior();
	const bool nightLights = clock::isNight(this->game.getCharacter().gameMinutes);
	const int radius = exterior ? ExteriorDrawRadius : DrawRadius;
	if (exterior) this->drawSky();

	// Altura total de uma célula: térreo + andares do MAP2 (exteriores).
	auto topOf = [&](const Cell &cell) { return (cell.type == CellType::Wall || cell.upperStories > 0) ? h * (1 + cell.upperStories) : 0.0f; };

	for (int cx = px - radius; cx <= px + radius; cx++)
	{
		for (int cz = pz - radius; cz <= pz + radius; cz++)
		{
			if (!this->level.inBounds(cx, cz)) continue;

			// Descarta células claramente atrás da câmera.
			const float ox = cx + 0.5f - this->x, oz = cz + 0.5f - this->z;
			if (ox * forward.x + oz * forward.z < -1.5f && ox * ox + oz * oz > 2.0f) continue;

			const Cell &c = this->level.cell(cx, cz);
			const float x0 = static_cast<float>(cx), z0 = static_cast<float>(cz);

			// Andares de cima (MAP2): faces onde o vizinho é mais baixo.
			if (c.upperStories > 0)
			{
				const float top = h * (1 + c.upperStories);
				for (int d = 0; d < 4; d++)
				{
					const float neighborTop = std::max(h, topOf(this->level.cell(cx + dirX[d], cz + dirZ[d])));
					if (neighborTop >= top) continue;
					const float ax = (d == 0) ? x0 + 1 : x0, az = (d == 2) ? z0 + 1 : z0;
					const float bx = (d == 0 || d == 1) ? ax : x0 + 1, bz = (d == 2 || d == 3) ? az : z0 + 1;
					for (int s = 0; s < c.upperStories; s++)
					{
						const float yb = h * (1 + s), yt = yb + h;
						if (yt <= neighborTop) continue;
						wallFace(ax, az, bx, bz, static_cast<float>(dirX[d]), static_cast<float>(dirZ[d]), yb, yt, c.upperTexture, 0.0f, 1.0f, false);
					}
				}
			}

			if (c.type == CellType::Wall)
			{
				for (int d = 0; d < 4; d++)
				{
					if (this->level.cell(cx + dirX[d], cz + dirZ[d]).type == CellType::Wall) continue;
					const float nx = static_cast<float>(dirX[d]), nz = static_cast<float>(dirZ[d]);
					// Aresta do lado d do bloco.
					const float ax = (d == 0) ? x0 + 1 : (d == 1) ? x0 : x0;
					const float az = (d == 2) ? z0 + 1 : (d == 3) ? z0 : z0;
					const float bx = (d == 0 || d == 1) ? ax : x0 + 1;
					const float bz = (d == 2 || d == 3) ? az : z0 + 1;
					wallFace(ax, az, bx, bz, nx, nz, 0.0f, h, c.wallTexture, 0.0f, 1.0f, false, wallSurface, true);
				}

				continue;
			}

			// Bordas de piso e teto junto a paredes (bits: u=0 -X, u=1 +X, v=0 -Z, v=1 +Z).
			Surface flatSurface;
			if (isWall(cx - 1, cz)) flatSurface.aoMask |= 1;
			if (isWall(cx + 1, cz)) flatSurface.aoMask |= 2;
			if (isWall(cx, cz - 1)) flatSurface.aoMask |= 4;
			if (isWall(cx, cz + 1)) flatSurface.aoMask |= 8;

			// Chão (ou água/lava/fosso) e teto.
			if (c.chasm)
			{
				// Margem de pedra (piso vizinho) descendo até o fundo, voltada para dentro da célula.
				int rimTexture = -1;
				for (int d = 0; d < 4; d++)
				{
					const Cell &n = this->level.cell(cx + dirX[d], cz + dirZ[d]);
					if (n.chasm) continue;
					const bool againstWall = n.type == CellType::Wall;
					if (!againstWall) rimTexture = n.floorTexture;
					const float ax = (d == 0) ? x0 + 1 : x0, az = (d == 2) ? z0 + 1 : z0;
					const float bx = (d == 0 || d == 1) ? ax : x0 + 1, bz = (d == 2 || d == 3) ? az : z0 + 1;
					Surface rim;
					rim.aoFloorY = -ChasmDepth;
					rim.aoCeilingY = 0.15f;
					// Contra uma parede, a própria parede continua para baixo do chão.
					wallFace(ax, az, bx, bz, -static_cast<float>(dirX[d]), -static_cast<float>(dirZ[d]), -ChasmDepth, 0.0f,
						againstWall ? n.wallTexture : n.floorTexture, againstWall ? 1.0f : 0.0f, againstWall ? 1.0f + ChasmDepth / h : ChasmDepth, false, rim);
				}

				const std::vector<int> &frames = this->level.getLiquidFrames(c.chasmKind);
				if (c.chasmKind != ChasmKind::Dry && !frames.empty())
				{
					// Animação original (5 quadros) com ondas e reflexos.
					Surface liquid;
					liquid.kind = (c.chasmKind == ChasmKind::Lava) ? Surface::Kind::Lava : Surface::Kind::Water;
					const int frame = frames[static_cast<size_t>(this->time * 6.0) % frames.size()];
					flat(x0, z0, -LiquidLevel, frame, liquid);
				}
				else
				{
					Surface pit = flatSurface;
					pit.aoMask = 15;
					flat(x0, z0, -ChasmDepth, rimTexture >= 0 ? rimTexture : c.chasmTexture, pit);
				}
			}
			else
			{
				flat(x0, z0, 0.0f, c.floorTexture, flatSurface);
			}

			if (!exterior) flat(x0, z0, h, this->level.getCeilingTexture(), flatSurface);

			switch (c.type)
			{
			case CellType::Door:
			{
				const float open = this->level.doorOpenness(cx, cz);
				if (open >= 0.99f) break;
				const float yb = open * h;
				for (int d = 0; d < 4; d++)
				{
					if (this->level.cell(cx + dirX[d], cz + dirZ[d]).type == CellType::Wall) continue;
					const float ax = (d == 0) ? x0 + 1 : x0, az = (d == 2) ? z0 + 1 : z0;
					const float bx = (d == 0 || d == 1) ? ax : x0 + 1, bz = (d == 2 || d == 3) ? az : z0 + 1;
					wallFace(ax, az, bx, bz, static_cast<float>(dirX[d]), static_cast<float>(dirZ[d]), yb, h, c.wallTexture,
						0.0f, 1.0f - open, false);
				}
				break;
			}
			case CellType::TransparentWall:
				for (int d = 0; d < 4; d++)
				{
					const float ax = (d == 0) ? x0 + 1 : x0, az = (d == 2) ? z0 + 1 : z0;
					const float bx = (d == 0 || d == 1) ? ax : x0 + 1, bz = (d == 2 || d == 3) ? az : z0 + 1;
					wallFace(ax, az, bx, bz, static_cast<float>(dirX[d]), static_cast<float>(dirZ[d]), 0.0f, h, c.wallTexture, 0.0f, 1.0f, true);
				}
				break;
			case CellType::Edge:
			{
				const int d = (c.facing == Facing::PosX) ? 0 : (c.facing == Facing::NegX) ? 1 : (c.facing == Facing::PosZ) ? 2 : 3;
				const float ax = (d == 0) ? x0 + 1 : x0, az = (d == 2) ? z0 + 1 : z0;
				const float bx = (d == 0 || d == 1) ? ax : x0 + 1, bz = (d == 2 || d == 3) ? az : z0 + 1;
				wallFace(ax, az, bx, bz, static_cast<float>(dirX[d]), static_cast<float>(dirZ[d]), 0.0f, h, c.wallTexture, 0.0f, 1.0f, true);
				break;
			}
			case CellType::Diagonal:
				if (c.diagonalRight) wallFace(x0, z0, x0 + 1, z0 + 1, 1, -1, 0.0f, h, c.wallTexture, 0.0f, 1.0f, true);
				else wallFace(x0 + 1, z0, x0, z0 + 1, 1, 1, 0.0f, h, c.wallTexture, 0.0f, 1.0f, true);
				break;
			case CellType::Raised:
			{
				const float vTop = std::max(0.0f, 1.0f - c.raisedTop / h);
				const float vBottom = std::min(1.0f, 1.0f - c.raisedBottom / h);
				for (int d = 0; d < 4; d++)
				{
					if (this->level.cell(cx + dirX[d], cz + dirZ[d]).type == CellType::Wall) continue;
					const float ax = (d == 0) ? x0 + 1 : x0, az = (d == 2) ? z0 + 1 : z0;
					const float bx = (d == 0 || d == 1) ? ax : x0 + 1, bz = (d == 2 || d == 3) ? az : z0 + 1;
					wallFace(ax, az, bx, bz, static_cast<float>(dirX[d]), static_cast<float>(dirZ[d]), c.raisedBottom, c.raisedTop,
						c.wallTexture, vTop, vBottom, false);
				}

				flat(x0, z0, c.raisedTop, c.capTexture);
				if (c.raisedBottom > 0.0f) flat(x0, z0, c.raisedBottom, c.capTexture);
				break;
			}
			default:
				break;
			}
		}
	}

	// Sprites (billboards voltados para a câmera).
	const int frameTick = static_cast<int>(this->time * 8.0);
	for (const Sprite &sprite : this->level.getSprites())
	{
		const float ox = sprite.x - this->x, oz = sprite.z - this->z;
		if (ox * ox + oz * oz > radius * radius || ox * forward.x + oz * forward.z < -0.5f) continue;

		const float floorY = this->level.floorHeightAt(static_cast<int>(std::floor(sprite.x)), static_cast<int>(std::floor(sprite.z)));
		const float yb = floorY + sprite.yBottom;
		const float yt = yb + sprite.height;
		const float hw = sprite.width * 0.5f;
		const Vec3 v[4] =
		{
			{ sprite.x - right.x * hw, yt, sprite.z - right.z * hw },
			{ sprite.x + right.x * hw, yt, sprite.z + right.z * hw },
			{ sprite.x + right.x * hw, yb, sprite.z + right.z * hw },
			{ sprite.x - right.x * hw, yb, sprite.z - right.z * hw }
		};
		const float uv[4][2] = { { 0, 0 }, { 0.999f, 0 }, { 0.999f, 0.999f }, { 0, 0.999f } };
		// Nas ruas, lampiões e tochas só acendem à noite (de dia, a versão apagada quando existe).
		const bool lit = sprite.lightRadius > 0.0f && (!exterior || nightLights);
		const std::vector<int> &frames = (!lit && !sprite.dayFrames.empty()) ? sprite.dayFrames : sprite.frames;
		const int frame = frames[static_cast<size_t>(frameTick) % frames.size()];
		Surface surface;
		// Chamas de velas e braseiros brilham; o resto (taverneiro, estátuas...) fica fora do bloom.
		surface.kind = lit ? Surface::Kind::Emissive : Surface::Kind::Figure;
		this->renderer.drawQuad(v, uv, tex(frame), true, surface);
	}

	this->drawMagic();
	this->drawArrows();
	this->creatures.draw(this->renderer, this->level, this->x, this->z, static_cast<float>(DrawRadius));
	this->citizens.draw(this->renderer, this->level, this->x, this->z, static_cast<float>(ExteriorDrawRadius));
}

void arena::GameWorldScreen::drawWeapon(std::vector<uint32_t> &pixels, int width, int viewHeight)
{
	if (this->weapon.frames.empty() || this->health <= 0)
	{
		return;
	}

	int frameIndex = this->weapon.idleFrame;
	if (this->swinging && !this->swingFrames.empty())
	{
		const size_t step = std::min(this->swingFrames.size() - 1, static_cast<size_t>(this->swingTime * 16.0));
		frameIndex = this->swingFrames[step];
	}

	if (frameIndex >= static_cast<int>(this->weapon.frames.size()))
	{
		return;
	}

	// Os deslocamentos do CIF são relativos à área 3D original de 320x147.
	const WeaponFrame &frame = this->weapon.frames[static_cast<size_t>(frameIndex)];
	const Texture &texture = this->weapon.textures[static_cast<size_t>(frame.texture)];
	const float scale = viewHeight / 147.0f;
	const float bobX = static_cast<float>(std::sin(this->bob * 0.5)) * 4.0f;
	const float bobY = static_cast<float>(std::fabs(std::sin(this->bob * 0.5))) * 3.0f;
	const int x0 = static_cast<int>(width * 0.5f + (frame.offsetX - 160.0f + bobX) * scale);
	const int y0 = static_cast<int>((frame.offsetY + bobY) * scale);
	// Textura ampliada (estilo HD): tamanho na tela pelas unidades originais, com filtro bilinear.
	const float texelScale = scale / texture.scale;
	const bool smooth = texture.scale > 1;
	const int w = static_cast<int>(texture.width * texelScale);
	const int h = static_cast<int>(texture.height * texelScale);
	auto texelAt = [&texture](int tx, int ty)
	{
		return texture.pixels[static_cast<size_t>(std::clamp(ty, 0, texture.height - 1) * texture.width + std::clamp(tx, 0, texture.width - 1))];
	};
	for (int y = std::max(0, y0); y < std::min(viewHeight, y0 + h); y++)
	{
		const float fy = (y - y0 + 0.5f) / texelScale - 0.5f;
		for (int px = std::max(0, x0); px < std::min(width, x0 + w); px++)
		{
			const float fx = (px - x0 + 0.5f) / texelScale - 0.5f;
			if (!smooth)
			{
				const uint32_t texel = texelAt(static_cast<int>(fx + 0.5f), static_cast<int>(fy + 0.5f));
				if ((texel >> 24) != 0) pixels[static_cast<size_t>(y) * width + px] = texel;
				continue;
			}

			const int bx = static_cast<int>(std::floor(fx)), by = static_cast<int>(std::floor(fy));
			const float ax = fx - bx, ay = fy - by;
			float acc[3] = { 0, 0, 0 }, weightSum = 0.0f;
			for (int k = 0; k < 4; k++)
			{
				const uint32_t s = texelAt(bx + (k & 1), by + (k >> 1));
				if ((s >> 24) == 0) continue;
				const float weight = ((k & 1) ? ax : 1.0f - ax) * ((k >> 1) ? ay : 1.0f - ay);
				acc[0] += (s & 0xFF) * weight; acc[1] += ((s >> 8) & 0xFF) * weight; acc[2] += ((s >> 16) & 0xFF) * weight;
				weightSum += weight;
			}
			if (weightSum < 0.5f) continue;
			pixels[static_cast<size_t>(y) * width + px] = 0xFF000000u | (static_cast<uint32_t>(acc[2] / weightSum) << 16)
				| (static_cast<uint32_t>(acc[1] / weightSum) << 8) | static_cast<uint32_t>(acc[0] / weightSum);
		}
	}
}

std::string arena::GameWorldScreen::compassDirection() const
{
	// Mundo: +X = sul, +Z = oeste.
	const float north = -std::cos(this->yaw);
	const float east = -std::sin(this->yaw);
	float heading = std::atan2(east, north);
	if (heading < 0.0f) heading += 2.0f * Pi;
	const int index = static_cast<int>(std::lround(heading / (Pi / 4.0f))) % 8;
	return this->game.getText().get("World.Direction" + std::to_string(index));
}

void arena::GameWorldScreen::renderWorld(std::vector<uint32_t> &pixels, int width, int height)
{
	if (this->renderer.getWidth() != width || this->renderer.getHeight() != height)
	{
		this->renderer.resize(width, height);
	}

	if (!this->loaded)
	{
		std::fill(this->renderer.getPixels().begin(), this->renderer.getPixels().end(), 0xFF000000u);
	}
	else
	{
		Camera camera;
		const float bobOffset = static_cast<float>(std::sin(this->bob)) * 0.012f;
		camera.position = { this->x, EyeHeight + this->eyeOffset + bobOffset, this->z };
		camera.yaw = this->yaw;
		camera.pitch = this->pitch;
		camera.fovY = 1.05f;

		// Luz do personagem (tocha) + braseiros/velas do nível com tremulação.
		std::vector<PointLight> lights;
		const bool exteriorLevel = this->level.isExterior();
		const bool streetLights = !exteriorLevel || clock::isNight(this->game.getCharacter().gameMinutes);
		// Nas ruas, a tocha do personagem só faz diferença à noite.
		if (!exteriorLevel || clock::daylight(this->game.getCharacter().gameMinutes) < 0.6f) lights.push_back(PointLight{ camera.position, 4.5f, 1.0f, 0.9f, 0.75f });
		for (const Sprite &sprite : this->level.getSprites())
		{
			if (sprite.lightRadius <= 0.0f || !streetLights) continue;
			const float dx = sprite.x - this->x, dz = sprite.z - this->z;
			if (dx * dx + dz * dz > (exteriorLevel ? 24.0f : 16.0f) * (exteriorLevel ? 24.0f : 16.0f)) continue;
			const float flicker = 0.92f + 0.08f * static_cast<float>(std::sin(this->time * 9.0 + sprite.x * 3.1 + sprite.z));
			lights.push_back(PointLight{ { sprite.x, sprite.yBottom + sprite.height * 0.7f, sprite.z },
				sprite.lightRadius * 1.2f * flicker, 1.25f, 0.75f, 0.4f });
		}

		// Lava ilumina os arredores (as células mais próximas).
		std::vector<std::pair<float, Vec3>> lava;
		const int px = static_cast<int>(std::floor(this->x)), pz = static_cast<int>(std::floor(this->z));
		for (int cx = px - 8; cx <= px + 8; cx++)
		{
			for (int cz = pz - 8; cz <= pz + 8; cz++)
			{
				if (!this->level.inBounds(cx, cz) || this->level.cell(cx, cz).chasmKind != ChasmKind::Lava) continue;
				const float dx = cx + 0.5f - this->x, dz = cz + 0.5f - this->z;
				lava.push_back({ dx * dx + dz * dz, Vec3{ cx + 0.5f, -LiquidLevel + 0.25f, cz + 0.5f } });
			}
		}
		std::sort(lava.begin(), lava.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
		for (size_t i = 0; i < std::min<size_t>(6, lava.size()); i++)
		{
			const float flicker = 0.9f + 0.1f * static_cast<float>(std::sin(this->time * 3.0 + i));
			lights.push_back(PointLight{ lava[i].second, 2.4f * flicker, 1.3f, 0.45f, 0.1f });
		}

		this->addMagicLights(lights);
		this->renderer.setGpu(this->game.getGpu());
		{
			// Janelas: pretas de dia; à noite, a cor da tabela do original (alaranjado aceso).
			// Nos interiores ficam pretas, como no original.
			static constexpr uint8_t Twilight[16][3] = { { 63, 41, 0 }, { 59, 39, 0 }, { 55, 36, 0 }, { 51, 34, 0 }, { 47, 31, 0 }, { 43, 29, 0 }, { 39, 26, 0 }, { 35, 24, 0 },
				{ 31, 21, 0 }, { 27, 19, 0 }, { 23, 16, 0 }, { 19, 14, 0 }, { 15, 11, 0 }, { 11, 8, 0 }, { 7, 5, 0 }, { 3, 3, 0 } };
			const float light = this->level.isExterior() ? clock::daylight(this->game.getCharacter().gameMinutes) : 1.0f;
			if (light >= 0.999f) this->renderer.setWindowLight(18.0f, 20.0f, 26.0f, 0.0f); // vidro escuro de dia
			else
			{
				const auto &c = Twilight[std::clamp(static_cast<int>(light * 16.0f), 0, 15)];
				this->renderer.setWindowLight(c[0] * 4.0f, c[1] * 4.0f, c[2] * 4.0f, 1.0f - light);
			}
		}
		this->renderer.beginFrame(camera, this->level.isExterior() ? (0.22f + 0.78f * clock::daylight(this->game.getCharacter().gameMinutes)) * (this->weather() == Clear ? 1.0f : 0.72f) : (!this->returnMap.empty() && !this->isDungeon()) ? 0.22f : 0.06f, lights, static_cast<float>(this->time), this->game.getGraphics());
		this->drawLevel();
		this->renderer.finishFrame();
	}

	pixels = this->renderer.getPixels();
	if (this->loaded && this->level.isExterior()) this->drawWeather(pixels, width, height);
	this->drawHud(pixels, width, height);
	if (this->loaded && this->overlay != Overlay::None)
	{
		this->drawOverlay(pixels, width, height);
	}
}

void arena::GameWorldScreen::drawHud(std::vector<uint32_t> &pixels, int width, int height)
{
	using namespace hud;
	namespace col = kit::color;
	Frame frame{ pixels, width, height };
	kit::Painter painter(frame, this->game.getFonts());
	const Fonts &fonts = this->game.getFonts();
	const Localization &text = this->game.getText();
	const int u = painter.unit();
	const int margin = 8 * u;
	this->uiFrameW = width;
	this->uiFrameH = height;
	this->screenButtons.clear();

	if (!this->loaded)
	{
		painter.text(fonts.arena, text.get("World.LoadError") + " " + this->loadError, width / 2, height / 2, rgb(255, 120, 100), TextAlign::Center);
		return;
	}

	// Arma (ou mãos conjurando) e flash vermelho ao ser ferido.
	const int weaponView = height - 24 * u; // proporção da área 3D original (arma um pouco acima da borda)
	if (this->casting) this->drawCasting(pixels, width, weaponView);
	else this->drawWeapon(pixels, width, weaponView);
	if (this->hurtTimer > 0.0)
	{
		fillRect(frame, 0, 0, width, height, rgb(200, 0, 0, static_cast<uint8_t>(std::min(1.0, this->hurtTimer / 0.4) * 110)));
	}

	// Mira discreta.
	const int cxp = width / 2, cyp = height / 2;
	fillRect(frame, cxp - 3 * u, cyp, 7 * u, u, rgb(240, 220, 170, 150));
	fillRect(frame, cxp, cyp - 3 * u, u, 7 * u, rgb(240, 220, 170, 150));

	// Local (canto superior esquerdo) e efeitos mágicos ativos logo abaixo.
	painter.text(fonts.a, this->currentLocation(), margin, margin, col::GoldBright);
	int chipY = margin + 20 * u;
	for (const std::string &effect : this->describeEffects())
	{
		const int w = painter.textWidth(fonts.arena, effect) + 10 * u;
		painter.fill(Rect{ margin, chipY, w, 13 * u }, rgb(20, 14, 30, 190));
		painter.fill(Rect{ margin, chipY, 2 * u, 13 * u }, col::Magic);
		painter.text(fonts.arena, effect, margin + 6 * u, chipY + 3 * u, rgb(200, 210, 255));
		chipY += 15 * u;
	}

	// Bússola em faixa (topo, centro): direções deslizam conforme o giro.
	{
		const int bandW = 180 * u, bandH = 16 * u;
		const Rect band{ cxp - bandW / 2, margin, bandW, bandH };
		painter.fill(band, rgb(16, 11, 7, 200));
		painter.fill(Rect{ band.x, band.y, band.w, u }, col::GoldDim);
		painter.fill(Rect{ band.x, band.y + band.h - u, band.w, u }, col::GoldDim);
		const float north = -std::cos(this->yaw), east = -std::sin(this->yaw);
		const float heading = std::atan2(east, north) * 180.0f / Pi;
		constexpr float Span = 80.0f; // graus visíveis de cada lado
		for (int tick = 0; tick < 360; tick += 15)
		{
			float delta = tick - heading;
			while (delta > 180.0f) delta -= 360.0f;
			while (delta < -180.0f) delta += 360.0f;
			if (std::fabs(delta) > Span) continue;
			const int tx = cxp + static_cast<int>(delta / Span * (bandW / 2 - 8 * u));
			if (tick % 45 == 0)
			{
				const int dir = tick / 45;
				const bool cardinal = dir % 2 == 0;
				painter.text(fonts.arena, text.get("Compass.D" + std::to_string(dir)), tx, band.y + 4 * u, cardinal ? col::GoldBright : col::TextDim, TextAlign::Center);
			}
			else
			{
				fillRect(frame, tx, band.y + 6 * u, u, 4 * u, rgb(150, 120, 70, 200));
			}
		}

		// Marcador central.
		for (int i = 0; i < 3; i++) fillRect(frame, cxp - (2 - i) * u, band.y + band.h + i * u, (5 - 2 * i) * u, u, col::Gold);

		// Hora e data à direita da bússola.
		const double minutes = this->game.getCharacter().gameMinutes;
		char clockText[16];
		std::snprintf(clockText, sizeof(clockText), "%02d:%02d", clock::hour(minutes), clock::minute(minutes));
		painter.text(fonts.arena, clockText, band.x + band.w + 8 * u, band.y + 4 * u, col::GoldBright);
		painter.text(fonts.arena, ui::format(text.get("Calendar.Date"), { std::to_string(clock::dayOfMonth(minutes)), text.get("Calendar.Month" + std::to_string(clock::month(minutes))) }),
			band.x + band.w + 8 * u, band.y + 15 * u, col::TextDim);

		// Criatura sob a mira: nome e vida, logo abaixo da bússola.
		const int target = this->creatures.findTarget(this->x, this->z, this->yaw, 4.0f, 0.97f);
		if (target >= 0 && this->health > 0)
		{
			const Creature &c = this->creatures.getCreatures()[static_cast<size_t>(target)];
			const std::string name = text.getOr("Creatures.Name" + std::to_string(this->textIndexOf(c.definition)), this->creatures.definitionOf(target).originalName);
			const int barW = 90 * u;
			painter.text(fonts.arena, name, cxp, band.y + band.h + 6 * u, rgb(255, 225, 170), TextAlign::Center);
			const Rect hp{ cxp - barW / 2, band.y + band.h + 17 * u, barW, 5 * u };
			painter.fill(hp, rgb(30, 10, 8, 220));
			painter.fill(Rect{ hp.x, hp.y, barW * std::max(0, c.hp) / std::max(1, c.maxHp), hp.h }, rgb(190, 40, 28));
		}
	}

	// Minimapa (canto superior direito) com moldura de madeira. Norte para cima: -X do mundo; leste à direita: -Z.
	if (this->showMinimap)
	{
		constexpr int Radius = 10;
		const int cellPx = 4 * u;
		const int size = (Radius * 2 + 1) * cellPx;
		const int frameT = 6 * u;
		const Rect box{ width - margin - size - 2 * frameT, margin, size + 2 * frameT, size + 2 * frameT };
		painter.woodPanel(box, 230);
		const int mapX = box.x + frameT, mapY = box.y + frameT;
		const int pcx = static_cast<int>(std::floor(this->x));
		const int pcz = static_cast<int>(std::floor(this->z));
		// O mapa desliza pela fração da posição (o jogador fica sempre no centro), sem pular de célula em célula.
		const int offsetX = static_cast<int>(std::lround((this->z - pcz - 0.5f) * cellPx));
		const int offsetY = static_cast<int>(std::lround((0.5f - (this->x - pcx)) * cellPx));
		auto clippedCell = [&](int px, int py, uint32_t tint)
		{
			const int x0 = std::max(px, mapX), y0 = std::max(py, mapY);
			const int x1 = std::min(px + cellPx, mapX + size), y1 = std::min(py + cellPx, mapY + size);
			if (x1 > x0 && y1 > y0) fillRect(frame, x0, y0, x1 - x0, y1 - y0, tint);
		};
		for (int row = -Radius - 1; row <= Radius + 1; row++)
		{
			for (int c = -Radius - 1; c <= Radius + 1; c++)
			{
				const int cx = pcx + row, cz = pcz - c;
				uint32_t tint = rgb(26, 20, 14);
				if (this->level.inBounds(cx, cz))
				{
					const Cell &cell = this->level.cell(cx, cz);
					tint = rgb(92, 78, 60);
					if (cell.type == CellType::Wall) tint = cell.isExit ? rgb(230, 190, 60) : rgb(40, 32, 24);
					else if (cell.type == CellType::Door) tint = this->level.doorOpenness(cx, cz) > 0.5f ? rgb(140, 96, 50) : rgb(200, 120, 40);
					else if (cell.type == CellType::Raised) tint = rgb(120, 104, 84);
					else if (cell.chasm) tint = rgb(46, 76, 140);
				}
				clippedCell(mapX + (c + Radius) * cellPx + offsetX, mapY + (row + Radius) * cellPx + offsetY, tint);
			}
		}

		const float fx = std::cos(this->yaw), fz = std::sin(this->yaw);
		const float sx = mapX + (Radius + 0.5f) * cellPx;
		const float sy = mapY + (Radius + 0.5f) * cellPx;
		for (int i = 0; i < 2 * cellPx; i++) fillRect(frame, static_cast<int>(sx - fz * i), static_cast<int>(sy + fx * i), u, u, col::GoldBright);
		fillRect(frame, static_cast<int>(sx) - u, static_cast<int>(sy) - u, 3 * u, 3 * u, col::GoldBright);
		painter.text(fonts.arena, text.get("World.North"), mapX + size / 2, mapY + u, col::GoldBright, TextAlign::Center);
	}

	// Painéis inferiores: personagem e barras (esquerda); arma, feitiço e tesouro (direita).
	const CharacterDraft &character = this->game.getCharacter();
	const int panelW = 170 * u, panelH = 62 * u;
	const Rect left{ margin, height - margin - panelH, panelW, panelH };
	painter.woodPanel(left, 225);
	painter.text(fonts.arena, character.name, left.x + 9 * u, left.y + 8 * u, col::GoldBright);
	painter.text(fonts.arena, text.get("Menu.Level") + " " + std::to_string(character.level), left.x + left.w - 9 * u, left.y + 8 * u, col::TextDim, TextAlign::Right);
	const int barX = left.x + 9 * u, barW = panelW - 18 * u;
	int barY = left.y + 20 * u;
	auto bar = [&](const std::string &label, int value, int maxValue, uint32_t tint)
	{
		painter.bar(Rect{ barX, barY, barW, 11 * u }, value, maxValue, tint, label + " " + std::to_string(value) + "/" + std::to_string(maxValue));
		barY += 13 * u;
	};
	bar(text.get("Sheet.Health"), this->health, character.maxHealth, col::Health);
	bar(text.get("Sheet.Fatigue"), character.maxStamina, character.maxStamina, col::Stamina);
	bar(text.get("Sheet.SpellPoints"), this->spellPoints, character.maxSpellPoints, col::Magic);

	const Rect right{ width - margin - panelW, height - margin - panelH, panelW, panelH };
	painter.woodPanel(right, 225);
	int ry = right.y + 8 * u;
	// Rótulo à esquerda e valor à direita; o valor é encurtado (com reticências) se não couber.
	auto fitted = [&](const std::string &label, std::string value)
	{
		const int room = right.w - 18 * u - painter.textWidth(fonts.arena, label) - 6 * u;
		if (painter.textWidth(fonts.arena, value) <= room) return value;
		while (!value.empty() && painter.textWidth(fonts.arena, value + "...") > room)
		{
			value.pop_back();
			while (!value.empty() && (static_cast<unsigned char>(value.back()) & 0xC0) == 0x80) value.pop_back(); // UTF-8
			if (!value.empty() && (static_cast<unsigned char>(value.back()) & 0x80)) value.pop_back();
		}
		return value + "...";
	};
	painter.text(fonts.arena, text.get("Hud.Weapon"), right.x + 9 * u, ry, col::TextDim);
	painter.text(fonts.arena, fitted(text.get("Hud.Weapon"), this->weapon.name), right.x + right.w - 9 * u, ry, col::Text, TextAlign::Right);
	ry += 13 * u;
	std::string spellLine = text.get("Hud.NoSpell");
	if (const Spell *ready = this->game.getData().getSpells().find(character.readySpell))
	{
		spellLine = spellName(this->game, *ready) + " (" + std::to_string(artifact::isGranted(this->game.getCharacter(), ready->index) ? 0 : ready->castingCost()) + ")";
	}
	painter.text(fonts.arena, text.get("Hud.Spell"), right.x + 9 * u, ry, col::TextDim);
	painter.text(fonts.arena, fitted(text.get("Hud.Spell"), spellLine), right.x + right.w - 9 * u, ry, rgb(170, 190, 255), TextAlign::Right);
	ry += 15 * u;
	painter.divider(right.x + 9 * u, ry, right.w - 18 * u, col::GoldDim);
	ry += 6 * u;
	std::string wealth = text.get("Menu.Gold") + " " + std::to_string(character.gold) + "   " + text.get("Hud.Exp") + " " + std::to_string(this->experience);
	if (!this->keys.empty()) wealth += "   " + text.get("World.Keys") + " " + std::to_string(this->keys.size());
	painter.text(fonts.arena, wealth, right.x + right.w / 2, ry, col::GoldBright, TextAlign::Center);

	// Mensagens: faixa translúcida no centro, acima dos painéis.
	if (this->messageTimer > 0.0 && !this->message.empty() && !this->paused && this->health > 0)
	{
		const int boxW = std::min(width - 2 * (margin + panelW + 6 * u), 330 * u);
		const std::string wrapped = ui::wrapText(fonts.arena, this->message, std::max(40, (boxW - 16 * u) / u));
		const int lines = 1 + static_cast<int>(std::count(wrapped.begin(), wrapped.end(), '\n'));
		const int boxH = lines * 12 * u + 10 * u;
		const Rect box{ cxp - boxW / 2, height - margin - boxH, boxW, boxH };
		const uint8_t alpha = static_cast<uint8_t>(std::clamp(this->messageTimer * 2.0, 0.0, 1.0) * 190);
		painter.fill(box, rgb(14, 10, 6, alpha));
		painter.fill(Rect{ box.x, box.y, box.w, u }, rgb(201, 158, 82, alpha));
		painter.fill(Rect{ box.x, box.y + box.h - u, box.w, u }, rgb(201, 158, 82, alpha));
		painter.paragraph(fonts.arena, wrapped, Rect{ box.x + 8 * u, box.y + 6 * u, box.w - 16 * u, box.h }, col::Text, true);
	}

	// Ajuda: quadro de controles (H mostra/esconde; aparece sozinho no começo).
	if (this->helpTimer > 0.0 && !this->paused && this->health > 0)
	{
		const int rows = 8;
		const Rect box{ cxp - 150 * u, height / 2 - 75 * u, 300 * u, 30 * u + rows * 13 * u };
		painter.woodPanel(box, 215);
		painter.text(fonts.a, text.get("Help.Title"), box.x + box.w / 2, box.y + 8 * u, col::GoldBright, TextAlign::Center);
		for (int i = 0; i < 15; i++)
		{
			const std::string entry = text.get("Help.K" + std::to_string(i + 1));
			const size_t separator = entry.find('|');
			const int cx = box.x + 12 * u + (i / rows) * (box.w / 2);
			const int cy = box.y + 26 * u + (i % rows) * 13 * u;
			painter.text(fonts.arena, entry.substr(0, separator), cx, cy, col::GoldBright);
			if (separator != std::string::npos) painter.text(fonts.arena, entry.substr(separator + 1), cx + 46 * u, cy, col::Text);
		}
	}

	// Pausa e morte: painel central com botões clicáveis.
	auto screenPanel = [&](const std::string &title, uint32_t titleTint, const std::vector<std::pair<std::string, char>> &buttons, uint32_t dim)
	{
		fillRect(frame, 0, 0, width, height, dim);
		const int bw = 150 * u, bh = 18 * u, gap = 6 * u;
		const int boxH = 44 * u + static_cast<int>(buttons.size()) * (bh + gap);
		const Rect box{ cxp - (bw + 30 * u) / 2, cyp - boxH / 2, bw + 30 * u, boxH };
		painter.woodPanel(box, 240);
		painter.text(fonts.a, title, cxp, box.y + 10 * u, titleTint, TextAlign::Center);
		painter.divider(box.x + 12 * u, box.y + 30 * u, box.w - 24 * u, col::GoldDim);
		int y = box.y + 38 * u;
		for (const auto &[label, action] : buttons)
		{
			const Rect r{ cxp - bw / 2, y, bw, bh };
			painter.button(r, label, kit::hit(r, this->uiMouseX, this->uiMouseY), false);
			this->screenButtons.push_back({ r, action });
			y += bh + gap;
		}
	};

	if (this->cutscene.active())
	{
		this->cutscene.draw(painter, width, height);
	}
	else if (this->health <= 0)
	{
		screenPanel(text.get("World.Dead"), rgb(230, 70, 50), { { text.get("Pause.Load"), 'l' }, { text.get("Pause.Restart"), 'r' }, { text.get("Pause.MainMenu"), 'm' } }, rgb(60, 0, 0, 160));
	}
	else if (this->gateTimer > 0.0)
	{
		// Clarão branco-dourado crescendo enquanto o portão leva o jogador.
		const double t = std::clamp(1.0 - this->gateTimer / GateDuration, 0.0, 1.0);
		fillRect(frame, 0, 0, width, height, rgb(255, 244, 214, static_cast<uint8_t>(std::min(1.0, t * 1.4) * 255)));
	}
	else if (!this->visionText.empty())
	{
		this->drawVision(painter, width, height);
	}
	else if (this->chapterOpen)
	{
		this->drawChapterEnd(painter, width, height);
	}
	else if (this->paused)
	{
		screenPanel(text.get("World.Paused"), col::GoldBright, { { text.get("Pause.Continue"), 'c' }, { text.get("Pause.Save"), 's' }, { text.get("Pause.Load"), 'l' },
			{ text.get("Pause.Character"), 'p' }, { text.get("Pause.Spells"), 'b' }, { text.get("Pause.Journal"), 'j' }, { ui::format(text.get("Pause.UiScale"), { std::to_string(static_cast<int>(std::lround(kit::interfaceScale() * 100.0f))) }), 'u' }, { text.get("Pause.Help"), 'h' }, { text.get("Pause.MainMenu"), 'm' } }, rgb(0, 0, 0, 150));
	}
}

void arena::GameWorldScreen::drawChapterEnd(kit::Painter &painter, int width, int height)
{
	// Fim do capítulo: visão de Ria Silmane e o resumo da fuga.
	namespace col = kit::color;
	const int u = painter.unit();
	const Fonts &fonts = this->game.getFonts();
	const Localization &text = this->game.getText();
	const CharacterDraft &character = this->game.getCharacter();
	painter.fill(Rect{ 0, 0, width, height }, hud::rgb(12, 8, 4, 215));

	const int w = std::min(width - 16 * u, 420 * u), h = std::min(height - 16 * u, 232 * u);
	const Rect box{ (width - w) / 2, (height - h) / 2, w, h };
	painter.woodPanel(box, 245);
	painter.text(fonts.a, text.get("Chapter.Title"), box.x + w / 2, box.y + 10 * u, col::GoldBright, TextAlign::Center);
	painter.divider(box.x + 14 * u, box.y + 32 * u, w - 28 * u, col::GoldDim);

	const Rect page{ box.x + 14 * u, box.y + 40 * u, w - 28 * u, h - 40 * u - 34 * u };
	painter.parchmentPanel(page);
	int y = page.y + 10 * u;
	y += painter.paragraph(fonts.arena, ui::format(this->game.gendered("Chapter.Vision"), { character.name }), Rect{ page.x + 12 * u, y, page.w - 24 * u, page.h }, col::Ink) + 8 * u;

	int kills = 0;
	for (int i = 0; i < static_cast<int>(this->creatures.getCreatures().size()); i++) if (!this->creatures.isAlive(i)) kills++;
	painter.divider(page.x + 12 * u, y, page.w - 24 * u, col::InkSoft);
	y += 8 * u;
	painter.text(fonts.arena, ui::format(text.get("Chapter.Stats"), { std::to_string(this->game.getCharacter().level), std::to_string(this->experience),
		std::to_string(kills), std::to_string(character.gold) }), page.x + page.w / 2, y, col::Ink, TextAlign::Center, false);
	y += 16 * u;
	painter.paragraph(fonts.arena, text.get("Chapter.Note"), Rect{ page.x + 12 * u, y, page.w - 24 * u, page.y + page.h - y }, col::InkSoft);

	const CityLocation *destination = this->game.getData().getCity(std::clamp(character.raceIndex, 0, 7), std::max(0, this->nextCity));
	const std::string travel = ui::format(text.get("Chapter.Travel"), { destination ? destination->name : std::string() });
	const std::pair<std::string, char> buttons[] = { { travel, 't' }, { text.get("Chapter.Explore"), 'x' }, { text.get("Pause.MainMenu"), 'm' } };
	const int bw = (w - 28 * u - 2 * 8 * u) / 3;
	int bx = box.x + 14 * u;
	for (const auto &[label, action] : buttons)
	{
		const Rect r{ bx, box.y + h - 26 * u, bw, 18 * u };
		painter.button(r, label, kit::hit(r, this->uiMouseX, this->uiMouseY), false);
		this->screenButtons.push_back({ r, action });
		bx += bw + 8 * u;
	}
}

void arena::GameWorldScreen::drawSky()
{
	// Cúpula simples em volta da câmera: degradê do horizonte (claro) ao alto (azul).
	// Cores pela hora: dia azul, crepúsculo alaranjado no horizonte, noite azul-escura.
	const double minutes = this->game.getCharacter().gameMinutes;
	const float daylight = clock::daylight(minutes);
	const int currentWeather = static_cast<int>(this->weather());
	if (this->skyTexture.pixels.empty() || std::fabs(daylight - this->skyDaylight) > 0.01f || currentWeather != this->skyWeather)
	{
		this->skyWeather = currentWeather;
		constexpr int Height = 128;
		this->skyDaylight = daylight;
		this->skyTexture.width = 1;
		this->skyTexture.height = Height;
		this->skyTexture.scale = 2; // filtro suave
		this->skyTexture.id = newTextureId();
		this->skyTexture.pixels.clear();
		const float dusk = std::clamp(1.0f - std::fabs(daylight - 0.45f) / 0.45f, 0.0f, 1.0f); // máximo no meio da transição
		auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
		for (int y = 0; y < Height; y++)
		{
			const float t = std::pow(static_cast<float>(y) / (Height - 1), 0.8f); // 0 = alto, 1 = horizonte
			// Dia (alto -> horizonte) e noite, misturados pela luz; o crepúsculo esquenta o horizonte.
			float r = lerp(lerp(8, 56, daylight), lerp(20, 186, daylight), t);
			float g = lerp(lerp(12, 110, daylight), lerp(28, 212, daylight), t);
			float b = lerp(lerp(30, 176, daylight), lerp(52, 236, daylight), t);
			const float warm = dusk * t * t;
			r = lerp(r, 235, warm * 0.8f); g = lerp(g, 140, warm * 0.6f); b = lerp(b, 90, warm * 0.6f);
			if (currentWeather != Clear)
			{
				// Nuvens: céu acinzentado (mais claro na neve), mantendo o escuro da noite.
				const float grey = (r * 0.3f + g * 0.59f + b * 0.11f) * (currentWeather == Snow ? 1.05f : 0.8f);
				r = lerp(r, grey, 0.85f); g = lerp(g, grey, 0.85f); b = lerp(b, grey * 1.04f, 0.85f);
			}
			this->skyTexture.pixels.push_back(0xFF000000u | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r));
		}
	}

	constexpr float R = 52.0f, Top = 28.0f, Bottom = -4.0f; // quinas dentro do limite de desenho (96)
	const float cx = this->x, cz = this->z;
	Surface sky;
	sky.kind = Surface::Kind::Sky;
	const float corners[4][2] = { { -R, -R }, { R, -R }, { R, R }, { -R, R } };
	for (int i = 0; i < 4; i++)
	{
		const float *a = corners[i], *b = corners[(i + 1) % 4];
		const Vec3 v[4] = { { cx + a[0], Top, cz + a[1] }, { cx + b[0], Top, cz + b[1] }, { cx + b[0], Bottom, cz + b[1] }, { cx + a[0], Bottom, cz + a[1] } };
		const float uv[4][2] = { { 0, 0.02f }, { 1, 0.02f }, { 1, 0.99f }, { 0, 0.99f } };
		this->renderer.drawQuad(v, uv, this->skyTexture, true, sky);
	}

	const Vec3 lid[4] = { { cx - R, Top, cz - R }, { cx + R, Top, cz - R }, { cx + R, Top, cz + R }, { cx - R, Top, cz + R } };
	const float lidUv[4][2] = { { 0, 0.01f }, { 1, 0.01f }, { 1, 0.02f }, { 0, 0.02f } };
	this->renderer.drawQuad(lid, lidUv, this->skyTexture, true, sky);
}

bool arena::GameWorldScreen::isPrison() const
{
	return this->mifName == this->game.getText().get("World.StartDungeonMif");
}

bool arena::GameWorldScreen::enterBuilding(int cellX, int cellZ, const Cell &cell)
{
	// *MENU da cidade -> tipo de prédio (como no original): prefixo do .MIF no executável e
	// variação pela posição da porta (ror(offset, 4) ^ offset) % 8.
	const Localization &text = this->game.getText();
	struct Building { const char *prefix; const char *nameKey; };
	static const std::map<int, Building> Buildings =
	{
		{ 0, { "EQUIP", "City.Equipment" } }, { 1, { "TAVERN", "City.Tavern" } }, { 2, { "MAGE", "City.MagesGuild" } },
		{ 3, { "TEMPLE", "City.Temple" } }, { 4, { "BS", "City.House" } }, { 5, { "BS", "City.House" } }, { 6, { "BS", "City.House" } },
		{ 9, { "NOBLE", "City.Noble" } }, { 11, { "PALACE", "City.Palace" } }, { 12, { "PALACE", "City.Palace" } }, { 13, { "PALACE", "City.Palace" } }
	};

	// Ermo: *MENU com outra numeração (cripta, casa, taverna, templo, torre, portões, masmorras).
	static const std::map<int, Building> WildBuildings =
	{
		{ 1, { "WCRYPT", "Wild.Crypt" } }, { 2, { "BS", "City.House" } }, { 3, { "TAVERN", "City.Tavern" } },
		{ 4, { "TEMPLE", "City.Temple" } }, { 5, { "TOWER", "Wild.Tower" } }
	};

	const bool wild = this->isWild();
	if ((!wild && (cell.menu == 7 || cell.menu == 8)) || (wild && (cell.menu == 6 || cell.menu == 7))) return this->useCityGate(cellX, cellZ);
	if (wild && (cell.menu == 8 || cell.menu == 9))
	{
		// Masmorra do ermo: semente pela província e pelo bloco do ermo (2x2 blocos de 32).
		const CityLocation *city = this->currentCity();
		if (!city) return false;
		const int blockX = cellZ / WildChunk + this->wildFirstX, blockY = cellX / WildChunk + this->wildFirstY;
		const uint32_t seed = wildDungeonSeed(this->game.getData(), city->province, blockX, blockY);
		char back[160];
		std::snprintf(back, sizeof(back), "@%s@%.2f@%.2f@%.3f@", this->mifName.c_str(), this->x, this->z, this->yaw + Pi);
		this->enterDungeon("DNG:" + std::to_string(seed) + ":2:2:0:0", back, ui::format(text.get("Dungeon.WildName"), { city->name }));
		return true;
	}

	const auto &table = wild ? WildBuildings : Buildings;
	const auto it = table.find(cell.menu);
	if (it == table.end()) return false;

	// Lojas, guilda, casas nobres e palácio fecham à noite (das 21h às 6h).
	const bool nightClosed = !wild && (cell.menu == 0 || cell.menu == 2 || cell.menu == 9 || cell.menu >= 11);
	if (nightClosed && clock::shopsClosed(this->game.getCharacter().gameMinutes))
	{
		this->showMessage(text.get("City.ClosedAtNight"), 3.0);
		return false;
	}

	// Coordenadas originais: x = coluna (Z do mundo), y = linha (X do mundo).
	const uint16_t offset = static_cast<uint16_t>((cellX << 8) + (cellZ << 1));
	const uint16_t rotated = static_cast<uint16_t>((offset >> 4) | (offset << 12));
	int variant = ((rotated ^ offset) % 8) + 1;
	std::string prefix = it->second.prefix;
	if (prefix == "PALACE")
	{
		// Missão: o governante que pediu o item da pista recebe-o e marca a masmorra do pedaço.
		CharacterDraft &hero = this->game.getCharacter();
		const CityLocation *here = this->currentCity();
		if (hero.questStep == 3 && hero.staffPieces < quest::PieceCount && here && here->globalId == hero.questGiver)
		{
			const int questProvince = quest::targetProvince(hero);
			const CityLocation *piece = this->game.getData().getCity(questProvince, quest::PieceDungeon);
			const std::string item = text.get("Quest.Item" + std::to_string(hero.staffPieces));
			hero.questStep = 4;
			quest::note(hero, ui::format(text.get("Journal.Delivered"), { item, here->name, piece ? piece->name : std::string() }));
			this->game.setPendingVision(text.get("Quest.RulerTitle"), ui::format(text.get("Quest.RulerThanks"), { hero.name, item, piece ? piece->name : std::string() }));
		}

		// Palácio: 3 variações pela semente do governante; vilas e aldeias têm o seu.
		const CityLocation *city = this->currentCity();
		if (city && city->type == CityLocation::Type::Town) prefix = "TOWNPAL";
		else if (city && city->type == CityLocation::Type::Village) prefix = "VILPAL";
		const uint32_t ruler = city ? ((static_cast<uint32_t>(city->globalX) << 16) + static_cast<uint32_t>(city->globalY)) : 0u;
		const uint32_t rulerSeed = (ruler << 16) | (ruler >> 16);
		variant = static_cast<int>(((rulerSeed >> 8) & 0xFFFF) % 3) + 1;
	}

	const std::string mif = prefix + std::to_string(variant) + ".MIF";
	if (!this->game.getAssets().exists(mif))
	{
		this->showMessage(text.get("City.DoorLocked"), 2.5);
		return false;
	}

	// Volta para a rua: a célula do jogador, olhando para fora do prédio.
	char back[160];
	std::snprintf(back, sizeof(back), "@%s@%.2f@%.2f@%.3f@", this->mifName.c_str(), this->x, this->z, this->yaw + Pi);
	const std::string name = ui::format(text.get(it->second.nameKey), { this->locationName });
	this->persist();
	this->game.playSound("OPENDOOR.VOC");
	this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, "IN:" + mif + back + name));
	return true;
}

void arena::GameWorldScreen::leaveBuilding()
{
	// Sai para a rua, na frente da porta por onde entrou.
	char where[96];
	std::snprintf(where, sizeof(where), "%.2f,%.2f,%.1f", this->returnX, this->returnZ, this->returnYaw * 180.0f / Pi);
	this->game.playSound("CLOSDOOR.VOC");
	this->persist();
	if (this->returnX >= 0.0f) this->game.setPendingSpawn(where);
	this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, this->returnMap));
}

bool arena::GameWorldScreen::tryTalkToClerk()
{
	// Atendente à frente (taverneiro, ferreiro, sacerdote ou mago) num prédio da cidade.
	if (this->returnMap.empty()) return false;
	const std::string building = this->level.getInfName();
	ServiceMenu::Kind kind;
	if (building.rfind("EQUIP", 0) == 0) kind = ServiceMenu::Kind::Equipment;
	else if (building.rfind("TEMPLE", 0) == 0) kind = ServiceMenu::Kind::Temple;
	else if (building.rfind("MAGE", 0) == 0) kind = ServiceMenu::Kind::MagesGuild;
	else if (building.rfind("TAVERN", 0) == 0 || building.rfind("NEWTAV", 0) == 0) kind = ServiceMenu::Kind::Tavern;
	else return false;

	static const char *Clerks[] = { "BARTEND.DFA", "SMITH.DFA", "PREST1.DFA", "PREST2.DFA", "WIZZ1.DFA" };
	const float fx = std::cos(this->yaw), fz = std::sin(this->yaw);
	bool facing = false;
	for (const Sprite &sprite : this->level.getSprites())
	{
		if (std::find_if(std::begin(Clerks), std::end(Clerks), [&sprite](const char *name) { return sprite.name == name; }) == std::end(Clerks)) continue;
		const float dx = sprite.x - this->x, dz = sprite.z - this->z;
		const float along = dx * fx + dz * fz;
		const float side = std::fabs(dx * fz - dz * fx);
		if (along > 0.0f && along < 2.6f && side < 0.9f) { facing = true; break; }
	}

	if (!facing) return false;

	// Taverneiro: entrega ou pagamento do trabalho que ele (ou outro taverneiro) pediu.
	const CityLocation *city = this->currentCity();
	if (kind == ServiceMenu::Kind::Tavern && city)
	{
		const jobs::Outcome outcome = jobs::arrive(this->game, *city, false);
		if (!outcome.text.empty())
		{
			this->openVision(outcome.title, outcome.text);
			this->persist();
			return true;
		}
	}

	// Preços pela qualidade do lugar e estoque fixo por loja (semente pela porta).
	const int factor = !city || city->type == CityLocation::Type::CityState ? 100 : city->type == CityLocation::Type::Town ? 90 : 80;
	const uint32_t seed = (city ? city->seed() : 0u) ^ (static_cast<uint32_t>(this->returnX * 16.0f) << 16) ^ static_cast<uint32_t>(this->returnZ * 16.0f);
	this->serviceMenu.open(this->game, kind, this->locationName, seed, factor, this->health, this->spellPoints);
	this->overlay = Overlay::Service;
	this->heldKeys.clear();
	this->holdLeft = this->holdRight = this->holdUp = this->holdDown = false;
	this->game.playSound("CLICKS.VOC", 0.5f);
	return true;
}

bool arena::GameWorldScreen::tryTalkToCitizen()
{
	// Morador à frente: vira para o jogador e cumprimenta (uma das falas do idioma).
	if (this->citizens.empty()) return false;
	const int index = this->citizens.findFacing(this->x, this->z, this->yaw, 3.0f);
	if (index < 0) return false;
	this->citizens.faceTowards(index, this->x, this->z);
	const Localization &text = this->game.getText();
	const bool male = this->citizens.isMale(index);

	// Nome fixo para cada morador (semente pela cidade e pelo índice), pelas regras do original.
	const CityLocation *city = this->currentCity();
	const int province = city ? city->province : 0;
	ArenaRandom random((city ? city->seed() : 1u) + static_cast<uint32_t>(index) * 2654435761u);
	const std::string name = this->game.getData().npcName(province, male, random);
	const int line = static_cast<int>(this->game.getRandom()() % 8u);
	const std::string greeting = ui::format(text.get(std::string(male ? "Citizens.Male" : "Citizens.Female") + std::to_string(line)), { this->locationName, this->game.getCharacter().name });
	const std::string who = ui::format(text.get("Dialogue.WhoAnswer" + std::to_string(static_cast<int>(random.next() % 6))), { name, this->locationName });

	// Onde fica: o prédio mais próximo de cada tipo, a partir do morador.
	const auto [cx, cz] = this->citizens.position(index);
	std::vector<DialogueMenu::Place> places;
	const std::pair<int, const char *> kinds[] = { { 1, "City.PlaceTavern" }, { 0, "City.PlaceEquipment" }, { 3, "City.PlaceTemple" }, { 2, "City.PlaceMagesGuild" }, { 11, "City.PlacePalace" } };
	for (const auto &[menu, key] : kinds) places.push_back({ text.get(key), this->placeDirections(cx, cz, menu, text.get(std::string(key) + "The")) });

	// Boatos em ordem sorteada.
	std::vector<std::string> rumors;
	for (int i = 0; text.has("Rumors.R" + std::to_string(i)); i++) rumors.push_back(ui::format(text.get("Rumors.R" + std::to_string(i)), { this->locationName }));
	std::shuffle(rumors.begin(), rumors.end(), this->game.getRandom());

	// Missão principal: depois do sonho, os moradores sabem onde procurar. Na província certa,
	// o primeiro boato aponta a masmorra da pista (e a marca no mapa); nas outras, a província.
	const CharacterDraft &character = this->game.getCharacter();
	const int questProvince = quest::targetProvince(character);
	const CityLocation *pieceDungeon = questProvince >= 0 ? this->game.getData().getCity(questProvince, quest::PieceDungeon) : nullptr;
	const CityLocation *clueDungeon = questProvince >= 0 ? this->game.getData().getCity(questProvince, quest::ClueDungeon) : nullptr;
	const bool questRumor = character.questStep == 1 && pieceDungeon && clueDungeon;
	if (questRumor)
	{
		rumors.insert(rumors.begin(), province == questProvince ? ui::format(text.get("Quest.RumorHere"), { pieceDungeon->name, text.get("Quest.Item" + std::to_string(character.staffPieces)), clueDungeon->name })
			: ui::format(text.get("Quest.RumorElsewhere"), { pieceDungeon->name, text.get("Provinces.Name" + std::to_string(questProvince)) }));
	}

	this->dialogueMenu.open(name, greeting, who, std::move(places), std::move(rumors));
	this->dialogueMenu.firstRumorIsQuest = questRumor && province == questProvince;
	this->overlay = Overlay::Dialogue;
	this->heldKeys.clear();
	this->holdLeft = this->holdRight = this->holdUp = this->holdDown = false;
	return true;
}

void arena::GameWorldScreen::updateDayNight(bool force)
{
	// Troca a música e o movimento das ruas quando anoitece e amanhece.
	if (!this->level.isExterior()) return;
	const bool night = clock::isNight(this->game.getCharacter().gameMinutes);
	if (night == this->nightMusic && !force) return;
	this->nightMusic = night;
	this->citizens.setNight(night);
	const Weather w = this->weather();
	this->game.playMusic(night ? "NIGHT.XMI" : w == Rain ? "RAINING.XMI" : w == Snow ? "SNOWING.XMI" : w == Overcast ? "OVERCAST.XMI" : "SUNNYDAY.XMI");
	this->showMessage(this->game.getText().get(night ? "City.NightFalls" : "City.DayBreaks"), 3.0);
}

std::string arena::GameWorldScreen::placeDirections(float fromX, float fromZ, int menu, const std::string &label) const
{
	// Porta mais próxima com esse *MENU (palácio: 11..13) e a direção pela bússola.
	const Localization &text = this->game.getText();
	float best = 1e9f, bx = 0, bz = 0;
	for (int cx = 0; cx < this->level.getWidth(); cx++)
		for (int cz = 0; cz < this->level.getDepth(); cz++)
		{
			const int m = this->level.cell(cx, cz).menu;
			if (m != menu && !(menu == 11 && m >= 11 && m <= 13)) continue;
			const float dx = cx + 0.5f - fromX, dz = cz + 0.5f - fromZ;
			const float d = dx * dx + dz * dz;
			if (d < best) { best = d; bx = dx; bz = dz; }
		}

	if (best >= 1e9f) return ui::format(text.get("Dialogue.NoPlace"), { this->locationName });
	const float distance = std::sqrt(best);
	if (distance < 4.0f) return ui::format(text.get("Dialogue.PlaceHere"), { label });
	// Bússola: norte = -X, leste = -Z (mesma conta do HUD).
	const float heading = std::atan2(-bz, -bx) * 180.0f / Pi;
	const int dir = (static_cast<int>(std::lround(heading / 45.0f)) % 8 + 8) % 8;
	const int steps = static_cast<int>(std::lround(distance * 2.0f / 5.0f)) * 5;
	return ui::format(text.get("Dialogue.PlaceAt"), { label, text.get("Dialogue.Dir" + std::to_string(dir)), std::to_string(steps) });
}

arena::GameWorldScreen::Weather arena::GameWorldScreen::weather() const
{
	// Igual para toda a cidade num mesmo dia: 60% limpo, 20% nublado, 20% chuva (neve nas
	// montanhas; no deserto quase nunca chove).
	if (!this->level.isExterior()) return Clear;
	return weatherFor(this->currentCity(), this->game.getCharacter().gameMinutes);
}

arena::GameWorldScreen::Weather arena::GameWorldScreen::weatherFor(const CityLocation *city, double gameMinutes)
{
	if (!city) return Clear;
	ArenaRandom random(city->seed() ^ (static_cast<uint32_t>(clock::dayIndex(gameMinutes)) * 2246822519u));
	random.next();
	const int roll = random.next() % 100;
	if (city->climate == CityLocation::Climate::Desert) return roll < 85 ? Clear : Overcast;
	if (roll < 60) return Clear;
	if (roll < 80) return Overcast;
	return city->climate == CityLocation::Climate::Mountain ? Snow : Rain;
}

void arena::GameWorldScreen::drawWeather(std::vector<uint32_t> &pixels, int width, int height)
{
	// Chuva (riscos inclinados) ou neve (flocos) por cima da cena, presos à tela; o movimento
	// da câmera desloca as gotas para dar sensação de profundidade.
	const Weather w = this->weather();
	if (w != Rain && w != Snow) return;
	const int u = std::max(1, height / 360);
	const float t = static_cast<float>(this->time);
	const float turn = this->yaw * width * 0.6f;
	const int count = w == Rain ? 420 : 260;
	const uint32_t color = w == Rain ? 0x00D2C8B4u : 0x00FFFAF5u;
	auto blend = [&](int px, int py, uint32_t alpha)
	{
		if (px < 0 || py < 0 || px >= width || py >= height) return;
		uint32_t &d = pixels[static_cast<size_t>(py) * width + px];
		auto mix = [&](int shift) { return (((((color >> shift) & 0xFF) * alpha) + (((d >> shift) & 0xFF) * (255 - alpha))) / 255) << shift; };
		d = 0xFF000000u | mix(16) | mix(8) | mix(0);
	};

	for (int i = 0; i < count; i++)
	{
		// Posição pseudoaleatória fixa por gota; profundidade muda tamanho e velocidade.
		uint32_t h = static_cast<uint32_t>(i) * 2654435761u;
		const float depth = 0.4f + ((h >> 8) & 0xFF) / 255.0f * 0.6f;
		const float speed = (w == Rain ? 1.9f : 0.18f) * height * depth;
		const float fx = ((h & 0xFFFF) / 65535.0f) * width;
		const float fy = (((h >> 16) & 0xFFFF) / 65535.0f) * height;
		float dropX = std::fmod(fx - turn * depth + (w == Snow ? std::sin(t * 1.3f + i) * 12.0f * u : 0.0f), static_cast<float>(width));
		float dropY = std::fmod(fy + t * speed, static_cast<float>(height));
		if (dropX < 0) dropX += width;
		if (w == Rain)
		{
			const int len = static_cast<int>((10 + 14 * depth) * u);
			const uint32_t alpha = static_cast<uint32_t>(70 + 80 * depth);
			for (int k = 0; k < len; k++) blend(static_cast<int>(dropX - k * 0.18f), static_cast<int>(dropY) + k, alpha);
		}
		else
		{
			const int size = std::max(1, static_cast<int>(2.5f * depth * u));
			for (int dy = 0; dy < size; dy++)
				for (int dx = 0; dx < size; dx++) blend(static_cast<int>(dropX) + dx, static_cast<int>(dropY) + dy, 210);
		}
	}
}

const arena::CityLocation *arena::GameWorldScreen::currentCity() const
{
	int province = 0, local = 0;
	for (const std::string *name : { &this->mifName, &this->returnMap })
		if (std::sscanf(name->c_str(), "CITY:%d:%d", &province, &local) == 2 || std::sscanf(name->c_str(), "WILD:%d:%d", &province, &local) == 2)
			return this->game.getData().getCity(province, local);
	return nullptr;
}

bool arena::GameWorldScreen::useCityGate(int cellX, int cellZ)
{
	// Portão da cidade: atravessa para o ermo (ou de volta), do outro lado do portão.
	const CityLocation *city = this->currentCity();
	if (!city) return false;
	const float dx = cellX + 0.5f - this->x, dz = cellZ + 0.5f - this->z;
	const int sx = std::fabs(dx) >= std::fabs(dz) ? (dx > 0 ? 1 : -1) : 0;
	const int sz = sx == 0 ? (dz > 0 ? 1 : -1) : 0;
	// Da cidade, o ermo abre com a janela padrão (cidade em WildCityOffset); do ermo, pela janela atual.
	const float shiftX = this->isWild() ? -this->wildCityX() : static_cast<float>(WildCityOffset);
	const float shiftZ = this->isWild() ? -this->wildCityZ() : static_cast<float>(WildCityOffset);
	char where[96];
	std::snprintf(where, sizeof(where), "%.2f,%.2f,%.1f", cellX + shiftX + 0.5f + sx * 1.6f, cellZ + shiftZ + 0.5f + sz * 1.6f, std::atan2(static_cast<float>(sz), static_cast<float>(sx)) * 180.0f / Pi);
	this->persist();
	this->game.playSound("OPENDOOR.VOC");
	this->game.setPendingSpawn(where);
	const std::string target = (this->isWild() ? "CITY:" : "WILD:") + std::to_string(city->province) + ":" + std::to_string(city->localId);
	this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, target));
	return true;
}

bool arena::GameWorldScreen::loadMap(const std::string &name)
{
	// Mapa de interior: .MIF pronto, andar de um .MIF pronto ou masmorra gerada.
	unsigned long long seed = 0;
	int width = 2, depth = 2, artifact = 0, levelIndex = 0;
	if (std::sscanf(name.c_str(), "DNG:%llu:%d:%d:%d:%d", &seed, &width, &depth, &artifact, &levelIndex) == 5)
	{
		MifFile mif;
		if (!generateDungeon(this->game.getAssets(), static_cast<uint32_t>(seed), width, depth, artifact != 0, mif, this->loadError)) return false;
		this->dungeonFloors = static_cast<int>(mif.levels.size());
		this->dungeonLevel = std::clamp(levelIndex, 0, static_cast<int>(mif.levels.size()) - 1);
		mif.startingLevel = this->dungeonLevel;
		return this->level.build(this->game.getAssets(), this->palette, mif, false, this->loadError);
	}

	if (name.rfind("MIFL:", 0) == 0)
	{
		const size_t colon = name.find(':', 5); // "MIFL:ARQ.MIF" ou "MIFL:ARQ.MIF:andar"
		const std::string file = name.substr(5, colon == std::string::npos ? std::string::npos : colon - 5);
		const auto data = this->game.getAssets().read(file);
		auto mif = data ? loadMif(*data, this->loadError) : std::nullopt;
		if (!mif || mif->levels.empty()) { if (this->loadError.empty()) this->loadError = file + " não encontrado."; return false; }
		this->dungeonLevel = colon == std::string::npos ? mif->startingLevel : std::atoi(name.c_str() + colon + 1);
		this->dungeonLevel = std::clamp(this->dungeonLevel, 0, static_cast<int>(mif->levels.size()) - 1);
		mif->startingLevel = this->dungeonLevel;
		return this->level.build(this->game.getAssets(), this->palette, *mif, false, this->loadError);
	}

	return this->level.load(this->game.getAssets(), this->palette, name, this->loadError);
}

void arena::GameWorldScreen::enterDungeon(const std::string &dungeon, const std::string &back, const std::string &name)
{
	this->persist();
	this->game.playSound("OPENDOOR.VOC");
	this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, "IN:" + dungeon + back + name));
}

void arena::GameWorldScreen::changeDungeonLevel(int delta)
{
	// Mesmo nome com o andar trocado; aparece junto da escada do outro lado.
	const size_t at = this->mifName.find('@');
	std::string head = this->mifName.substr(0, at), tail = at == std::string::npos ? std::string() : this->mifName.substr(at);
	if (head.rfind("IN:MIFL:", 0) == 0 && std::count(head.begin(), head.end(), ':') == 2) head += ":" + std::to_string(this->dungeonLevel);
	const size_t lastColon = head.rfind(':');
	if (lastColon == std::string::npos) return;
	head = head.substr(0, lastColon + 1) + std::to_string(this->dungeonLevel + delta);
	this->persist();
	this->game.playSound("CLICKS.VOC");
	this->game.setPendingSpawn(delta > 0 ? "STAIR:up" : "STAIR:down");
	this->game.setPendingNote(ui::format(this->game.getText().get(delta > 0 ? "Dungeon.Down" : "Dungeon.Up"), { std::to_string(this->dungeonLevel + delta + 1) }));
	this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, head + tail));
}

void arena::GameWorldScreen::spawnBeside(bool levelUp)
{
	// Célula livre ao lado da escada (de subida ou de descida), olhando para fora dela.
	const int dirX[4] = { 1, -1, 0, 0 }, dirZ[4] = { 0, 0, 1, -1 };
	for (int cx = 0; cx < this->level.getWidth(); cx++)
		for (int cz = 0; cz < this->level.getDepth(); cz++)
		{
			const Cell &c = this->level.cell(cx, cz);
			if (levelUp ? !c.isExit : !c.isLevelDown) continue;
			for (int d = 0; d < 4; d++)
			{
				if (this->level.blocksMovement(cx + dirX[d], cz + dirZ[d])) continue;
				this->x = cx + dirX[d] + 0.5f;
				this->z = cz + dirZ[d] + 0.5f;
				this->yaw = std::atan2(static_cast<float>(dirZ[d]), static_cast<float>(dirX[d]));
				return;
			}
		}
}

void arena::GameWorldScreen::travelToDungeon(const CityLocation &target, int days)
{
	// Masmorra com nome: entra direto; ao sair, aparece nos arredores da cidade mais próxima.
	const CityLocation *dungeon = &target;
	const CityLocation *nearest = nullptr;
	int bestDistance = 1 << 30;
	for (const CityLocation &c : this->game.getData().getCities())
	{
		if (c.type == CityLocation::Type::Dungeon || c.province != dungeon->province || c.name.empty()) continue;
		const int d = (c.globalX - dungeon->globalX) * (c.globalX - dungeon->globalX) + (c.globalY - dungeon->globalY) * (c.globalY - dungeon->globalY);
		if (d < bestDistance) { bestDistance = d; nearest = &c; }
	}

	this->persist();
	this->game.getCharacter().gameMinutes += days * clock::MinutesPerDay;
	const std::string back = nearest ? "@WILD:" + std::to_string(nearest->province) + ":" + std::to_string(nearest->localId) + "@-1@-1@0@" : "@" + this->mifName + "@-1@-1@0@";
	std::string map;
	const std::string premade = std::to_string(dungeon->dungeonSeed()).substr(0, 8) + ".MIF";
	if (dungeon->mainQuest() && this->game.getAssets().exists(premade)) map = "MIFL:" + premade;
	else
	{
		// A masmorra do artefato procurado tem 4 andares, como no original.
		const ArtifactHunt &hunt = this->game.getCharacter().hunt;
		const bool artifactHere = hunt.active() && artifact::dungeon(this->game.getData(), hunt.artifact) == dungeon;
		map = "DNG:" + std::to_string(dungeon->dungeonSeed()) + ":2:2:" + (artifactHere ? "1" : "0") + ":0";
	}
	this->game.setPendingNote(ui::format(this->game.getText().get(days == 1 ? "Travel.ArrivedDungeonDay" : "Travel.ArrivedDungeon"),
		days == 1 ? std::initializer_list<std::string>{ dungeon->name } : std::initializer_list<std::string>{ std::to_string(days), dungeon->name }));
	this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, "IN:" + map + back + dungeon->name));
}

void arena::GameWorldScreen::shiftWilderness()
{
	// Perto da borda da janela (a 1,5 bloco dela), o ermo é remontado com o bloco do jogador no meio, e ele
	// continua no mesmo ponto do mundo. Só a borda do ermo inteiro (64x64 blocos) é um limite de verdade.
	constexpr float Margin = 1.5f * WildChunk;
	const float size = static_cast<float>(WildWindow * WildChunk);
	if (this->wildShifting || (this->x > Margin && this->z > Margin && this->x < size - Margin && this->z < size - Margin)) return;
	const CityLocation *city = this->currentCity();
	if (!city) return;
	const int blockCol = static_cast<int>(this->z) / WildChunk + this->wildFirstX, blockRow = static_cast<int>(this->x) / WildChunk + this->wildFirstY;
	const int firstX = std::clamp(blockCol - WildWindow / 2, 0, WildLastFirst), firstY = std::clamp(blockRow - WildWindow / 2, 0, WildLastFirst);
	if (firstX == this->wildFirstX && firstY == this->wildFirstY) return;

	char where[96];
	std::snprintf(where, sizeof(where), "%.2f,%.2f,%.1f", this->x + (this->wildFirstY - firstY) * WildChunk, this->z + (this->wildFirstX - firstX) * WildChunk, this->yaw * 180.0f / Pi);
	this->wildShifting = true;
	this->persist();
	this->game.setPendingSpawn(where);
	this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, "WILD:" + std::to_string(city->province) + ":" + std::to_string(city->localId)
		+ ":" + std::to_string(firstX) + ":" + std::to_string(firstY)));
}

void arena::GameWorldScreen::updateWildCreatures(double dt)
{
	// Ermo: criaturas surgem fora da vista, a 18..28 passos, e somem quando ficam longe. De dia
	// poucas e só bichos do campo; à noite mais e com mortos-vivos. Limitadas ao nível do jogador.
	this->wildSpawnTimer -= dt;
	if (this->wildSpawnTimer > 0.0) return;
	this->wildSpawnTimer = 3.0;
	this->creatures.vanishFarFrom(this->x, this->z, 45.0f);

	const double minutes = this->game.getCharacter().gameMinutes;
	const bool night = clock::isNight(minutes);
	const int cap = night ? 6 : 2;
	if (this->creatures.activeNear(this->x, this->z, 35.0f) >= cap) return;
	std::mt19937 &rng = this->game.getRandom();
	if (std::uniform_int_distribution<int>(0, 99)(rng) >= (night ? 55 : 25)) return;

	const CityLocation *city = this->currentCity();
	const bool mountain = city && city->climate == CityLocation::Climate::Mountain;
	static const std::vector<int> Day = { 0, 1, 2, 3, 5, 8, 13 };
	static const std::vector<int> Night = { 0, 1, 3, 5, 6, 8, 9, 10, 11, 12, 13, 14 };
	std::vector<int> choices;
	const int playerLevel = this->game.getCharacter().level;
	for (int kind : night ? Night : Day)
	{
		if (kind == 3 && mountain) kind = 4; // lobos da neve nas montanhas
		const CreatureDefinition *def = this->creatures.definition(kind);
		if (def && def->level <= playerLevel + 2) choices.push_back(kind);
	}
	if (choices.empty()) return;
	const int kind = choices[std::uniform_int_distribution<size_t>(0, choices.size() - 1)(rng)];

	// Lugar livre longe da vista (atrás ou dos lados) e fora das muralhas da cidade.
	const float fx = std::cos(this->yaw), fz = std::sin(this->yaw);
	for (int tries = 0; tries < 24; tries++)
	{
		const float angle = std::uniform_real_distribution<float>(0.0f, 2.0f * Pi)(rng);
		const float distance = std::uniform_real_distribution<float>(18.0f, 28.0f)(rng);
		const float sx = this->x + std::cos(angle) * distance, sz = this->z + std::sin(angle) * distance;
		if ((std::cos(angle) * fx + std::sin(angle) * fz) > 0.5f) continue; // na frente: aparece do nada
		const int cx = static_cast<int>(std::floor(sx)), cz = static_cast<int>(std::floor(sz));
		if (!this->level.inBounds(cx, cz) || this->level.blocksMovement(cx, cz) || this->level.cell(cx, cz).chasm) continue;
		if (std::fabs(sx - this->wildCityX() - WildChunk) < WildChunk && std::fabs(sz - this->wildCityZ() - WildChunk) < WildChunk) continue;
		this->creatures.spawn(this->game.getAssets(), this->palette, kind, cx + 0.5f, cz + 0.5f, rng);
		return;
	}
}

void arena::GameWorldScreen::openVision(const std::string &title, const std::string &body)
{
	this->visionTitle = title;
	this->visionText = body;
	this->overlay = Overlay::None; // o sonho no quarto da taverna fecha o balcão
	this->heldKeys.clear();
	this->holdLeft = this->holdRight = this->holdUp = this->holdDown = false;
}

void arena::GameWorldScreen::tryDream()
{
	// Ria aparece no sonho com o lugar do próximo pedaço (depois do oitavo, o caminho até Tharn).
	CharacterDraft &character = this->game.getCharacter();
	if (!quest::dreamDue(character)) return;
	const Localization &text = this->game.getText();
	const std::string body = ui::format(text.get("Quest.Dream" + std::to_string(character.staffPieces)), { character.name, character.name, character.name });
	character.questStep = 1;
	const int nextProvince = quest::targetProvince(character);
	const CityLocation *nextPiece = nextProvince >= 0 ? this->game.getData().getCity(nextProvince, quest::PieceDungeon) : nullptr;
	quest::note(character, nextPiece ? ui::format(text.get("Journal.Dream"), { nextPiece->name }) : text.get("Journal.DreamFinal"), "Quest.Dream" + std::to_string(character.staffPieces));
	this->openVision(text.get("Quest.DreamTitle"), body);
	// Ria aparece (VISION.FLC); no primeiro sonho ela mostra o Cajado do Caos (CHAOSVSN.FLC).
	std::vector<std::string> scenes{ "VISION.FLC" };
	if (character.staffPieces == 0) scenes.push_back("CHAOSVSN.FLC");
	this->cutscene.start(this->game, scenes);
	this->persist();
}

bool arena::GameWorldScreen::tryTakeQuestItem(size_t sprite)
{
	// *ITEM 13 dos mapas prontos: o item da pista (masmorra 33) ou o pedaço do cajado (32).
	CharacterDraft &character = this->game.getCharacter();
	const Localization &text = this->game.getText();
	const int province = quest::targetProvince(character);
	const CityLocation *piece = province >= 0 ? this->game.getData().getCity(province, quest::PieceDungeon) : nullptr;
	const CityLocation *clue = province >= 0 ? this->game.getData().getCity(province, quest::ClueDungeon) : nullptr;
	auto inside = [&](const CityLocation *dungeon) { return dungeon && this->mifName.find("MIFL:" + quest::premadeMif(*dungeon)) != std::string::npos; };

	if (this->mifName.find(std::string("MIFL:") + quest::PalaceMif) != std::string::npos)
	{
		// A Joia do Fogo: tocada com o Cajado completo, leva junto a força vital de Tharn.
		if (character.staffPieces < quest::PieceCount || character.questStep >= 4)
		{
			this->showMessage(text.get(character.questStep >= 4 ? "Quest.NothingLeft" : "Quest.JewelNeedsStaff"), 4.0);
			return true;
		}
		this->removeSpriteAt(sprite);
		const auto &all = this->creatures.getCreatures();
		for (size_t i = 0; i < all.size(); i++)
		{
			if (all[i].definition != quest::TharnCreature || !this->creatures.isAlive(static_cast<int>(i))) continue;
			const int maxHp = all[i].maxHp;
			if (this->creatures.damage(static_cast<int>(i), maxHp + 1)) this->rewardKill(static_cast<int>(i), maxHp);
		}
		character.questStep = 4;
		quest::note(character, text.get("Journal.End"), "Quest.Ending");
		this->game.playSound("FANFARE2.VOC");
		this->openVision(text.get("Quest.EndTitle"), ui::format(text.get("Quest.Ending"), { character.name }));
		this->cutscene.start(this->game, { "JAGARDTH.FLC", "END01.FLC", "END02.FLC", "NUKING.FLC" });
		this->persist();
		return true;
	}

	if (inside(clue) && character.questStep >= 1 && character.questStep <= 2)
	{
		this->removeSpriteAt(sprite);
		const std::string item = text.get("Quest.Item" + std::to_string(character.staffPieces));
		const CityLocation *giver = nullptr;
		for (const CityLocation &c : this->game.getData().getCities()) if (c.globalId == character.questGiver) giver = &c;
		this->game.playSound("FANFARE2.VOC");
		if (giver)
		{
			character.questStep = 3; // levar ao governante, que revela a masmorra do pedaço
			quest::note(character, ui::format(text.get("Journal.ClueHeld"), { item, clue->name, giver->name }));
			this->openVision(text.get("Quest.ClueTitle"), ui::format(text.get("Quest.ClueHeld"), { item, giver->name }));
		}
		else
		{
			character.questStep = 4; // a masmorra do pedaço aparece no mapa de viagem
			quest::note(character, ui::format(text.get("Journal.Clue"), { item, clue->name, piece ? piece->name : std::string() }));
			this->openVision(text.get("Quest.ClueTitle"), ui::format(text.get("Quest.ClueFound"), { item, piece ? piece->name : std::string() }));
		}
		this->persist();
		return true;
	}

	if (inside(piece) && character.questStep >= 1)
	{
		this->removeSpriteAt(sprite);
		character.staffPieces++;
		character.questStep = 0; // próximo sonho ao descansar
		quest::note(character, ui::format(text.get("Journal.Piece"), { text.get("Journal.Ordinal" + std::to_string(character.staffPieces)), piece->name }), "Quest.Piece" + std::to_string(character.staffPieces));
		this->game.playSound("FANFARE2.VOC");
		this->openVision(text.get("Quest.PieceTitle"), text.get("Quest.Piece" + std::to_string(character.staffPieces)) + "\n\n"
			+ ui::format(text.get("Quest.PieceCount"), { std::to_string(character.staffPieces) }));
		this->persist();
		return true;
	}

	this->showMessage(text.get(inside(piece) || inside(clue) ? "Quest.NotYet" : "Quest.NothingLeft"));
	return true;
}

void arena::GameWorldScreen::drawVision(kit::Painter &painter, int width, int height)
{
	// Painel de pergaminho no estilo do fim de capítulo, com um botão para seguir.
	namespace col = kit::color;
	const int u = painter.unit();
	const Fonts &fonts = this->game.getFonts();
	painter.fill(Rect{ 0, 0, width, height }, hud::rgb(12, 8, 4, 215));

	// Altura pelo tamanho do texto (estimativa de ~78 letras por linha), até 300 unidades.
	const int lines = static_cast<int>(this->visionText.size()) / 78 + 1 + static_cast<int>(std::count(this->visionText.begin(), this->visionText.end(), '\n'));
	const int w = std::min(width - 16 * u, 460 * u), h = std::min({ height - 16 * u, 300 * u, (100 + lines * 11) * u });
	const Rect box{ (width - w) / 2, (height - h) / 2, w, h };
	painter.woodPanel(box, 245);
	painter.text(fonts.a, this->visionTitle, box.x + w / 2, box.y + 10 * u, col::GoldBright, TextAlign::Center);
	painter.divider(box.x + 14 * u, box.y + 32 * u, w - 28 * u, col::GoldDim);

	const Rect page{ box.x + 14 * u, box.y + 40 * u, w - 28 * u, h - 40 * u - 34 * u };
	painter.parchmentPanel(page);
	painter.paragraph(fonts.arena, this->visionText, Rect{ page.x + 12 * u, page.y + 10 * u, page.w - 24 * u, page.h - 20 * u }, col::Ink);

	if (this->offerPending())
	{
		// Proposta de trabalho ou de informação: aceitar (Enter) ou recusar (Esc).
		const Rect yes{ box.x + w / 2 - 136 * u, box.y + h - 26 * u, 130 * u, 18 * u }, no{ box.x + w / 2 + 6 * u, box.y + h - 26 * u, 130 * u, 18 * u };
		painter.button(yes, this->game.getText().get("Jobs.Accept"), kit::hit(yes, this->uiMouseX, this->uiMouseY), false);
		painter.button(no, this->game.getText().get("Jobs.Decline"), kit::hit(no, this->uiMouseX, this->uiMouseY), false);
		this->screenButtons.push_back({ yes, 'y' });
		this->screenButtons.push_back({ no, 'n' });
		return;
	}
	const Rect r{ box.x + (w - 130 * u) / 2, box.y + h - 26 * u, 130 * u, 18 * u };
	painter.button(r, this->game.getText().get("Quest.Continue"), kit::hit(r, this->uiMouseX, this->uiMouseY), false);
	this->screenButtons.push_back({ r, 'v' });
}

void arena::GameWorldScreen::palaceArrival()
{
	// Palácio Imperial: Tharn recebe o jogador (JAGAR.FLC); no último andar, a câmara da Joia (WALK.FLC).
	if (this->mifName.find(std::string("MIFL:") + quest::PalaceMif) == std::string::npos) return;
	CharacterDraft &character = this->game.getCharacter();
	if (character.staffPieces < quest::PieceCount || character.questStep >= 4) return;
	const Localization &text = this->game.getText();
	if (character.questStep < 2)
	{
		character.questStep = 2;
		quest::note(character, text.get("Journal.Palace"), "Quest.TharnTaunt");
		this->openVision(text.get("Quest.TharnTitle"), ui::format(text.get("Quest.TharnTaunt"), { character.name }));
		this->cutscene.start(this->game, { "JAGAR.FLC" });
	}
	else if (character.questStep == 2 && this->dungeonLevel == quest::JewelLevel)
	{
		character.questStep = 3;
		quest::note(character, text.get("Journal.Jewel"), "Quest.JewelChamber");
		this->openVision(text.get("Quest.JewelTitle"), text.get("Quest.JewelChamber"));
		this->cutscene.start(this->game, { "WALK.FLC" });
	}
}

void arena::GameWorldScreen::openJournal()
{
	this->journalMenu.open(quest::objective(this->game.getCharacter(), this->game.getData(), this->game.getText(), this->isPrison() && !this->escaped), jobs::objective(this->game));
	this->overlay = Overlay::Journal;
	this->heldKeys.clear();
	this->holdLeft = this->holdRight = this->holdUp = this->holdDown = false;
}

bool arena::GameWorldScreen::tryAttackCitizen()
{
	const int index = this->citizens.findFacing(this->x, this->z, this->yaw, 1.45f);
	if (index < 0) return false;
	const auto [cx, cz] = this->citizens.position(index);
	this->playAt("EHIT.VOC", cx, cz);
	this->playAt(this->citizens.isMale(index) ? "MDIE.VOC" : "FDIE.VOC", cx, cz);
	this->citizens.remove(index);
	const Localization &text = this->game.getText();
	this->showMessage(ui::format(text.get("World.CitizenKilled"), { this->locationName }), 4.0);
	this->callGuards();
	// A guarda lembra do crime por alguns dias: voltar à cidade nesse tempo traz os guardas de novo.
	CharacterDraft &hero = this->game.getCharacter();
	if (const CityLocation *city = this->currentCity()) hero.wanted[city->globalId] = hero.gameMinutes + WantedDays * clock::MinutesPerDay;
	this->persist();
	return true;
}

void arena::GameWorldScreen::callGuards()
{
	// Guardas da cidade (GUARD0@.CFA do original), mais fortes com o nível do jogador. Surgem de
	// 6 a 11 passos, nas ruas em volta, já em alerta.
	const CharacterDraft &character = this->game.getCharacter();
	if (this->guardDefinition < 0)
	{
		CreatureDefinition guard;
		guard.originalName = "Guard";
		guard.animFilename = "GUARD0@.CFA";
		guard.level = std::max(3, character.level + 2);
		guard.minHp = 25 + 8 * guard.level;
		guard.maxHp = guard.minHp + 20;
		guard.minDamage = 2 + guard.level / 2;
		guard.maxDamage = 6 + guard.level;
		guard.baseExperience = 50 * guard.level;
		guard.experienceMultiplier = 0;
		this->guardDefinition = this->creatures.addDefinition(guard);
		this->definitionText[this->guardDefinition] = 24; // Creatures.Name24 = Guarda
	}

	std::mt19937 &rng = this->game.getRandom();
	int spawned = 0;
	for (int tries = 0; tries < 80 && spawned < 3; tries++)
	{
		const float angle = std::uniform_real_distribution<float>(0.0f, 2.0f * Pi)(rng);
		const float distance = std::uniform_real_distribution<float>(6.0f, 11.0f)(rng);
		const int gx = static_cast<int>(std::floor(this->x + std::cos(angle) * distance)), gz = static_cast<int>(std::floor(this->z + std::sin(angle) * distance));
		if (!this->level.inBounds(gx, gz) || this->level.blocksMovement(gx, gz) || this->level.cell(gx, gz).chasm) continue;
		const int index = this->creatures.spawn(this->game.getAssets(), this->palette, this->guardDefinition, gx + 0.5f, gz + 0.5f, rng);
		if (index < 0) break; // sem animação
		this->creatures.wake(index);
		spawned++;
	}

	if (spawned > 0) this->game.playSound("HALT.VOC");
}

void arena::GameWorldScreen::shootArrow()
{
	// Flecha na direção do olhar (com a inclinação da câmera); flechas são infinitas, como no original.
	const CharacterDraft &character = this->game.getCharacter();
	std::mt19937 &rng = this->game.getRandom();
	Arrow arrow;
	const float cosPitch = std::cos(this->pitch);
	arrow.dx = std::cos(this->yaw) * cosPitch;
	arrow.dz = std::sin(this->yaw) * cosPitch;
	arrow.dy = std::sin(this->pitch);
	arrow.x = this->x + arrow.dx * 0.3f;
	arrow.z = this->z + arrow.dz * 0.3f;
	arrow.y = EyeHeight + this->eyeOffset - 0.1f;
	const int roll = this->weapon.minDamage + static_cast<int>(rng() % static_cast<uint32_t>(this->weapon.maxDamage - this->weapon.minDamage + 1));
	arrow.damage = std::max(1, roll + damageBonusFor(character.attributes[0] + this->fortifyBonus(0)));
	arrow.hitBonus = hitBonusFor(character.attributes[3] + this->fortifyBonus(3)) + this->weaponMaterialBonus / 5;
	this->arrows.push_back(arrow);

	if (this->arrowTextures.empty())
	{
		const std::vector<IndexedImage> frames = this->game.loadFrames("ARROW02.CFA");
		for (size_t i = 0; i < frames.size(); i++) this->arrowTextures.push_back(toTexture(frames[i], this->palette, "ARROW02.CFA", static_cast<int>(i), static_cast<int>(frames.size())));
	}
}

void arena::GameWorldScreen::updateArrows(double dt)
{
	constexpr float Speed = 18.0f, Range = 30.0f;
	const Localization &text = this->game.getText();
	const float ceiling = this->level.getWallHeight();
	std::mt19937 &rng = this->game.getRandom();
	for (size_t i = 0; i < this->arrows.size();)
	{
		Arrow &a = this->arrows[i];
		bool done = false;
		float remaining = Speed * static_cast<float>(dt);
		while (!done && remaining > 0.0f)
		{
			const float step = std::min(0.1f, remaining);
			remaining -= step;
			a.x += a.dx * step; a.y += a.dy * step; a.z += a.dz * step;
			a.travelled += step;

			int hit = -1;
			const auto &all = this->creatures.getCreatures();
			for (size_t c = 0; c < all.size() && hit < 0; c++)
			{
				const float dx = all[c].x - a.x, dz = all[c].z - a.z;
				if (this->creatures.isAlive(static_cast<int>(c)) && dx * dx + dz * dz < 0.4f * 0.4f && a.y < 1.1f) hit = static_cast<int>(c);
			}

			if (hit >= 0)
			{
				// Mesmo teste de acerto do corpo a corpo.
				const CreatureDefinition &def = this->creatures.definitionOf(hit);
				const CharacterDraft &hero = this->game.getCharacter();
				const int chance = meleeHitChance256(hero.level, hero.raceIndex, a.hitBonus, def.level, -1, 0, 0);
				const Creature &victim = all[static_cast<size_t>(hit)];
				if (static_cast<int>(rng() % 256u) >= chance) this->showMessage(ui::format(text.get("World.PlayerMiss"), { this->creatureRef(hit, false) }), 2.0);
				else
				{
					const int maxHp = victim.maxHp;
					this->playAt("ARROWHT.VOC", victim.x, victim.z);
					if (this->creatures.damage(hit, a.damage)) this->rewardKill(hit, maxHp);
					else this->showMessage(ui::format(text.get("World.PlayerHit"), { this->creatureRef(hit, false), std::to_string(a.damage) }), 2.0);
				}
				done = true;
				break;
			}

			const int cx = static_cast<int>(std::floor(a.x)), cz = static_cast<int>(std::floor(a.z));
			const bool wall = this->level.blocksMovement(cx, cz) && this->level.cell(cx, cz).type != CellType::Empty;
			if (wall || a.y < this->level.floorHeightAt(cx, cz) || a.y > ceiling || a.travelled > Range) done = true;
		}

		if (done) this->arrows.erase(this->arrows.begin() + static_cast<std::ptrdiff_t>(i));
		else i++;
	}
}

void arena::GameWorldScreen::drawArrows()
{
	if (this->arrowTextures.empty()) return;
	const Vec3 right = this->renderer.getRight();
	for (const Arrow &a : this->arrows)
	{
		const Texture &texture = this->arrowTextures[static_cast<size_t>(static_cast<int>(a.travelled * 3.0f) % static_cast<int>(this->arrowTextures.size()))];
		const float h = 0.08f;
		const Vec3 v[4] = { { a.x - right.x * h, a.y + h, a.z - right.z * h }, { a.x + right.x * h, a.y + h, a.z + right.z * h },
			{ a.x + right.x * h, a.y - h, a.z + right.z * h }, { a.x - right.x * h, a.y - h, a.z - right.z * h } };
		const float uv[4][2] = { { 0, 0 }, { 0.999f, 0 }, { 0.999f, 0.999f }, { 0, 0.999f } };
		this->renderer.drawQuad(v, uv, texture, true, Surface{});
	}
}
