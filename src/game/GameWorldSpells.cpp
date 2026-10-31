// Magia na exploração: conjurar o feitiço preparado (Q), animação das mãos (SPELL.IMG e
// FIRE/ICE/SHOCK/MAGIC/SPARKLE.CFA), projéteis luminosos, áreas e efeitos no jogador e nas criaturas.

#include <algorithm>
#include <map>
#include <cmath>

#include "../render/Ui.h"
#include "Artifacts.h"
#include "GameWorldScreen.h"

namespace
{
	using arena::Vec3;

	constexpr float EyeHeight = 0.48f;      // mesma altura dos olhos da GameWorldScreen
	constexpr float ProjectileSpeed = 7.0f; // voxels por segundo
	constexpr float ProjectileRange = 24.0f;
	constexpr float AreaRadius = 1.7f;      // explosão de bola de fogo, tempestades
	constexpr float CasterAreaRadius = 2.5f;
	constexpr double CastRelease = 0.25;    // instante em que o feitiço sai das mãos
	constexpr double CastLength = 0.7;
	constexpr double BurstLength = 0.5;

	// Cor da luz e do projétil por elemento (fogo, frio, veneno, choque, magia, nenhum).
	constexpr float ElementColor[6][3] = { { 1.4f, 0.7f, 0.25f }, { 0.6f, 0.9f, 1.4f }, { 0.5f, 1.3f, 0.35f },
		{ 0.9f, 0.8f, 1.5f }, { 1.1f, 0.5f, 1.4f }, { 1.2f, 1.15f, 0.9f } };

	// Animação de conjuração por elemento: fogo, gelo, choque, magia; feitiços em si mesmo usam o brilho.
	int castAnimationFor(const arena::Spell &spell)
	{
		if (!spell.harmful()) return 4;
		switch (spell.element)
		{
		case arena::SpellElement::Fire: return 0;
		case arena::SpellElement::Cold: return 1;
		case arena::SpellElement::Shock: return 2;
		default: return 3;
		}
	}

	// Esfera luminosa (centro quase branco, borda na cor do elemento) para o projétil.
	arena::Texture makeOrb(const float (&tint)[3])
	{
		constexpr int Size = 32;
		arena::Texture texture;
		texture.width = texture.height = Size;
		texture.pixels.resize(Size * Size);
		texture.hasTransparency = true;
		texture.scale = 2; // filtro suave
		texture.id = arena::newTextureId();
		for (int y = 0; y < Size; y++)
		{
			for (int x = 0; x < Size; x++)
			{
				const float dx = (x + 0.5f - Size / 2.0f) / (Size / 2.0f), dy = (y + 0.5f - Size / 2.0f) / (Size / 2.0f);
				const float d = std::sqrt(dx * dx + dy * dy);
				uint32_t &p = texture.pixels[static_cast<size_t>(y * Size + x)];
				if (d > 0.72f) { p = 0; continue; } // borda recortada: sem halo escuro
				const float core = std::clamp(1.0f - d * 1.8f, 0.0f, 1.0f);
				const float glow = std::sqrt(std::clamp(1.0f - d, 0.0f, 1.0f));
				auto channel = [&](float tintValue) { return static_cast<uint32_t>(std::clamp((tintValue * 200.0f * glow + 255.0f * core), 0.0f, 255.0f)); };
				p = 0xFF000000u | (channel(tint[2]) << 16) | (channel(tint[1]) << 8) | channel(tint[0]);
			}
		}

		return texture;
	}
}

void arena::GameWorldScreen::loadSpellArt()
{
	const char *files[5] = { "FIRE.CFA", "ICE.CFA", "SHOCK.CFA", "MAGIC.CFA", "SPARKLE.CFA" };
	for (size_t i = 0; i < 5; i++)
	{
		ScreenAnimation &animation = this->castAnimations[i];
		if (const auto data = this->game.getAssets().read(files[i]))
		{
			const std::vector<IndexedImage> frames = loadCfa(*data);
			for (size_t f = 0; f < frames.size(); f++)
			{
				const IndexedImage &frame = frames[f];
				animation.textures.push_back(toTexture(frame, this->palette, files[i], static_cast<int>(f), static_cast<int>(frames.size())));
				animation.offsets.emplace_back(frame.offsetX, frame.offsetY);
			}
		}
	}

	const IndexedImage hands = this->game.loadImage("SPELL.IMG");
	this->castHands = toTexture(hands, hands.palette.value_or(this->palette), "SPELL.IMG", 0, 1);
	for (size_t e = 0; e < this->orbTextures.size(); e++) this->orbTextures[e] = makeOrb(ElementColor[e]);
}

bool arena::GameWorldScreen::hasPlayerEffect(SpellEffectType type) const
{
	return std::any_of(this->playerEffects.begin(), this->playerEffects.end(), [type](const PlayerEffect &e) { return e.type == type; });
}

int arena::GameWorldScreen::fortifyBonus(int attribute) const
{
	int bonus = 0;
	for (const PlayerEffect &e : this->playerEffects)
	{
		if (e.type == SpellEffectType::Fortify && e.sub == attribute) bonus += e.magnitude;
	}

	return bonus;
}

