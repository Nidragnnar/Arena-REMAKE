// Criaturas: animações .CFA (convenções de quadros/direções do OpenTESArena, MIT)
// e uma IA simples no espírito do Arena: vagam, notam o jogador, perseguem e atacam.

#include <algorithm>
#include <cmath>

#include "Creatures.h"

namespace
{
	constexpr float Pi = 3.14159265f;
	constexpr float TwoPi = 2.0f * Pi;
	constexpr float ArenaUnits = 128.0f;
	constexpr float CreatureRadius = 0.22f;
	constexpr float SightRange = 7.5f;
	constexpr float HearingRange = 2.0f;
	constexpr float AttackRange = 0.95f;

	// Quadros por estado (arquivos de direção @1..@5).
	constexpr int WalkFrames[] = { 0, 1, 2, 3, 4, 5 };
	constexpr int LookFrames[] = { 6, 0, 7, 0 };
	constexpr int AttackFrames[] = { 8, 9, 10, 11 };
	constexpr double WalkFps = 12.0, LookFps = 8.0, AttackFps = 14.0, DeathFps = 12.0;

	float randomFloat(std::mt19937 &rng, float lo, float hi)
	{
		return lo + (hi - lo) * static_cast<float>(rng() % 10000) / 10000.0f;
	}

	int randomInt(std::mt19937 &rng, int lo, int hi)
	{
		return (hi <= lo) ? lo : lo + static_cast<int>(rng() % static_cast<uint32_t>(hi - lo + 1));
	}

	float speedFor(const arena::CreatureDefinition &def)
	{
		// Criaturas pequenas e baixas (ratos) são mais rápidas.
		return (def.level <= 1 && def.maxHp <= 6) ? 1.9f : 1.5f;
	}
}

bool arena::CreatureSystem::loadAnimation(const AssetManager &assets, const Palette &palette, int definitionIndex)
{
	if (this->animations.contains(definitionIndex))
	{
		return true;
	}

	const CreatureDefinition &def = this->definitions[static_cast<size_t>(definitionIndex)];
	const size_t at = def.animFilename.find('@');
	if (at == std::string::npos)
	{
		return false;
	}

	auto loadFile = [&](int direction, std::vector<CreatureAnimation::Frame> &out)
	{
		std::string name = def.animFilename;
		name[at] = static_cast<char>('0' + direction);
		const auto data = assets.read(name);
		if (!data) return false;
		const std::vector<IndexedImage> images = loadCfa(*data);
		for (size_t i = 0; i < images.size(); i++)
		{
			const IndexedImage &image = images[i];
			// Dimensões: (px * escala / 256) e altura ainda * 200/256 (pixels do VGA), em unidades de 128.
			CreatureAnimation::Frame frame;
			frame.texture = static_cast<int>(this->textures.size());
			frame.width = ((image.width * def.scale) / 256) / ArenaUnits;
			frame.height = ((((image.height * def.scale) / 256) * 200) / 256) / ArenaUnits;
			this->textures.push_back(toTexture(image, palette, name, static_cast<int>(i), static_cast<int>(images.size())));
			out.push_back(frame);
		}

		return !out.empty();
	};

	CreatureAnimation animation;
	for (int dir = 1; dir <= 5; dir++)
	{
		if (!loadFile(dir, animation.directions[static_cast<size_t>(dir - 1)]))
		{
			return false;
		}
	}

	loadFile(6, animation.death);
	this->animations.emplace(definitionIndex, std::move(animation));
	return true;
}

