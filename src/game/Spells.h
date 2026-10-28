#pragma once

#include <array>
#include <random>
#include <string>
#include <vector>

#include "../assets/AssetManager.h"

namespace arena
{
	// Formato do SPELLSG.65 (128 registros de 85 bytes): parâmetros [6][3] (linha = parâmetro %0..%5,
	// coluna = efeito), alvo, elemento, até 3 efeitos com sub-efeito e atributo, custo e nome.
	enum class SpellTarget { Caster = 0, Touch = 1, Ranged = 2, AreaCaster = 3, AreaRanged = 4, Other = 255 };
	enum class SpellElement { Fire = 0, Cold = 1, Poison = 2, Shock = 3, Magic = 4, None = 5 };

	// Efeitos (índice do arquivo). O modelo de descrição no SPELLMKR.TXT segue a mesma ordem.
	enum class SpellEffectType
	{
		Cause = 0, ContinuousDamage = 1, Create = 2, Cure = 3, Damage = 4, DesignateNonTarget = 5, Destroy = 6,
		Disintegrate = 7, Dispel = 8, Drain = 9, ElementalResistance = 10, Fortify = 11, Heal = 12, Transfer = 13,
		Trap = 14, Invisibility = 15, Levitate = 16, Light = 17, Lock = 18, Open = 19, Regenerate = 20, Silence = 21,
		SpellAbsorption = 22, SpellReflection = 23, SpellResistance = 24
	};

	struct SpellEffect
	{
		int type = -1;     // SpellEffectType, -1 = vazio
		int sub = 0;       // sub-efeito (ex.: vida/vigor/magia, doença/veneno/paralisia)
		int attribute = 0;
		std::array<int, 6> params{}; // %0..%5

		bool valid() const { return this->type >= 0 && this->type <= 24; }
	};

	struct Spell
	{
		int index = -1; // posição no SPELLSG.65
		std::string originalName;
		SpellTarget target = SpellTarget::Caster;
		SpellElement element = SpellElement::None;
		int flags = 0;
		std::array<SpellEffect, 3> effects{};
		int goldCost = 0; // metade do preço de compra na guilda

		// Custo em pontos de magia. No original é custo ÷ nível; o remake usa o valor de um
		// personagem de nível 10 (custo ÷ 10) até existirem guildas e níveis.
		int castingCost() const { return std::max(1, this->goldCost / 10); }
		bool harmful() const { return this->target != SpellTarget::Caster && this->target != SpellTarget::Other; }
	};

	class SpellLibrary
	{
	public:
		bool load(const AssetManager &assets);
		const std::vector<Spell> &getSpells() const { return this->spells; }
		const Spell *find(int index) const;

	private:
		std::vector<Spell> spells;
	};

	// Valores com escala por nível, como nos modelos do SPELLMKR.TXT:
	// "%0 a %1 pontos + %2 a %3 a cada %4 nível(is)".
	int spellAmount(const SpellEffect &effect, int level, std::mt19937 &rng);
	// Chance em %: "%0% + %1% a cada %4 nível(is)".
	int spellChance(const SpellEffect &effect, int level);
	// Duração em rodadas: "%5 rodada(s) por %2 nível(is)" (ou por nível quando %2 é 0).
	int spellRounds(const SpellEffect &effect, int level);
	constexpr double SecondsPerRound = 5.0; // 1 rodada = 1 minuto do jogo = 5 s reais

	// Índice do modelo de descrição no SPELLMKR.TXT para o efeito/sub-efeito (-1 se não houver).
	int spellTemplateIndex(const SpellEffect &effect);

	// Livro inicial das classes que conjuram (o original vende feitiços na guilda dos magos).
	std::vector<int> starterSpells(int classIndex, bool castsMagic);
}