void arena::GameWorldScreen::castReadySpell()
{
	if (this->casting || this->health <= 0) return;
	const Localization &text = this->game.getText();
	const CharacterDraft &c = this->game.getCharacter();
	if (c.spells.empty())
	{
		this->showMessage(text.get("Items.NoSpellbook"), 2.5);
		return;
	}

	const Spell *spell = this->game.getData().getSpells().find(c.readySpell);
	if (!spell)
	{
		this->showMessage(text.get("Spells.NoneReady"), 3.0);
		return;
	}

	// Poder de artefato: grátis, mas precisa recarregar entre um uso e outro.
	if (spell->artifactPower() && this->powerRecharge[spell->index] > 0.0)
	{
		this->showMessage(ui::format(text.get("Artifacts.Recharging"), { spellName(this->game, *spell), std::to_string(static_cast<int>(std::ceil(this->powerRecharge[spell->index]))) }), 2.5);
		return;
	}
	if (spell->artifactPower()) this->powerRecharge[spell->index] = artifact::PowerRecharge;
	const int cost = spell->castingCost();
	if (this->spellPoints < cost)
	{
		this->showMessage(ui::format(text.get("Spells.NotEnough"), { spellName(this->game, *spell), std::to_string(cost) }), 2.5);
		return;
	}

	this->spellPoints -= cost;
	this->casting = true;
	this->castReleased = false;
	this->castTime = 0.0;
	this->castingSpell = spell->index;
	this->swinging = false;
	this->game.playSound(spell->harmful() ? "SLOWBALL.VOC" : "BURST5.VOC", 0.8f);
}

void arena::GameWorldScreen::releaseSpell()
{
	const Spell *spell = this->game.getData().getSpells().find(this->castingSpell);
	if (!spell) return;
	const Localization &text = this->game.getText();

	switch (spell->target)
	{
	case SpellTarget::Touch:
	{
		const int target = this->creatures.findTarget(this->x, this->z, this->yaw, 1.6f, 0.72f);
		if (target < 0)
		{
			this->showMessage(text.get("Spells.Fizzle"), 2.0);
			return;
		}

		const Creature &c = this->creatures.getCreatures()[static_cast<size_t>(target)];
		this->bursts.push_back(Burst{ c.x, EyeHeight * 0.8f, c.z, 0.0, static_cast<int>(spell->element) });
		this->applySpellToCreature(*spell, target);
		return;
	}
	case SpellTarget::AreaCaster:
	{
		this->bursts.push_back(Burst{ this->x, EyeHeight * 0.6f, this->z, 0.0, static_cast<int>(spell->element) });
		this->game.playSound("EXPLODE.VOC", 0.8f);
		bool any = false;
		const auto &all = this->creatures.getCreatures();
		for (size_t i = 0; i < all.size(); i++)
		{
			const float dx = all[i].x - this->x, dz = all[i].z - this->z;
			if (!this->creatures.isAlive(static_cast<int>(i)) || dx * dx + dz * dz > CasterAreaRadius * CasterAreaRadius) continue;
			this->applySpellToCreature(*spell, static_cast<int>(i));
			any = true;
		}

		if (!any) this->showMessage(ui::format(text.get("Spells.Cast"), { spellName(this->game, *spell) }), 2.0);
		return;
	}
	case SpellTarget::Ranged:
	case SpellTarget::AreaRanged:
	{
		// Sai das mãos, um pouco abaixo dos olhos, na direção da mira.
		const float cp = std::cos(this->pitch);
		const float dx = cp * std::cos(this->yaw), dy = std::sin(this->pitch), dz = cp * std::sin(this->yaw);
		Projectile projectile;
		projectile.x = this->x + dx * 0.35f;
		projectile.y = EyeHeight + this->eyeOffset - 0.08f + dy * 0.35f;
		projectile.z = this->z + dz * 0.35f;
		projectile.dx = dx; projectile.dy = dy; projectile.dz = dz;
		projectile.spell = spell->index;
		this->projectiles.push_back(projectile);
		return;
	}
	default:
		this->applySpellToSelf(*spell);
		return;
	}
}

void arena::GameWorldScreen::spellImpact(const Spell &spell, float px, float py, float pz, int directHit)
{
	this->bursts.push_back(Burst{ px, py, pz, 0.0, static_cast<int>(spell.element) });
	const bool area = spell.target == SpellTarget::AreaRanged;
	this->playAt(area ? "EXPLODE.VOC" : (spell.element == SpellElement::Shock ? "THUNDER.VOC" : "NHIT.VOC"), px, pz, area ? 1.0f : 0.8f);

	if (area)
	{
		const auto &all = this->creatures.getCreatures();
		for (size_t i = 0; i < all.size(); i++)
		{
			const float dx = all[i].x - px, dz = all[i].z - pz;
			if (this->creatures.isAlive(static_cast<int>(i)) && dx * dx + dz * dz <= AreaRadius * AreaRadius) this->applySpellToCreature(spell, static_cast<int>(i));
		}
	}
	else if (directHit >= 0)
	{
		this->applySpellToCreature(spell, directHit);
	}
}

void arena::GameWorldScreen::rewardKill(int target, int maxHp)
{
	const Localization &text = this->game.getText();
	const Creature &victim = this->creatures.getCreatures()[static_cast<size_t>(target)];
	const CreatureDefinition &def = this->creatures.definitionOf(target);
	this->playAt("BODYFALL.VOC", victim.x, victim.z);
	const int gained = def.baseExperience + maxHp * def.experienceMultiplier;
	this->experience += gained;
	this->corpseLoot[target] = generateLoot(LootSource::Creature, victim.definition, this->game.getData().getItems(), this->game.getRandom());
	this->showMessage(ui::format(text.get("World.CreatureKilled"), { this->creatureRef(target, false), std::to_string(gained) }), 3.0);

	// A fera caçada para o governante: falta voltar ao palácio para receber.
	CharacterDraft &hero = this->game.getCharacter();
	if (target == this->jobCreature && hero.job.kind == SideJob::Kind::Monster && hero.job.step == 0)
	{
		this->jobCreature = -1;
		this->game.playSound("FANFARE2.VOC");
		this->openVision(text.get("Jobs.FoundTitle"), jobs::targetDone(this->game));
	}
	this->persist();
}

