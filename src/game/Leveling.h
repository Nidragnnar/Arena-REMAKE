#pragma once

#include <random>

#include "Game.h"

namespace arena
{
	// Experiência total para chegar ao nível (tabela do Manual): nível 2 = limite inicial da classe
	// (800 a 1200, do executável); cada nível seguinte x1,875 até o 10º e x1,5 depois.
	int experienceForLevel(const ClassDefinition &def, int level);

	// Bônus de vida da Resistência a cada nível (mesma escala do bônus de dano da Força).
	int healthBonusFor(int endurance);

	// Vigor e magia máximos a partir dos atributos atuais.
	void recalculateDerived(CharacterDraft &character, const ClassDefinition &def);

	// Sobe um nível se a experiência permitir: vida + dado da classe + bônus da Resistência e
	// 3 a 6 pontos de bônus para distribuir (UESP: Character Creation). Devolve a vida ganha (0 = não subiu).
	int tryLevelUp(CharacterDraft &character, const ClassDefinition &def, std::mt19937 &rng);

	constexpr int MaxAttribute = 100;
}
