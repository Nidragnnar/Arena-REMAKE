#include <algorithm>

#include "Spells.h"

namespace
{
	constexpr size_t RecordSize = 85;
	constexpr size_t NameOffset = 52;
	constexpr size_t NameLength = 33;
}

bool arena::SpellLibrary::load(const AssetManager &assets)
{
	const auto data = assets.read("SPELLSG.65");
	if (!data || data->size() < RecordSize)
	{
		return false;
	}

	this->spells.clear();
	const ByteBuffer &b = *data;
	auto u16 = [&b](size_t at) { return static_cast<int>(b[at] | (b[at + 1] << 8)); };
	for (size_t i = 0; (i + 1) * RecordSize <= b.size(); i++)
	{
		const size_t o = i * RecordSize;
		std::string name;
		for (size_t k = 0; k < NameLength && b[o + NameOffset + k] != 0; k++) name.push_back(static_cast<char>(b[o + NameOffset + k]));
		if (name.empty()) continue;

		Spell spell;
		spell.index = static_cast<int>(i);
		spell.originalName = name;
		spell.target = static_cast<SpellTarget>(b[o + 36]);
		spell.element = static_cast<SpellElement>(std::min<int>(b[o + 38], 5));
		spell.flags = u16(o + 39);
		for (size_t e = 0; e < 3; e++)
		{
			SpellEffect &effect = spell.effects[e];
			effect.type = (b[o + 41 + e] == 255) ? -1 : b[o + 41 + e];
			effect.sub = b[o + 44 + e];
			effect.attribute = b[o + 47 + e];
			for (size_t row = 0; row < 6; row++) effect.params[row] = u16(o + (row * 3 + e) * 2);
		}

		spell.goldCost = u16(o + 50);
		this->spells.push_back(spell);
	}

	return !this->spells.empty();
}

const arena::Spell *arena::SpellLibrary::find(int index) const
{
	for (const Spell &spell : this->spells)
	{
		if (spell.index == index) return &spell;
	}

	return nullptr;
}

int arena::spellAmount(const SpellEffect &effect, int level, std::mt19937 &rng)
{
	const auto &p = effect.params;
	auto roll = [&rng](int lo, int hi) { if (hi < lo) std::swap(lo, hi); return lo + static_cast<int>(rng() % static_cast<uint32_t>(hi - lo + 1)); };
	const int steps = level / std::max(1, p[4]);
	int total = roll(p[0], p[1]);
	for (int i = 0; i < steps; i++) total += roll(p[2], p[3]);
	return total;
}

int arena::spellChance(const SpellEffect &effect, int level)
{
	const auto &p = effect.params;
	return std::clamp(p[0] + p[1] * (level / std::max(1, p[4])), 0, 100);
}

int arena::spellRounds(const SpellEffect &effect, int level)
{
	const auto &p = effect.params;
	return std::max(1, p[5] * std::max(1, level / std::max(1, p[2])));
}

int arena::spellTemplateIndex(const SpellEffect &effect)
{
	switch (static_cast<SpellEffectType>(effect.type))
	{
	case SpellEffectType::Cause:
	{
		// Modelos 0..5: doença, veneno, medo, paralisia, maldição, morte.
		constexpr int Map[] = { 0, 1, 3, 4, 5, 2 };
		return Map[std::clamp(effect.sub, 0, 5)];
	}
	case SpellEffectType::ContinuousDamage: return 6 + std::clamp(effect.sub, 0, 2);
	case SpellEffectType::Create: return 9 + std::clamp(effect.sub, 0, 2);
	case SpellEffectType::Cure:
	{
		// Modelos 12..17: doença, veneno, medo, paralisia, maldição, ressurreição.
		constexpr int Map[] = { 12, 13, 15, 16, 14, 17 };
		return Map[std::clamp(effect.sub, 0, 5)];
	}
	case SpellEffectType::Damage: return 18 + std::clamp(effect.sub, 0, 2);
	case SpellEffectType::DesignateNonTarget: return 21;
	case SpellEffectType::Destroy: return 22 + std::clamp(effect.sub, 0, 1);
	default: break;
	}

	// Demais efeitos: um modelo cada, a partir do 25.
	return (effect.type >= 7 && effect.type <= 24) ? 18 + effect.type : -1;
}

std::vector<int> arena::starterSpells(int classIndex, bool castsMagic)
{
	if (!castsMagic)
	{
		return {};
	}

	// Comuns: Dardo de Fogo, Cura Leve, Luz. Mais um ou dois que combinam com a classe.
	std::vector<int> spells = { 61, 62, 2 };
	switch (classIndex)
	{
	case 0: spells.insert(spells.end(), { 12, 9 }); break;  // Mago: Raio de Gelo, Abrir
	case 1: spells.push_back(7); break;                      // Espadachim Arcano: Escudo
	case 2: spells.push_back(6); break;                      // Mago de Batalha: Fogo do Mago
	case 3: spells.push_back(8); break;                      // Feiticeiro: Choque
	case 4: spells.insert(spells.end(), { 13, 21 }); break; // Curandeiro: Cura, Curar Veneno
	case 5: spells.insert(spells.end(), { 24, 9 }); break;  // Lâmina Noturna: Invisibilidade, Abrir
	default: break;
	}

	return spells;
}