void arena::GameWorldScreen::applySpellToCreature(const Spell &spell, int creature)
{
	if (!this->creatures.isAlive(creature)) return;
	const Localization &text = this->game.getText();
	std::mt19937 &rng = this->game.getRandom();
	const int Level = this->game.getCharacter().level;

	auto hurt = [&](int amount)
	{
		const Creature &victim = this->creatures.getCreatures()[static_cast<size_t>(creature)];
		const int maxHp = victim.maxHp;
		this->playAt("EHIT.VOC", victim.x, victim.z, 0.8f);
		if (this->creatures.damage(creature, std::max(1, amount))) this->rewardKill(creature, maxHp);
		else this->showMessage(ui::format(text.get("Spells.CreatureHit"), { this->creatureRef(creature, true), std::to_string(amount) }), 2.5);
	};

	for (const SpellEffect &effect : spell.effects)
	{
		if (!effect.valid() || !this->creatures.isAlive(creature)) continue;
		switch (static_cast<SpellEffectType>(effect.type))
		{
		case SpellEffectType::Damage:
			if (effect.sub == 0) hurt(spellAmount(effect, Level, rng));
			else this->showMessage(ui::format(text.get("Spells.CreatureResisted"), { this->creatureRef(creature, true) }), 2.0);
			break;
		case SpellEffectType::ContinuousDamage:
			this->creatureDots.push_back(CreatureDot{ creature, spellRounds(effect, Level) * SecondsPerRound, 0.0, std::max(1, spellAmount(effect, Level, rng)) });
			this->showMessage(ui::format(text.get("Spells.CreaturePoisoned"), { this->creatureRef(creature, true) }), 2.5);
			break;
		case SpellEffectType::Cause:
		{
			const bool success = static_cast<int>(rng() % 100u) < spellChance(effect, Level);
			if (effect.sub == 1 && success)
			{
				// Veneno: %2 pontos a cada %3 rodada(s), por %5 rodadas por nível.
				this->creatureDots.push_back(CreatureDot{ creature, effect.params[5] * Level * SecondsPerRound, 0.0, std::max(1, effect.params[2]) });
				this->showMessage(ui::format(text.get("Spells.CreaturePoisoned"), { this->creatureRef(creature, true) }), 2.5);
			}
			else if (effect.sub == 2 && success)
			{
				this->creatures.paralyze(creature, spellRounds(effect, Level) * SecondsPerRound);
				this->showMessage(ui::format(text.get("Spells.CreatureParalyzed"), { this->creatureRef(creature, true) }), 3.0);
			}
			else if ((effect.sub == 0 || effect.sub == 3) && success)
			{
				// Doença e maldição: a criatura definha devagar (1 ou 2 pontos a cada meia rodada).
				this->creatureDots.push_back(CreatureDot{ creature, std::max(3, spellRounds(effect, Level)) * SecondsPerRound, 0.0, effect.sub == 3 ? 2 : 1 });
				this->showMessage(ui::format(text.get(effect.sub == 3 ? "Spells.CreatureCursed" : "Spells.CreatureDiseased"), { this->creatureRef(creature, true) }), 2.5);
			}
			else
			{
				this->showMessage(ui::format(text.get("Spells.CreatureResisted"), { this->creatureRef(creature, true) }), 2.0);
			}
			break;
		}
		case SpellEffectType::Silence:
			this->creatures.silence(creature, std::max(2, spellRounds(effect, Level)) * SecondsPerRound);
			this->showMessage(ui::format(text.get("Spells.CreatureSilenced"), { this->creatureRef(creature, true) }), 2.5);
			break;
		case SpellEffectType::Drain:
			hurt(std::max(1, spellAmount(effect, Level, rng)));
			break;
		case SpellEffectType::Transfer:
		{
			// Rouba vida: o dano ao alvo volta como cura para o conjurador.
			const int amount = std::max(1, effect.params[0]);
			const std::string ref = this->creatureRef(creature, false);
			hurt(amount);
			if (effect.sub == 1)
			{
				this->health = std::min(this->game.getCharacter().maxHealth, this->health + amount);
				this->showMessage(ui::format(text.get("Spells.Drained"), { std::to_string(amount), ref }), 3.0);
			}
			break;
		}
		default:
			this->showMessage(ui::format(text.get("Spells.NotAvailable"), { spellName(this->game, spell) }), 3.0);
			break;
		}
	}
}

bool arena::GameWorldScreen::openDoorAhead(const SpellEffect &effect)
{
	const Localization &text = this->game.getText();
	const float fx = std::cos(this->yaw), fz = std::sin(this->yaw);
	for (float d = 0.1f; d < 2.0f; d += 0.05f)
	{
		const int cx = static_cast<int>(std::floor(this->x + fx * d));
		const int cz = static_cast<int>(std::floor(this->z + fz * d));
		const Cell &c = this->level.cell(cx, cz);
		if (c.type == CellType::Wall) break;
		if (c.type != CellType::Door) continue;

		if (this->level.lockLevel(cx, cz) > 0 && static_cast<int>(this->game.getRandom()() % 100u) >= spellChance(effect, this->game.getCharacter().level))
		{
			this->game.playSound("CLICKS.VOC");
			this->showMessage(text.get("Spells.DoorResists"), 3.0);
			return true;
		}

		this->level.unlock(cx, cz);
		if (this->level.doorOpenness(cx, cz) < 0.5f) this->level.toggleDoor(cx, cz);
		this->game.playSound("LOCK.VOC");
		this->playAt("OPENDOOR.VOC", cx + 0.5f, cz + 0.5f);
		this->showMessage(text.get("Spells.DoorOpened"), 3.0);
		return true;
	}

	this->showMessage(text.get("Spells.NoDoor"), 2.5);
	return false;
}