void arena::CreatureSystem::init(const AssetManager &assets, const Palette &palette, const ArenaData &data, const Level &level, std::mt19937 &rng)
{
	// Conjuradores do original: Fantasma e Espectro (frio), Homúnculo e Lich (magia), Daemon de Fogo (fogo),
	// Medusa (veneno), Vampiro (magia, rouba vida) e Jagar Tharn (choque).
	static const std::map<int, SpellElement> Casters = { { 11, SpellElement::Cold }, { 14, SpellElement::Cold }, { 15, SpellElement::Magic },
		{ 19, SpellElement::Fire }, { 20, SpellElement::Poison }, { 21, SpellElement::Magic }, { 22, SpellElement::Magic }, { 23, SpellElement::Shock } };
	for (int i = 0; data.getCreature(i) != nullptr; i++)
	{
		this->definitions.push_back(*data.getCreature(i));
		if (const auto it = Casters.find(i); it != Casters.end()) this->definitions.back().spellElement = static_cast<int>(it->second);
	}

	for (const CreatureSpawn &spawn : level.getCreatureSpawns())
	{
		if (spawn.creatureIndex >= static_cast<int>(this->definitions.size()) || !this->loadAnimation(assets, palette, spawn.creatureIndex))
		{
			continue;
		}

		const CreatureDefinition &def = this->definitions[static_cast<size_t>(spawn.creatureIndex)];
		Creature creature;
		creature.definition = spawn.creatureIndex;
		creature.x = spawn.x;
		creature.z = spawn.z;
		creature.angle = randomFloat(rng, 0.0f, TwoPi);
		creature.maxHp = creature.hp = randomInt(rng, def.minHp, def.maxHp);
		creature.thinkTimer = randomFloat(rng, 0.0f, 3.0f);
		this->creatures.push_back(creature);
	}
}

int arena::CreatureSystem::aliveCount() const
{
	return static_cast<int>(std::count_if(this->creatures.begin(), this->creatures.end(),
		[](const Creature &c) { return c.state != Creature::State::Dying && c.state != Creature::State::Dead; }));
}

bool arena::CreatureSystem::hasLineOfSight(const Level &level, float x0, float z0, float x1, float z1) const
{
	const float dx = x1 - x0, dz = z1 - z0;
	const float length = std::sqrt(dx * dx + dz * dz);
	const int steps = static_cast<int>(length / 0.1f);
	for (int i = 1; i < steps; i++)
	{
		const float t = static_cast<float>(i) / steps;
		const int cx = static_cast<int>(std::floor(x0 + dx * t));
		const int cz = static_cast<int>(std::floor(z0 + dz * t));
		const Cell &c = level.cell(cx, cz);
		if (c.type == CellType::Wall || (c.type == CellType::Door && level.doorOpenness(cx, cz) < 0.9f)) return false;
	}

	return true;
}

bool arena::CreatureSystem::canStand(const Level &level, float x, float z, size_t self, const PlayerCombatStats &player) const
{
	for (int cx = static_cast<int>(std::floor(x - CreatureRadius)); cx <= static_cast<int>(std::floor(x + CreatureRadius)); cx++)
	{
		for (int cz = static_cast<int>(std::floor(z - CreatureRadius)); cz <= static_cast<int>(std::floor(z + CreatureRadius)); cz++)
		{
			// Criaturas não entram em água/fossos nem atravessam paredes e portas fechadas.
			if (level.blocksMovement(cx, cz) || level.cell(cx, cz).chasm) return false;
		}
	}

	const float pdx = x - player.x, pdz = z - player.z;
	if (player.alive && pdx * pdx + pdz * pdz < 0.55f * 0.55f) return false;

	for (size_t i = 0; i < this->creatures.size(); i++)
	{
		const Creature &other = this->creatures[i];
		if (i == self || other.state == Creature::State::Dying || other.state == Creature::State::Dead) continue;
		const float dx = x - other.x, dz = z - other.z;
		if (dx * dx + dz * dz < 0.45f * 0.45f) return false;
	}

	return true;
}

void arena::CreatureSystem::moveTowards(Creature &creature, size_t self, const Level &level, const PlayerCombatStats &player, float dirX, float dirZ, float distance)
{
	const float nx = creature.x + dirX * distance;
	const float nz = creature.z + dirZ * distance;
	if (this->canStand(level, nx, creature.z, self, player)) creature.x = nx;
	if (this->canStand(level, creature.x, nz, self, player)) creature.z = nz;
}

bool arena::CreatureSystem::blocks(float x, float z, float radius) const
{
	for (const Creature &c : this->creatures)
	{
		if (c.state == Creature::State::Dying || c.state == Creature::State::Dead) continue;
		const float dx = x - c.x, dz = z - c.z;
		if (dx * dx + dz * dz < (radius + CreatureRadius) * (radius + CreatureRadius)) return true;
	}

	return false;
}

