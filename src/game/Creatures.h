#pragma once

#include <algorithm>
#include <array>
#include <map>
#include <random>
#include <vector>

#include "../render3d/Renderer3D.h"
#include "../world/Level.h"
#include "ArenaData.h"

namespace arena
{
	// Quadros de uma criatura já convertidos em texturas, com tamanho no mundo.
	struct CreatureAnimation
	{
		struct Frame { int texture = -1; float width = 0, height = 0; };
		std::array<std::vector<Frame>, 5> directions; // arquivos @1..@5 (6..8 = espelhos de 4..2)
		std::vector<Frame> death;                     // arquivo @6
	};

	struct Creature
	{
		enum class State { Idle, Look, Wander, Chase, Attack, Dying, Dead };

		int definition = 0;
		float x = 0, z = 0;
		float angle = 0;      // direção para onde olha (radianos, plano XZ)
		int hp = 1, maxHp = 1;
		State state = State::Idle;
		double stateTime = 0;
		double cooldown = 0;
		double thinkTimer = 0;
		bool awake = false;
		bool attackResolved = false;
		double hurtFlash = 0;
		double soundTimer = 0;
		double paralyzed = 0; // segundos restantes de paralisia (feitiços)
		bool gone = false;    // sumiu (ermo: longe do jogador); a vaga pode ser reaproveitada
	};

	// Resultado de eventos de combate para mensagens e HUD.
	struct CombatEvent
	{
		enum class Type { CreatureHitPlayer, CreatureMissedPlayer, CreatureNoticed, CreatureSound, CreatureAttackStart };
		Type type = Type::CreatureHitPlayer;
		int creature = -1;
		int amount = 0;
	};

	// Alvo do jogador para os testes de acerto.
	struct PlayerCombatStats
	{
		float x = 0, z = 0;
		int level = 1;
		int defenseBonus = 0; // bônus de defesa (Agilidade)
		std::array<int, 7> partArmor{}; // proteção por parte do corpo (peito, mãos, pernas, ombros, cabeça, pés)
		int shieldArmor = 0;
		bool alive = true;
		bool invisible = false; // feitiço de invisibilidade: as criaturas não percebem nem perseguem
	};

	class CreatureSystem
	{
	public:
		void init(const AssetManager &assets, const Palette &palette, const ArenaData &data, const Level &level, std::mt19937 &rng);
		void update(double dt, const Level &level, const PlayerCombatStats &player, std::mt19937 &rng, std::vector<CombatEvent> &events);
		void draw(Renderer3D &renderer, const Level &level, float eyeX, float eyeZ, float drawRadius) const;

		// Criatura viva mais próxima dentro do alcance e do cone à frente (-1 se nenhuma).
		int findTarget(float px, float pz, float yaw, float reach, float coneCos) const;
		// Aplica dano; retorna true se a criatura morreu.
		bool damage(int index, int amount);
		void paralyze(int index, double seconds) { this->creatures[static_cast<size_t>(index)].paralyzed = std::max(this->creatures[static_cast<size_t>(index)].paralyzed, seconds); }
		// Estado salvo: vida e posição (morta = corpo no chão).
		// Ermo: cria uma criatura agora (reaproveita vagas sumidas) e some com as distantes.
		int spawn(const AssetManager &assets, const Palette &palette, int definition, float x, float z, std::mt19937 &rng);
		void vanishFarFrom(float x, float z, float distance);
		int activeNear(float x, float z, float distance) const;
		const CreatureDefinition *definition(int index) const { return index >= 0 && index < static_cast<int>(this->definitions.size()) ? &this->definitions[static_cast<size_t>(index)] : nullptr; }

		void restore(int index, int hp, float x, float z, bool dead)
		{
			if (index < 0 || index >= static_cast<int>(this->creatures.size())) return;
			Creature &c = this->creatures[static_cast<size_t>(index)];
			c.x = x; c.z = z;
			c.hp = std::clamp(hp, 0, c.maxHp);
			if (dead) { c.state = Creature::State::Dead; c.hp = 0; }
		}
		bool isAlive(int index) const { const auto s = this->creatures[static_cast<size_t>(index)].state; return s != Creature::State::Dying && s != Creature::State::Dead; }
		bool blocks(float x, float z, float radius) const;

		const std::vector<Creature> &getCreatures() const { return this->creatures; }
		const CreatureDefinition &definitionOf(int index) const { return this->definitions[static_cast<size_t>(this->creatures[static_cast<size_t>(index)].definition)]; }
		int aliveCount() const;

	private:
		bool loadAnimation(const AssetManager &assets, const Palette &palette, int definitionIndex);
		bool canStand(const Level &level, float x, float z, size_t self, const PlayerCombatStats &player) const;
		bool hasLineOfSight(const Level &level, float x0, float z0, float x1, float z1) const;
		void moveTowards(Creature &creature, size_t self, const Level &level, const PlayerCombatStats &player, float dirX, float dirZ, float distance);

		std::vector<CreatureDefinition> definitions;
		std::map<int, CreatureAnimation> animations;
		std::vector<Texture> textures;
		std::vector<Creature> creatures;
	};
}