void arena::GameWorldScreen::applySpellToSelf(const Spell &spell)
{
	const Localization &text = this->game.getText();
	std::mt19937 &rng = this->game.getRandom();
	const CharacterDraft &c = this->game.getCharacter();
	const int Level = this->game.getCharacter().level;
	const std::string name = spellName(this->game, spell);

	auto addEffect = [this](SpellEffectType type, double seconds, int magnitude, int sub)
	{
		// Reconjurar renova a duração em vez de acumular.
		this->playerEffects.erase(std::remove_if(this->playerEffects.begin(), this->playerEffects.end(),
			[type, sub](const PlayerEffect &e) { return e.type == type && e.sub == sub; }), this->playerEffects.end());
		this->playerEffects.push_back(PlayerEffect{ type, seconds, magnitude, sub, 0.0 });
	};

	for (const SpellEffect &effect : spell.effects)
	{
		if (!effect.valid()) continue;
		const auto &p = effect.params;
		switch (static_cast<SpellEffectType>(effect.type))
		{
		case SpellEffectType::Heal:
		{
			const int amount = std::max(1, spellAmount(effect, Level, rng));
			if (effect.sub == 1)
			{
				const int healed = std::min(amount, c.maxHealth - this->health);
				this->health += std::max(0, healed);
				this->showMessage(ui::format(text.get("Spells.Healed"), { std::to_string(std::max(0, healed)) }), 3.0);
			}
			else if (effect.sub == 2)
			{
				this->spellPoints = std::min(c.maxSpellPoints, this->spellPoints + amount);
			}
			else
			{
				this->showMessage(text.get("Spells.Stamina"), 2.5);
			}
			break;
		}
		case SpellEffectType::Light:
			addEffect(SpellEffectType::Light, spellRounds(effect, Level) * SecondsPerRound, (spell.flags & 128) ? 1 : 0, 0);
			this->showMessage(text.get("Spells.LightOn"), 3.0);
			break;
		case SpellEffectType::Create:
			if (effect.sub == 0)
			{
				this->shieldPoints += p[0] + p[1] * (Level / std::max(1, p[4]));
				this->showMessage(ui::format(text.get("Spells.ShieldOn"), { std::to_string(this->shieldPoints) }), 3.0);
			}
			else if (effect.sub == 1)
			{
				// Earth Wall: uma parede de terra à frente por algumas rodadas.
				this->raiseWallAhead(std::max(2, spellRounds(effect, Level)) * SecondsPerRound);
			}
			else
			{
				this->showMessage(ui::format(text.get("Spells.NotAvailable"), { name }), 3.0);
			}
			break;
		case SpellEffectType::Invisibility:
			addEffect(SpellEffectType::Invisibility, (p[0] + p[1] * (Level / std::max(1, p[4]))) * SecondsPerRound, 0, 0);
			this->showMessage(text.get("Spells.InvisibleOn"), 3.0);
			break;
		case SpellEffectType::DesignateNonTarget:
			// Santuário: as criaturas deixam de notar o conjurador por um tempo (como invisibilidade).
			addEffect(SpellEffectType::Invisibility, spellRounds(effect, Level) * SecondsPerRound, 0, 0);
			this->showMessage(text.get("Spells.InvisibleOn"), 3.0);
			break;
		case SpellEffectType::ElementalResistance:
			addEffect(SpellEffectType::ElementalResistance, spellRounds(effect, Level) * SecondsPerRound, spellChance(effect, Level), effect.sub);
			this->showMessage(ui::format(text.get("Spells.ResistOn"), { text.get("Spells.Resist" + std::to_string(std::clamp(effect.sub, 0, 4))) }), 3.0);
			break;
		case SpellEffectType::Fortify:
			addEffect(SpellEffectType::Fortify, p[5] * Level * SecondsPerRound + SecondsPerRound, p[0], effect.sub);
			this->showMessage(ui::format(text.get("Spells.FortifyOn"),
				{ this->game.attributeName(std::clamp(effect.sub, 0, AttributeCount - 1), false), std::to_string(p[0]) }), 3.0);
			break;
		case SpellEffectType::Regenerate:
			addEffect(SpellEffectType::Regenerate, std::max(1, p[4] * Level) * SecondsPerRound, std::max(1, p[0]), std::max(1, p[1]));
			this->showMessage(text.get("Spells.RegenerateOn"), 3.0);
			break;
		case SpellEffectType::Cure:
			this->showMessage(text.get("Spells.NothingToCure"), 2.5);
			break;
		case SpellEffectType::Open:
			this->openDoorAhead(effect);
			break;
		case SpellEffectType::Lock:
			this->lockDoorAhead(effect);
			break;
		case SpellEffectType::Levitate:
			addEffect(SpellEffectType::Levitate, spellRounds(effect, Level) * SecondsPerRound, 0, 0);
			this->showMessage(text.get("Spells.LevitateOn"), 3.0);
			break;
		case SpellEffectType::Destroy:
			if (effect.sub == 0) this->passWallAhead();
			else this->showMessage(ui::format(text.get("Spells.NotAvailable"), { name }), 3.0);
			break;
		case SpellEffectType::SpellResistance:
		case SpellEffectType::SpellReflection:
		case SpellEffectType::SpellAbsorption:
			// Proteções contra magia: resistem, devolvem ou absorvem os feitiços das criaturas (boltHitsPlayer).
			addEffect(static_cast<SpellEffectType>(effect.type), std::max(1, spellRounds(effect, Level)) * SecondsPerRound, spellChance(effect, Level), 0);
			this->showMessage(text.get("Spells.MagicWardOn"), 3.0);
			break;
		default:
			this->showMessage(ui::format(text.get("Spells.NotAvailable"), { name }), 3.0);
			break;
		}
	}
}