void arena::CreatureSystem::update(double dt, const Level &level, const PlayerCombatStats &player, std::mt19937 &rng, std::vector<CombatEvent> &events)
{
	const float fdt = static_cast<float>(dt);
	for (size_t i = 0; i < this->creatures.size(); i++)
	{
		Creature &c = this->creatures[i];
		const CreatureDefinition &def = this->definitions[static_cast<size_t>(c.definition)];
		c.stateTime += dt;
		c.cooldown -= dt;
		c.spellCooldown -= dt;
		c.silenced -= dt;
		c.hurtFlash -= dt;

		if (c.state == Creature::State::Dead)
		{
			continue;
		}

		if (c.state == Creature::State::Dying)
		{
			const auto &death = this->animations.at(c.definition).death;
			if (c.stateTime * DeathFps >= std::max<size_t>(1, death.size())) c.state = Creature::State::Dead;
			continue;
		}

		// Paralisada por feitiço: congela no lugar (animação parada).
		if (c.paralyzed > 0.0)
		{
			c.paralyzed -= dt;
			c.stateTime -= dt;
			continue;
		}

		const float dx = player.x - c.x, dz = player.z - c.z;
		const float dist = std::sqrt(dx * dx + dz * dz);
		const bool visible = player.alive && !player.invisible;

		// Percepção: vê o jogador em linha reta ou o ouve muito perto.
		if (!c.awake && visible && (dist < HearingRange || (dist < SightRange && this->hasLineOfSight(level, c.x, c.z, player.x, player.z))))
		{
			c.awake = true;
			events.push_back({ CombatEvent::Type::CreatureNoticed, static_cast<int>(i), 0 });
		}

		if (c.state == Creature::State::Attack)
		{
			c.angle = std::atan2(dz, dx);
			const double attackLength = std::size(AttackFrames) / AttackFps;
			if (!c.attackResolved && c.stateTime >= attackLength * 0.7)
			{
				c.attackResolved = true;
				if (player.alive && dist <= AttackRange + 0.25f)
				{
					// Chance de acerto: nível da criatura contra a defesa (Agilidade) do jogador.
					// O golpe mira uma parte do corpo (o peito com o dobro de chance); armadura e escudo reduzem o acerto.
					const int roll = randomInt(rng, 0, 7);
					const int part = (roll == 7) ? 0 : roll;
					const int armor = player.partArmor[static_cast<size_t>(part)] + player.shieldArmor;
					const int chance = std::clamp(45 + 5 * def.level - 5 * player.defenseBonus - 2 * (player.level - 1) - 5 * armor, 5, 90);
					if (randomInt(rng, 1, 100) <= chance)
						events.push_back({ CombatEvent::Type::CreatureHitPlayer, static_cast<int>(i), randomInt(rng, def.minDamage, def.maxDamage) });
					else
						events.push_back({ CombatEvent::Type::CreatureMissedPlayer, static_cast<int>(i), 0 });
				}
			}

			if (c.stateTime >= attackLength)
			{
				c.state = Creature::State::Chase;
				c.stateTime = 0;
				c.cooldown = randomFloat(rng, 0.7f, 1.4f);
			}

			continue;
		}

		if (c.awake && visible)
		{
			// Rosnados/guinchos periódicos enquanto caça o jogador.
			c.soundTimer -= dt;
			if (c.soundTimer <= 0.0)
			{
				c.soundTimer = randomFloat(rng, 3.0f, 7.0f);
				events.push_back({ CombatEvent::Type::CreatureSound, static_cast<int>(i), 0 });
			}

			c.angle = std::atan2(dz, dx);
			if (def.spellElement >= 0 && c.silenced <= 0.0 && c.spellCooldown <= 0.0 && c.cooldown <= 0.0 && dist > 1.6f && dist < SightRange
				&& this->hasLineOfSight(level, c.x, c.z, player.x, player.z))
			{
				// Conjura de longe: a animação de ataque sem golpe, e o projétil sai pelo jogo (CreatureCast).
				c.state = Creature::State::Attack;
				c.stateTime = 0;
				c.attackResolved = true;
				c.spellCooldown = randomFloat(rng, 4.0f, 8.0f);
				events.push_back({ CombatEvent::Type::CreatureCast, static_cast<int>(i), randomInt(rng, def.minDamage, def.maxDamage) });
			}
			else if (dist <= AttackRange && c.cooldown <= 0.0)
			{
				c.state = Creature::State::Attack;
				c.stateTime = 0;
				c.attackResolved = false;
				events.push_back({ CombatEvent::Type::CreatureAttackStart, static_cast<int>(i), 0 });
			}
			else if (dist > AttackRange * 0.8f)
			{
				if (c.state != Creature::State::Chase) { c.state = Creature::State::Chase; c.stateTime = 0; }
				this->moveTowards(c, i, level, player, dx / dist, dz / dist, speedFor(def) * fdt);
			}
			else
			{
				c.state = Creature::State::Idle;
			}

			continue;
		}

		// Sem notar o jogador: alterna entre parado, olhando em volta e vagando.
		c.thinkTimer -= dt;
		if (c.thinkTimer <= 0.0)
		{
			const int roll = randomInt(rng, 0, 9);
			c.stateTime = 0;
			if (roll < 4) { c.state = Creature::State::Idle; c.thinkTimer = randomFloat(rng, 1.5f, 4.0f); }
			else if (roll < 6) { c.state = Creature::State::Look; c.thinkTimer = std::size(LookFrames) / LookFps; }
			else { c.state = Creature::State::Wander; c.angle = randomFloat(rng, 0.0f, TwoPi); c.thinkTimer = randomFloat(rng, 1.0f, 2.5f); }
		}

		if (c.state == Creature::State::Wander)
		{
			const float oldX = c.x, oldZ = c.z;
			this->moveTowards(c, i, level, player, std::cos(c.angle), std::sin(c.angle), speedFor(def) * 0.45f * fdt);
			if (oldX == c.x && oldZ == c.z) c.angle += Pi * 0.5f; // bateu: vira
		}
	}
}

