// Nomes e descrições dos feitiços em PT-BR (modelos do SPELLMKR.TXT traduzidos).

#include <algorithm>

#include "../render/Ui.h"
#include "SpellbookUi.h"

namespace
{
	using namespace arena;

	// Nome do atributo/recurso citado por %c no modelo.
	std::string effectSubject(const Game &game, const SpellEffect &effect)
	{
		const Localization &text = game.getText();
		switch (static_cast<SpellEffectType>(effect.type))
		{
		case SpellEffectType::Heal:
			return text.get(effect.sub == 1 ? "Sheet.Health" : effect.sub == 2 ? "Sheet.SpellPoints" : "Sheet.Fatigue");
		case SpellEffectType::Transfer:
			return effect.sub == 1 ? text.get("Sheet.Health") : game.attributeName(std::clamp(effect.sub, 0, AttributeCount - 1), false);
		case SpellEffectType::ElementalResistance:
			return text.get("Spells.Resist" + std::to_string(std::clamp(effect.sub, 0, 4)));
		case SpellEffectType::Drain:
		case SpellEffectType::Fortify:
			return game.attributeName(std::clamp(effect.sub, 0, AttributeCount - 1), false);
		default:
			return std::string();
		}
	}

	// Remove os ":" finais dos nomes de rótulo da ficha ("Vida:" -> "vida").
	std::string plainLabel(std::string s)
	{
		while (!s.empty() && (s.back() == ':' || s.back() == ' ')) s.pop_back();
		return s;
	}
}

std::string arena::spellName(const Game &game, const Spell &spell)
{
	return game.getText().getOr("Spells.Name" + std::to_string(spell.index), spell.originalName);
}

std::vector<std::string> arena::spellEffectLines(const Game &game, const Spell &spell, int level)
{
	(void)level;
	const Localization &text = game.getText();
	std::vector<std::string> lines;
	for (const SpellEffect &effect : spell.effects)
	{
		if (!effect.valid()) continue;
		const int index = spellTemplateIndex(effect);
		if (index < 0) continue;

		const std::string key = std::string("SpellText.T") + (index < 10 ? "0" : "") + std::to_string(index);
		const std::string pattern = text.getOr(key, std::string());
		if (pattern.empty()) continue;

		std::string out;
		for (size_t i = 0; i < pattern.size(); i++)
		{
			if (pattern[i] != '%' || i + 1 >= pattern.size())
			{
				out.push_back(pattern[i]);
				continue;
			}

			const char code = pattern[++i];
			if (code >= '0' && code <= '5') out += std::to_string(effect.params[static_cast<size_t>(code - '0')]);
			else if (code == '%') out.push_back('%');
			else if (code == 'a') out += "1";
			else if (code == 'b') out += text.get((spell.flags & 256) ? "SpellText.May" : "SpellText.MayNot");
			else if (code == 'c') out += plainLabel(effectSubject(game, effect));
			else if (code == 'f') out += text.get((spell.flags & 128) ? "SpellText.LightWanders" : "SpellText.LightFollows");
		}

		lines.push_back(out);
	}

	return lines;
}