void arena::GameWorldScreen::updateMagic(double dt)
{
	const Localization &text = this->game.getText();
	const float fdt = static_cast<float>(dt);

	for (auto &[power, seconds] : this->powerRecharge) seconds -= dt;

	if (this->casting)
	{
		this->castTime += dt;
		if (!this->castReleased && this->castTime >= CastRelease)
		{
			this->castReleased = true;
			this->releaseSpell();
		}

		if (this->castTime >= CastLength) this->casting = false;
	}

	// Projéteis: avançam em passos curtos e explodem em paredes, chão/teto ou criaturas.
	const float ceiling = this->level.getWallHeight();
	for (size_t i = 0; i < this->projectiles.size();)
	{
		Projectile &p = this->projectiles[i];
		const Spell *spell = this->game.getData().getSpells().find(p.spell);
		bool done = spell == nullptr;
		float remaining = ProjectileSpeed * fdt;
		while (!done && remaining > 0.0f)
		{
			const float step = std::min(0.1f, remaining);
			remaining -= step;
			p.x += p.dx * step; p.y += p.dy * step; p.z += p.dz * step;
			p.travelled += step;

			int hit = -1;
			const auto &all = this->creatures.getCreatures();
			for (size_t c = 0; c < all.size() && hit < 0; c++)
			{
				const float dx = all[c].x - p.x, dz = all[c].z - p.z;
				if (this->creatures.isAlive(static_cast<int>(c)) && dx * dx + dz * dz < 0.45f * 0.45f && p.y < 1.0f) hit = static_cast<int>(c);
			}

			const int cx = static_cast<int>(std::floor(p.x)), cz = static_cast<int>(std::floor(p.z));
			const float floorY = this->level.floorHeightAt(cx, cz);
			const bool wall = this->level.blocksMovement(cx, cz) && this->level.cell(cx, cz).type != CellType::Empty;
			if (hit >= 0 || wall || p.y < floorY || p.y > ceiling)
			{
				// Volta um passo para a explosão não aparecer dentro da parede.
				this->spellImpact(*spell, p.x - p.dx * step, p.y - p.dy * step, p.z - p.dz * step, hit);
				done = true;
			}
			else if (p.travelled > ProjectileRange)
			{
				done = true;
			}
		}

		if (done) this->projectiles.erase(this->projectiles.begin() + static_cast<std::ptrdiff_t>(i));
		else i++;
	}

	// Feitiços das criaturas: param na parede ou no jogador (só no plano XZ; voam na altura do peito).
	for (size_t i = 0; i < this->enemyBolts.size();)
	{
		EnemyBolt &b = this->enemyBolts[i];
		bool done = false;
		for (float remaining = ProjectileSpeed * 0.75f * fdt; !done && remaining > 0.0f;)
		{
			const float step = std::min(0.1f, remaining);
			remaining -= step;
			b.x += b.dx * step; b.y += b.dy * step; b.z += b.dz * step;
			b.travelled += step;
			const float px = this->x - b.x, pz = this->z - b.z;
			const int cx = static_cast<int>(std::floor(b.x)), cz = static_cast<int>(std::floor(b.z));
			if (px * px + pz * pz < 0.4f * 0.4f && this->health > 0)
			{
				this->boltHitsPlayer(b);
				done = true;
			}
			else if (this->level.blocksMovement(cx, cz) && this->level.cell(cx, cz).type != CellType::Empty)
			{
				this->bursts.push_back(Burst{ b.x - b.dx * step, b.y, b.z - b.dz * step, 0.0, b.element });
				this->playAt("NHIT.VOC", b.x, b.z, 0.7f);
				done = true;
			}
			else if (b.travelled > ProjectileRange) done = true;
		}

		if (done) this->enemyBolts.erase(this->enemyBolts.begin() + static_cast<std::ptrdiff_t>(i));
		else i++;
	}

	for (TempWall &w : this->tempWalls)
	{
		w.remaining -= dt;
		if (w.remaining <= 0.0) this->level.setWall(w.x, w.z, false, -1);
	}
	this->tempWalls.erase(std::remove_if(this->tempWalls.begin(), this->tempWalls.end(), [](const TempWall &w) { return w.remaining <= 0.0; }), this->tempWalls.end());

	for (Burst &b : this->bursts) b.age += dt;
	this->bursts.erase(std::remove_if(this->bursts.begin(), this->bursts.end(), [](const Burst &b) { return b.age > BurstLength; }), this->bursts.end());

	// Efeitos no jogador.
	for (size_t i = 0; i < this->playerEffects.size();)
	{
		PlayerEffect &e = this->playerEffects[i];
		e.remaining -= dt;
		if (e.type == SpellEffectType::Regenerate)
		{
			e.tick += dt;
			if (e.tick >= e.sub * SecondsPerRound)
			{
				e.tick = 0.0;
				this->health = std::min(this->game.getCharacter().maxHealth, this->health + e.magnitude);
			}
		}

		if (e.remaining > 0.0)
		{
			i++;
			continue;
		}

		if (e.type == SpellEffectType::Light) this->showMessage(text.get("Spells.LightGone"), 2.5);
		else if (e.type == SpellEffectType::Invisibility) this->showMessage(text.get("Spells.InvisibleGone"), 2.5);
		else if (e.type == SpellEffectType::Fortify) this->showMessage(text.get("Spells.FortifyGone"), 2.5);
		else if (e.type == SpellEffectType::Levitate) this->showMessage(text.get("Spells.LevitateGone"), 2.5);
		else if (e.type == SpellEffectType::SpellResistance || e.type == SpellEffectType::SpellReflection || e.type == SpellEffectType::SpellAbsorption) this->showMessage(text.get("Spells.MagicWardGone"), 2.5);
		this->playerEffects.erase(this->playerEffects.begin() + static_cast<std::ptrdiff_t>(i));
	}

	// Dano contínuo e veneno nas criaturas (a cada meia rodada).
	for (size_t i = 0; i < this->creatureDots.size();)
	{
		CreatureDot &d = this->creatureDots[i];
		d.remaining -= dt;
		d.tick += dt;
		if (this->creatures.isAlive(d.creature) && d.tick >= SecondsPerRound * 0.5)
		{
			d.tick = 0.0;
			const int maxHp = this->creatures.getCreatures()[static_cast<size_t>(d.creature)].maxHp;
			if (this->creatures.damage(d.creature, d.damage)) this->rewardKill(d.creature, maxHp);
		}

		if (d.remaining <= 0.0 || !this->creatures.isAlive(d.creature)) this->creatureDots.erase(this->creatureDots.begin() + static_cast<std::ptrdiff_t>(i));
		else i++;
	}
}