int arena::CreatureSystem::findTarget(float px, float pz, float yaw, float reach, float coneCos) const
{
	const float fx = std::cos(yaw), fz = std::sin(yaw);
	int best = -1;
	float bestDist = reach;
	for (size_t i = 0; i < this->creatures.size(); i++)
	{
		const Creature &c = this->creatures[i];
		if (c.state == Creature::State::Dying || c.state == Creature::State::Dead) continue;
		const float dx = c.x - px, dz = c.z - pz;
		const float dist = std::sqrt(dx * dx + dz * dz);
		if (dist > bestDist || dist < 1e-4f) continue;
		if ((dx * fx + dz * fz) / dist < coneCos && dist > 0.5f) continue;
		best = static_cast<int>(i);
		bestDist = dist;
	}

	return best;
}

bool arena::CreatureSystem::damage(int index, int amount)
{
	Creature &c = this->creatures[static_cast<size_t>(index)];
	c.hp -= amount;
	c.awake = true;
	c.hurtFlash = 0.2;
	if (c.hp <= 0)
	{
		c.state = Creature::State::Dying;
		c.stateTime = 0;
		return true;
	}

	return false;
}

void arena::CreatureSystem::draw(Renderer3D &renderer, const Level &level, float eyeX, float eyeZ, float drawRadius) const
{
	const Vec3 right = renderer.getRight();
	const Vec3 forward = renderer.getForward();
	for (const Creature &c : this->creatures)
	{
		const float ox = c.x - eyeX, oz = c.z - eyeZ;
		if (ox * ox + oz * oz > drawRadius * drawRadius || ox * forward.x + oz * forward.z < -0.5f) continue;

		const CreatureAnimation &anim = this->animations.at(c.definition);
		const CreatureDefinition &def = this->definitions[static_cast<size_t>(c.definition)];

		// Direção da animação relativa à câmera (1 = de frente; 6..8 espelham 4..2).
		const float diffAngle = std::atan2(eyeZ - c.z, eyeX - c.x);
		float relative = TwoPi + (diffAngle - c.angle);
		relative = std::fmod(relative + TwoPi / 16.0f, TwoPi);
		const int angleIndex = std::clamp(static_cast<int>(relative / (TwoPi / 8.0f)), 0, 7);
		int direction = angleIndex + 1;
		bool mirrored = false;
		if (direction >= 6)
		{
			direction = 10 - direction;
			mirrored = true;
		}

		const auto &frames = anim.directions[static_cast<size_t>(direction - 1)];
		const CreatureAnimation::Frame *frame = &frames[0];
		auto pick = [&](const std::vector<CreatureAnimation::Frame> &list, const int *indices, size_t count, double fps, bool loop)
		{
			size_t step = static_cast<size_t>(c.stateTime * fps);
			step = loop ? step % count : std::min(step, count - 1);
			const int index = indices[step];
			return &list[(index < static_cast<int>(list.size())) ? static_cast<size_t>(index) : 0];
		};

		switch (c.state)
		{
		case Creature::State::Chase:
		case Creature::State::Wander:
			frame = pick(frames, WalkFrames, std::size(WalkFrames), WalkFps, true);
			break;
		case Creature::State::Look:
			frame = pick(frames, LookFrames, std::size(LookFrames), LookFps, false);
			break;
		case Creature::State::Attack:
			mirrored = false;
			frame = pick(anim.directions[0], AttackFrames, std::size(AttackFrames), AttackFps, false);
			break;
		case Creature::State::Dying:
		case Creature::State::Dead:
			if (!anim.death.empty())
			{
				const size_t step = std::min(anim.death.size() - 1, static_cast<size_t>(c.stateTime * DeathFps));
				frame = &anim.death[c.state == Creature::State::Dead ? anim.death.size() - 1 : step];
				mirrored = false;
			}
			if (c.state == Creature::State::Dead && (!def.hasCorpse || c.gone)) continue;
			break;
		default:
			break;
		}

		const float floorY = level.floorHeightAt(static_cast<int>(std::floor(c.x)), static_cast<int>(std::floor(c.z)));
		const float yb = floorY - def.yOffset / ArenaUnits;
		const float yt = yb + frame->height;
		const float hw = frame->width * 0.5f;
		const Vec3 v[4] =
		{
			{ c.x - right.x * hw, yt, c.z - right.z * hw },
			{ c.x + right.x * hw, yt, c.z + right.z * hw },
			{ c.x + right.x * hw, yb, c.z + right.z * hw },
			{ c.x - right.x * hw, yb, c.z - right.z * hw }
		};
		const float u0 = mirrored ? 0.999f : 0.0f, u1 = mirrored ? 0.0f : 0.999f;
		const float uv[4][2] = { { u0, 0 }, { u1, 0 }, { u1, 0.999f }, { u0, 0.999f } };
		Surface figure;
		figure.kind = Surface::Kind::Figure;
		renderer.drawQuad(v, uv, this->textures[static_cast<size_t>(frame->texture)], true, figure);
	}
}