void arena::GameWorldScreen::addMagicLights(std::vector<PointLight> &lights) const
{
	const Vec3 eye{ this->x, EyeHeight + this->eyeOffset, this->z };
	for (const PlayerEffect &e : this->playerEffects)
	{
		if (e.type != SpellEffectType::Light) continue;
		// Globo de luz: acompanha o conjurador (ou vagueia ao redor, na Luz Errante).
		const float wander = e.magnitude ? 0.6f : 0.0f;
		const float t = static_cast<float>(this->time);
		lights.push_back(PointLight{ { eye.x + std::cos(t * 0.7f) * wander, eye.y + 0.3f, eye.z + std::sin(t * 0.9f) * wander }, 8.0f, 0.5f, 0.52f, 0.62f });
	}

	for (const Projectile &p : this->projectiles)
	{
		const Spell *spell = this->game.getData().getSpells().find(p.spell);
		const float *c = ElementColor[spell ? static_cast<int>(spell->element) : 5];
		lights.push_back(PointLight{ { p.x, p.y, p.z }, 3.2f, c[0], c[1], c[2] });
	}

	for (const EnemyBolt &b : this->enemyBolts)
	{
		const float *c = ElementColor[std::clamp(b.element, 0, 5)];
		lights.push_back(PointLight{ { b.x, b.y, b.z }, 3.2f, c[0], c[1], c[2] });
	}

	for (const Burst &b : this->bursts)
	{
		const float k = static_cast<float>(1.0 - b.age / BurstLength);
		const float *c = ElementColor[std::clamp(b.element, 0, 5)];
		lights.push_back(PointLight{ { b.x, b.y, b.z }, 4.0f * k + 0.5f, c[0] * k, c[1] * k, c[2] * k });
	}
}

void arena::GameWorldScreen::drawMagic()
{
	const Vec3 right = this->renderer.getRight();
	auto billboard = [&](float cx, float cy, float cz, float size, const Texture &texture)
	{
		const float h = size * 0.5f;
		const Vec3 v[4] = { { cx - right.x * h, cy + h, cz - right.z * h }, { cx + right.x * h, cy + h, cz + right.z * h },
			{ cx + right.x * h, cy - h, cz + right.z * h }, { cx - right.x * h, cy - h, cz - right.z * h } };
		const float uv[4][2] = { { 0, 0 }, { 0.999f, 0 }, { 0.999f, 0.999f }, { 0, 0.999f } };
		Surface surface;
		surface.kind = Surface::Kind::Emissive;
		this->renderer.drawQuad(v, uv, texture, true, surface);
	};

	for (const Projectile &p : this->projectiles)
	{
		const Spell *spell = this->game.getData().getSpells().find(p.spell);
		const int element = spell ? static_cast<int>(spell->element) : 5;
		const float pulse = 0.22f + 0.03f * static_cast<float>(std::sin(this->time * 20.0));
		billboard(p.x, p.y, p.z, spell && spell->target == SpellTarget::AreaRanged ? pulse * 1.5f : pulse, this->orbTextures[static_cast<size_t>(element)]);
	}

	for (const EnemyBolt &b : this->enemyBolts)
		billboard(b.x, b.y, b.z, 0.22f + 0.03f * static_cast<float>(std::sin(this->time * 20.0)), this->orbTextures[static_cast<size_t>(std::clamp(b.element, 0, 5))]);

	// Impacto: clarão que cresce e some (a luz vem de addMagicLights).
	for (const Burst &b : this->bursts)
	{
		const float k = static_cast<float>(b.age / BurstLength);
		billboard(b.x, b.y, b.z, 0.25f + k * 0.45f, this->orbTextures[static_cast<size_t>(std::clamp(b.element, 0, 5))]);
	}
}

void arena::GameWorldScreen::drawCasting(std::vector<uint32_t> &pixels, int width, int viewHeight)
{
	const Spell *spell = this->game.getData().getSpells().find(this->castingSpell);
	if (!this->casting || !spell) return;

	// Mesmas coordenadas da arma: área 3D original de 320x147, centrada na tela.
	const float scale = viewHeight / 147.0f;
	auto blit = [&](const Texture &texture, int offsetX, int offsetY, bool mirror)
	{
		const float texelScale = scale / texture.scale;
		const int x0 = static_cast<int>(width * 0.5f + (offsetX - 160.0f) * scale);
		const int y0 = static_cast<int>(offsetY * scale);
		const int w = static_cast<int>(texture.width * texelScale), h = static_cast<int>(texture.height * texelScale);
		for (int y = std::max(0, y0); y < std::min(viewHeight, y0 + h); y++)
		{
			const int ty = std::min(texture.height - 1, static_cast<int>((y - y0) / texelScale));
			for (int px = std::max(0, x0); px < std::min(width, x0 + w); px++)
			{
				int tx = std::min(texture.width - 1, static_cast<int>((px - x0) / texelScale));
				if (mirror) tx = texture.width - 1 - tx;
				const uint32_t texel = texture.pixels[static_cast<size_t>(ty * texture.width + tx)];
				if ((texel >> 24) != 0) pixels[static_cast<size_t>(y) * width + px] = texel;
			}
		}
	};

	// Mãos erguidas e, por cima, o efeito saindo delas (desenhado nos dois lados).
	const int handsHeight = this->castHands.height / std::max(1, this->castHands.scale);
	const int handsWidth = this->castHands.width / std::max(1, this->castHands.scale);
	blit(this->castHands, (320 - handsWidth) / 2, 147 - handsHeight, false);

	const ScreenAnimation &animation = this->castAnimations[static_cast<size_t>(castAnimationFor(*spell))];
	if (animation.textures.empty()) return;
	const size_t frame = std::min(animation.textures.size() - 1, static_cast<size_t>(this->castTime * 14.0));
	const Texture &texture = animation.textures[frame];
	const auto [ox, oy] = animation.offsets[frame];
	const int frameWidth = texture.width / std::max(1, texture.scale);
	blit(texture, ox, oy, false);
	blit(texture, 320 - ox - frameWidth, oy, true);
}

bool arena::GameWorldScreen::lockDoorAhead(const SpellEffect &effect)
{
	// Wizard Lock: tranca a porta à frente (nível da trava pela chance do feitiço).
	const Localization &text = this->game.getText();
	const float fx = std::cos(this->yaw), fz = std::sin(this->yaw);
	for (float d = 0.1f; d < 2.0f; d += 0.05f)
	{
		const int cx = static_cast<int>(std::floor(this->x + fx * d)), cz = static_cast<int>(std::floor(this->z + fz * d));
		const Cell &c = this->level.cell(cx, cz);
		if (c.type == CellType::Wall) break;
		if (c.type != CellType::Door) continue;
		if (this->level.doorOpenness(cx, cz) > 0.5f) this->level.toggleDoor(cx, cz);
		this->level.lock(cx, cz, std::max(1, spellChance(effect, this->game.getCharacter().level) / 10));
		this->game.playSound("LOCK.VOC");
		this->showMessage(text.get("Spells.DoorLocked"), 3.0);
		return true;
	}

	this->showMessage(text.get("Spells.NoDoor"), 2.5);
	return false;
}

bool arena::GameWorldScreen::passWallAhead()
{
	// Passwall: abre a parede logo à frente (nunca a borda do mapa, portas, saídas ou escadas).
	const Localization &text = this->game.getText();
	const int cx = static_cast<int>(std::floor(this->x + std::cos(this->yaw) * 1.0f)), cz = static_cast<int>(std::floor(this->z + std::sin(this->yaw) * 1.0f));
	const bool border = cx <= 0 || cz <= 0 || cx >= this->level.getWidth() - 1 || cz >= this->level.getDepth() - 1;
	const Cell &c = this->level.cell(cx, cz);
	if (border || c.type != CellType::Wall || c.isExit || c.isLevelDown || c.menu >= 0)
	{
		this->showMessage(text.get("Spells.WallResists"), 2.5);
		return false;
	}

	this->level.setWall(cx, cz, false, -1);
	this->game.playSound("GRIND.VOC");
	this->showMessage(text.get("Spells.WallOpened"), 3.0);
	return true;
}

bool arena::GameWorldScreen::raiseWallAhead(double seconds)
{
	// Earth Wall: ergue uma parede na célula vazia à frente (sem criaturas nela), com a textura
	// da parede mais próxima, e a desfaz depois.
	const Localization &text = this->game.getText();
	const int cx = static_cast<int>(std::floor(this->x + std::cos(this->yaw) * 1.3f)), cz = static_cast<int>(std::floor(this->z + std::sin(this->yaw) * 1.3f));
	const bool here = cx == static_cast<int>(std::floor(this->x)) && cz == static_cast<int>(std::floor(this->z));
	if (here || !this->level.inBounds(cx, cz) || this->level.cell(cx, cz).type != CellType::Empty || this->level.cell(cx, cz).chasm || this->creatures.blocks(cx + 0.5f, cz + 0.5f, 0.5f))
	{
		this->showMessage(text.get("Spells.NoRoom"), 2.5);
		return false;
	}

	// A textura de parede mais comum por perto, como reserva.
	std::map<int, int> counts;
	for (int dx = -5; dx <= 5; dx++)
		for (int dz = -5; dz <= 5; dz++)
			if (this->level.inBounds(cx + dx, cz + dz) && this->level.cell(cx + dx, cz + dz).type == CellType::Wall) counts[this->level.cell(cx + dx, cz + dz).wallTexture]++;
	int texture = -1, best = 0;
	for (const auto &[candidate, count] : counts) if (count > best) { best = count; texture = candidate; }
	// Melhor ainda: a parede colada ao jogador ou ao lugar da nova parede, que é a que se está vendo.
	const int px = static_cast<int>(std::floor(this->x)), pz = static_cast<int>(std::floor(this->z));
	const int nx[4] = { 1, -1, 0, 0 }, nz[4] = { 0, 0, 1, -1 };
	for (const auto &[ox, oz] : { std::pair<int, int>{ px, pz }, std::pair<int, int>{ cx, cz } })
		for (int d = 0; d < 4; d++)
		{
			const Cell &n = this->level.cell(ox + nx[d], oz + nz[d]);
			if (n.type == CellType::Wall && !(ox + nx[d] == cx && oz + nz[d] == cz) && n.menu < 0 && !n.isExit && !n.isLevelDown) { texture = n.wallTexture; goto chosen; }
		}
chosen:
	if (texture < 0) { this->showMessage(text.get("Spells.NoRoom"), 2.5); return false; }

	this->level.setWall(cx, cz, true, texture);
	this->tempWalls.push_back({ cx, cz, seconds });
	this->game.playSound("GRIND.VOC");
	this->showMessage(text.get("Spells.WallRaised"), 3.0);
	return true;
}