int arena::CreatureSystem::spawn(const AssetManager &assets, const Palette &palette, int definitionIndex, float x, float z, std::mt19937 &rng)
{
	if (definitionIndex < 0 || definitionIndex >= static_cast<int>(this->definitions.size()) || !this->loadAnimation(assets, palette, definitionIndex)) return -1;
	const CreatureDefinition &def = this->definitions[static_cast<size_t>(definitionIndex)];
	Creature creature;
	creature.definition = definitionIndex;
	creature.x = x;
	creature.z = z;
	creature.angle = randomFloat(rng, 0.0f, TwoPi);
	creature.maxHp = creature.hp = randomInt(rng, def.minHp, def.maxHp);
	creature.thinkTimer = randomFloat(rng, 0.0f, 2.0f);
	for (size_t i = 0; i < this->creatures.size(); i++)
	{
		if (!this->creatures[i].gone) continue;
		this->creatures[i] = creature;
		return static_cast<int>(i);
	}

	this->creatures.push_back(creature);
	return static_cast<int>(this->creatures.size()) - 1;
}

void arena::CreatureSystem::vanishFarFrom(float x, float z, float distance)
{
	// Vivas e corpos muito longe somem (o ermo não guarda criaturas).
	for (Creature &c : this->creatures)
	{
		const float dx = c.x - x, dz = c.z - z;
		if (c.gone || dx * dx + dz * dz < distance * distance) continue;
		c.gone = true;
		c.state = Creature::State::Dead;
	}
}

int arena::CreatureSystem::activeNear(float x, float z, float distance) const
{
	int count = 0;
	for (const Creature &c : this->creatures)
	{
		if (c.gone || c.state == Creature::State::Dying || c.state == Creature::State::Dead) continue;
		const float dx = c.x - x, dz = c.z - z;
		if (dx * dx + dz * dz < distance * distance) count++;
	}
	return count;
}