void arena::GameWorldScreen::creatureCasts(int creature, int damage)
{
	// Sai da criatura, na altura do peito, em direção aos olhos do jogador.
	const Creature &c = this->creatures.getCreatures()[static_cast<size_t>(creature)];
	const CreatureDefinition &def = this->creatures.definitionOf(creature);
	const float dx = this->x - c.x, dz = this->z - c.z;
	const float dist = std::max(0.01f, std::sqrt(dx * dx + dz * dz));
	EnemyBolt bolt;
	bolt.dx = dx / dist;
	bolt.dz = dz / dist;
	bolt.dy = 0.0f;
	bolt.x = c.x + bolt.dx * 0.4f;
	bolt.z = c.z + bolt.dz * 0.4f;
	bolt.y = EyeHeight * 0.85f;
	bolt.creature = creature;
	bolt.element = std::clamp(def.spellElement, 0, 4);
	bolt.damage = std::max(1, damage);
	this->enemyBolts.push_back(bolt);
	this->playAt("SLOWBALL.VOC", c.x, c.z, 0.9f);
	if (this->messageTimer < 1.0) this->showMessage(ui::format(this->game.getText().get("Spells.CreatureCasts"), { this->creatureRef(creature, true) }), 2.0);
}

void arena::GameWorldScreen::boltHitsPlayer(const EnemyBolt &bolt)
{
	const Localization &text = this->game.getText();
	std::mt19937 &rng = this->game.getRandom();
	this->bursts.push_back(Burst{ this->x, EyeHeight * 0.8f, this->z, 0.0, bolt.element });
	const std::string who = bolt.creature >= 0 ? this->creatureRef(bolt.creature, true) : std::string();
	auto succeeds = [&rng](int chance) { return static_cast<int>(rng() % 100u) < std::clamp(chance, 5, 95); };

	// Proteções do jogador: refletir devolve o feitiço, absorver vira magia, resistir anula.
	for (const PlayerEffect &e : this->playerEffects)
	{
		if (e.type == SpellEffectType::SpellReflection && succeeds(e.magnitude) && bolt.creature >= 0 && this->creatures.isAlive(bolt.creature))
		{
			const int maxHp = this->creatures.getCreatures()[static_cast<size_t>(bolt.creature)].maxHp;
			this->showMessage(ui::format(text.get("Spells.Reflected"), { this->creatureRef(bolt.creature, false) }), 3.0);
			if (this->creatures.damage(bolt.creature, bolt.damage)) this->rewardKill(bolt.creature, maxHp);
			return;
		}
		if (e.type == SpellEffectType::SpellAbsorption && succeeds(e.magnitude))
		{
			this->spellPoints = std::min(this->game.getCharacter().maxSpellPoints, this->spellPoints + bolt.damage);
			this->showMessage(ui::format(text.get("Spells.Absorbed"), { std::to_string(bolt.damage) }), 3.0);
			return;
		}
		if (e.type == SpellEffectType::SpellResistance && succeeds(e.magnitude))
		{
			this->showMessage(text.get("Spells.Resisted"), 2.5);
			return;
		}

		// Resistência ao elemento (Resist0..4 = fogo, frio, choque, magia, veneno).
		constexpr int ResistOf[5] = { 0, 1, 4, 2, 3 }; // por SpellElement: fogo, frio, veneno, choque, magia
		if (e.type == SpellEffectType::ElementalResistance && e.sub == ResistOf[std::clamp(bolt.element, 0, 4)] && succeeds(e.magnitude))
		{
			this->showMessage(ui::format(text.get("Spells.ResistedElement"), { text.get("Spells.Resist" + std::to_string(e.sub)) }), 2.5);
			return;
		}
	}

	// Dano: o Escudo mágico absorve primeiro.
	int amount = bolt.damage;
	const int absorbed = std::min(this->shieldPoints, amount);
	this->shieldPoints -= absorbed;
	amount -= absorbed;
	this->health = std::max(0, this->health - amount);
	this->hurtTimer = 0.4;
	this->game.playSound(this->health > 0 ? "UHIT.VOC" : (this->game.getCharacter().male ? "MDIE.VOC" : "FDIE.VOC"));
	this->showMessage(amount > 0 ? ui::format(text.get("Spells.PlayerHit"), { who, std::to_string(amount) }) : text.get("Spells.ShieldAbsorb"), 2.5);
	if (this->health <= 0)
	{
		this->swinging = false;
		this->heldKeys.clear();
		this->showMessage(text.get("World.DeathMessage"), 1e9);
	}
}
