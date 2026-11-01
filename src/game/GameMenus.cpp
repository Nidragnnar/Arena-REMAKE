// Janelas do jogo em resolução nativa, no estilo medieval do kit (madeira, dourado e pergaminho).

#include <algorithm>

#include "../render/Ui.h"
#include "GameMenus.h"
#include "Artifacts.h"
#include "Leveling.h"
#include "OriginalSave.h"

namespace
{
	using namespace arena;
	namespace col = kit::color;

	std::string signedValue(int v) { return (v > 0 ? "+" : "") + std::to_string(v); }

	int magicDefenseFor(int willpower) { return willpower <= 38 ? -2 : willpower <= 41 ? -1 : willpower <= 46 ? 0 : (willpower - 46) / 9; }
}

// ---------------------------------------------------------------------------
// Janela do personagem

void arena::CharacterMenu::open(Game &game, Tab startTab)
{
	this->tab = startTab;
	this->frameW = this->frameH = 1; // sem layout até o primeiro desenho
	this->selected = -1;
	this->scroll = 0;
	this->notice.clear();
	this->doll.load(game);
	this->refreshDoll(game);

	// No livro de feitiços, começa no preparado.
	if (startTab == Tab::Spells)
	{
		const CharacterDraft &c = game.getCharacter();
		for (size_t i = 0; i < c.spells.size(); i++)
			if (c.spells[i] == c.readySpell) this->selected = static_cast<int>(i);
	}
}

void arena::CharacterMenu::refreshDoll(Game &game)
{
	// Boneco montado com a paleta da ficha (ou os PNG redesenhados) e recortado.
	this->doll.compose(game, this->dollPixels, this->dollW, this->dollH);
}

int arena::CharacterMenu::rowCount(const Game &game) const
{
	const CharacterDraft &c = game.getCharacter();
	return this->tab == Tab::Inventory ? static_cast<int>(c.inventory.size()) : this->tab == Tab::Spells ? static_cast<int>(c.spells.size()) : 0;
}

void arena::CharacterMenu::layout(const kit::Painter &painter)
{
	const int u = painter.unit();
	const int W = this->frameW, H = this->frameH;
	this->unitPx = u;
	const int w = std::min(W - 8 * u, 560 * u), h = std::min(H - 8 * u, 330 * u);
	this->window = Rect{ (W - w) / 2, (H - h) / 2, w, h };
	const int pad = 10 * u;
	this->closeRect = Rect{ this->window.x + w - pad - 70 * u, this->window.y + pad, 70 * u, 18 * u };
	for (int i = 0; i < 3; i++) this->tabRects[static_cast<size_t>(i)] = Rect{ this->window.x + pad + 170 * u + i * 104 * u, this->window.y + pad + 30 * u, 100 * u, 18 * u };
	const int top = this->window.y + pad + 52 * u;
	this->content = Rect{ this->window.x + pad + 170 * u, top, w - 2 * pad - 170 * u, this->window.y + h - pad - 16 * u - top };
}

void arena::CharacterMenu::select(int row, const Game &game)
{
	const int count = this->rowCount(game);
	if (count <= 0) { this->selected = -1; return; }
	this->selected = std::clamp(row, 0, count - 1);
	if (this->selected < this->scroll) this->scroll = this->selected;
	if (this->selected >= this->scroll + this->visibleRows) this->scroll = this->selected - this->visibleRows + 1;
}

int arena::CharacterMenu::rowAt(int x, int y) const
{
	if (!kit::hit(this->listRect, x, y)) return -1;
	return this->scroll + (y - this->listRect.y) / std::max(1, this->rowH);
}

arena::OverlayResult arena::CharacterMenu::activate(Game &game, const MenuState &state)
{
	OverlayResult result;
	CharacterDraft &c = game.getCharacter();
	if (this->tab == Tab::Inventory)
	{
		result = useInventoryItem(game, this->selected, state.health, state.spellPoints, this->notice);
		if (this->selected >= static_cast<int>(c.inventory.size())) this->selected = static_cast<int>(c.inventory.size()) - 1;
		if (result.inventoryChanged) this->refreshDoll(game);
	}
	else if (this->tab == Tab::Spells && this->selected >= 0 && this->selected < static_cast<int>(c.spells.size()))
	{
		if (const Spell *spell = game.getData().getSpells().find(c.spells[static_cast<size_t>(this->selected)]))
		{
			c.readySpell = spell->index;
			result.inventoryChanged = true; // grava a escolha
			result.message = ui::format(game.getText().get("Spells.Prepared"), { spellName(game, *spell) });
			game.playSound("CLICKS.VOC", 0.5f);
		}
	}

	return result;
}

arena::OverlayResult arena::CharacterMenu::dropSelected(Game &game)
{
	OverlayResult result;
	CharacterDraft &c = game.getCharacter();
	if (this->tab != Tab::Inventory || this->selected < 0 || this->selected >= static_cast<int>(c.inventory.size())) return result;
	c.inventory.erase(c.inventory.begin() + this->selected);
	this->selected = std::min(this->selected, static_cast<int>(c.inventory.size()) - 1);
	result.inventoryChanged = true;
	this->refreshDoll(game);
	return result;
}

arena::OverlayResult arena::CharacterMenu::handleInput(const InputEvent &event, Game &game, const MenuState &state)
{
	OverlayResult result;
	eventToPixels(event, this->frameW, this->frameH, this->mouseX, this->mouseY);
	auto switchTab = [this](Tab next) { if (next != this->tab) { this->tab = next; this->selected = -1; this->scroll = 0; this->notice.clear(); } };
	const int count = this->rowCount(game);

	// Atalhos: a tecla da aba atual fecha; a de outra aba troca.
	auto hotkeyTab = [&](Tab t) { if (this->tab == t) result.close = true; else switchTab(t); };
	if (event.isKey(InputEvent::Key::Escape)) { result.close = true; return result; }
	if (event.isHotkey('C')) { hotkeyTab(Tab::Character); return result; }
	if (event.isHotkey('I')) { hotkeyTab(Tab::Inventory); return result; }
	if (event.isHotkey('B')) { hotkeyTab(Tab::Spells); return result; }
	if (event.isHotkey('1')) switchTab(Tab::Character);
	else if (event.isHotkey('2')) switchTab(Tab::Inventory);
	else if (event.isHotkey('3')) switchTab(Tab::Spells);
	else if (event.isKey(InputEvent::Key::Left)) switchTab(static_cast<Tab>((static_cast<int>(this->tab) + 2) % 3));
	else if (event.isKey(InputEvent::Key::Right)) switchTab(static_cast<Tab>((static_cast<int>(this->tab) + 1) % 3));
	else if (event.isKey(InputEvent::Key::Up)) this->select(this->selected < 0 ? 0 : this->selected - 1, game);
	else if (event.isKey(InputEvent::Key::Down)) this->select(this->selected + 1, game);
	else if (event.isKey(InputEvent::Key::Enter) || event.isHotkey('E') || (this->tab == Tab::Spells && event.isHotkey('Q'))) result = this->activate(game, state);
	else if (event.isHotkey('D')) result = this->dropSelected(game);
	else if (event.type == InputEvent::Type::MouseWheel)
	{
		this->scroll = std::clamp(this->scroll - event.wheel, 0, std::max(0, count - this->visibleRows));
	}
	else if (event.isClick() && this->frameW > 1)
	{
		const int x = this->mouseX, y = this->mouseY;
		this->notice.clear();
		if (kit::hit(this->closeRect, x, y) || !kit::hit(this->window, x, y)) result.close = true;
		for (int i = 0; i < 3; i++) if (kit::hit(this->tabRects[static_cast<size_t>(i)], x, y)) switchTab(static_cast<Tab>(i));
		if (this->primaryEnabled && kit::hit(this->primaryRect, x, y)) result = this->activate(game, state);
		else if (this->secondaryEnabled && kit::hit(this->secondaryRect, x, y)) result = this->dropSelected(game);
		else if (const int row = this->rowAt(x, y); row >= 0 && row < count)
		{
			// Primeiro clique seleciona; clicar de novo no mesmo usa/equipa/prepara.
			if (row == this->selected) result = this->activate(game, state);
			else this->selected = row;
		}
	}

	return result;
}

void arena::CharacterMenu::render(kit::Painter &painter, const Game &game, const MenuState &state)
{
	this->frameW = painter.frame().width;
	this->frameH = painter.frame().height;
	this->layout(painter);
	const int u = this->unitPx;
	const Fonts &fonts = game.getFonts();
	const Localization &text = game.getText();
	const CharacterDraft &c = game.getCharacter();
	const Rect &win = this->window;
	const int pad = 10 * u;

	painter.fill(Rect{ 0, 0, this->frameW, this->frameH }, hud::rgb(0, 0, 0, 150));
	painter.woodPanel(win, 245);

	// Cabeçalho: nome, raça e classe; botão de fechar.
	painter.text(fonts.a, c.name, win.x + pad + 4 * u, win.y + pad, col::GoldBright);
	painter.text(fonts.arena, game.raceName(c.raceIndex, false) + " - " + game.className(c.classIndex), win.x + pad + 4 * u, win.y + pad + 18 * u, col::TextDim);
	painter.button(this->closeRect, text.get("Menu.Close"), kit::hit(this->closeRect, this->mouseX, this->mouseY), false);

	const char *tabKeys[3] = { "Menu.TabCharacter", "Menu.TabInventory", "Menu.TabSpells" };
	for (int i = 0; i < 3; i++)
	{
		const Rect &r = this->tabRects[static_cast<size_t>(i)];
		painter.button(r, text.get(tabKeys[i]), kit::hit(r, this->mouseX, this->mouseY), static_cast<int>(this->tab) == i);
	}

	// Coluna esquerda: retrato (boneco vestido) e barras.
	const int leftW = 160 * u;
	const int barsH = 3 * 16 * u + 34 * u;
	const int portraitH = std::max(40 * u, this->content.y + this->content.h - (win.y + pad + 30 * u) - barsH);
	const int portraitW = std::min(leftW, portraitH * this->dollW / std::max(1, this->dollH));
	Rect portrait{ win.x + pad + (leftW - portraitW) / 2, win.y + pad + 30 * u, portraitW, portraitH };
	painter.fill(Rect{ portrait.x - 2 * u, portrait.y - 2 * u, portrait.w + 4 * u, portrait.h + 4 * u }, col::GoldDim);
	painter.image(this->dollPixels, this->dollW, this->dollH, portrait);

	int by = portrait.y + portrait.h + 8 * u;
	const Rect barArea{ win.x + pad, by, leftW, 12 * u };
	painter.bar(Rect{ barArea.x, by, barArea.w, 12 * u }, state.health, c.maxHealth, col::Health,
		text.get("Sheet.Health") + " " + std::to_string(state.health) + "/" + std::to_string(c.maxHealth)); by += 16 * u;
	painter.bar(Rect{ barArea.x, by, barArea.w, 12 * u }, state.stamina, c.maxStamina, col::Stamina,
		text.get("Sheet.Fatigue") + " " + std::to_string(state.stamina) + "/" + std::to_string(c.maxStamina)); by += 16 * u;
	painter.bar(Rect{ barArea.x, by, barArea.w, 12 * u }, state.spellPoints, c.maxSpellPoints, col::Magic,
		text.get("Sheet.SpellPoints") + " " + std::to_string(state.spellPoints) + "/" + std::to_string(c.maxSpellPoints)); by += 18 * u;
	painter.text(fonts.arena, text.get("Menu.Gold") + " " + std::to_string(c.gold), barArea.x, by, col::GoldBright);
	painter.text(fonts.arena, text.get("Menu.Level") + " " + std::to_string(c.level), barArea.x + barArea.w, by, col::Text, TextAlign::Right);
	const ClassDefinition &def = game.getData().getClass(std::max(0, c.classIndex));
	painter.text(fonts.arena, text.get("Menu.Experience") + " " + std::to_string(state.experience) + " / " + std::to_string(experienceForLevel(def, c.level + 1)),
		barArea.x, by + 12 * u, col::Text);

	// Conteúdo da aba em pergaminho.
	painter.parchmentPanel(this->content);
	this->primaryEnabled = this->secondaryEnabled = false;
	this->listRect = Rect{};
	switch (this->tab)
	{
	case Tab::Character: this->renderCharacter(painter, game, state); break;
	case Tab::Inventory: this->renderInventory(painter, game); break;
	case Tab::Spells: this->renderSpells(painter, game, state); break;
	}

	// Rodapé: dica da aba ou aviso.
	const char *hints[3] = { "Menu.HintCharacter", "Menu.HintInventory", "Menu.HintSpells" };
	const std::string footer = this->notice.empty() ? text.get(hints[static_cast<int>(this->tab)]) : this->notice;
	painter.text(fonts.arena, footer, this->content.x + this->content.w / 2, win.y + win.h - pad - 11 * u,
		this->notice.empty() ? col::TextDim : col::Danger, TextAlign::Center);
}

void arena::CharacterMenu::renderCharacter(kit::Painter &painter, const Game &game, const MenuState &state)
{
	const int u = this->unitPx;
	const Fonts &fonts = game.getFonts();
	const Localization &text = game.getText();
	const CharacterDraft &c = game.getCharacter();
	const Rect &r = this->content;
	const int pad = 12 * u;
	const int colW = (r.w - 3 * pad) / 2;
	const int line = 13 * u;

	painter.text(fonts.a, text.get("Menu.Attributes"), r.x + pad, r.y + pad, col::Ink, TextAlign::Left, false);
	int y = r.y + pad + 20 * u;
	for (int i = 0; i < AttributeCount; i++)
	{
		const int cx = r.x + pad + (i / 4) * (colW + pad);
		const int cy = y + (i % 4) * line;
		painter.text(fonts.arena, game.attributeName(i, false), cx, cy, col::InkSoft, TextAlign::Left, false);
		// Com bônus (encantamentos, itens mágicos, feitiços de reforço): o valor final e o bônus entre parênteses.
		const int bonus = state.attributeBonus[static_cast<size_t>(i)];
		const std::string value = bonus != 0 ? std::to_string(c.attributes[static_cast<size_t>(i)] + bonus) + " (" + signedValue(bonus) + ")" : std::to_string(c.attributes[static_cast<size_t>(i)]);
		painter.text(fonts.arena, value, cx + colW, cy, bonus > 0 ? hud::rgb(40, 110, 30) : bonus < 0 ? col::Danger : col::Ink, TextAlign::Right, false);
	}

	y += 4 * line + 6 * u;
	painter.divider(r.x + pad, y, r.w - 2 * pad, col::InkSoft);
	y += 8 * u;
	painter.text(fonts.a, text.get("Menu.Abilities"), r.x + pad, y, col::Ink, TextAlign::Left, false);
	y += 20 * u;

	std::array<int, AttributeCount> a = c.attributes; // valores com os bônus
	for (int i = 0; i < AttributeCount; i++) a[static_cast<size_t>(i)] += state.attributeBonus[static_cast<size_t>(i)];
	const std::pair<std::string, std::string> derived[] =
	{
		{ text.get("Menu.Damage"), signedValue(damageBonusFor(a[0])) },
		{ text.get("Menu.ToHit"), signedValue(hitBonusFor(a[3])) },
		{ text.get("Menu.Defense"), signedValue(hitBonusFor(a[3])) },
		{ text.get("Menu.MagicResist"), signedValue(magicDefenseFor(a[2])) },
		{ text.get("Menu.MaxWeight"), std::to_string(a[0] * 2) + " kg" },
		{ text.get("Menu.MaxMagic"), std::to_string(c.maxSpellPoints) },
	};
	for (size_t i = 0; i < std::size(derived); i++)
	{
		const int cx = r.x + pad + static_cast<int>(i / 3) * (colW + pad);
		const int cy = y + static_cast<int>(i % 3) * line;
		painter.text(fonts.arena, derived[i].first, cx, cy, col::InkSoft, TextAlign::Left, false);
		painter.text(fonts.arena, derived[i].second, cx + colW, cy, col::Ink, TextAlign::Right, false);
	}

	y += 3 * line + 6 * u;
	painter.divider(r.x + pad, y, r.w - 2 * pad, col::InkSoft);
	y += 8 * u;
	painter.text(fonts.a, text.get("Menu.Effects"), r.x + pad, y, col::Ink, TextAlign::Left, false);
	y += 20 * u;
	if (state.effects.empty()) painter.text(fonts.arena, text.get("Menu.NoEffects"), r.x + pad, y, col::InkSoft, TextAlign::Left, false);
	for (size_t i = 0; i < state.effects.size() && y + line < r.y + r.h; i++, y += line)
	{
		painter.text(fonts.arena, state.effects[i], r.x + pad, y, col::Ink, TextAlign::Left, false);
	}
}

void arena::CharacterMenu::renderInventory(kit::Painter &painter, const Game &game)
{
	const int u = this->unitPx;
	const Fonts &fonts = game.getFonts();
	const Localization &text = game.getText();
	const CharacterDraft &c = game.getCharacter();
	const Rect &r = this->content;
	const int pad = 10 * u;

	// Lista com colunas: item, detalhes, peso.
	this->rowH = 13 * u;
	const int detailsH = 52 * u;
	this->listRect = Rect{ r.x + pad, r.y + pad + 14 * u, r.w - 2 * pad, r.h - 2 * pad - 14 * u - detailsH };
	this->visibleRows = std::max(1, this->listRect.h / this->rowH);
	this->listRect.h = this->visibleRows * this->rowH;
	painter.text(fonts.arena, text.get("Menu.ColumnItem"), this->listRect.x + 16 * u, r.y + pad, col::InkSoft, TextAlign::Left, false);
	painter.text(fonts.arena, text.get("Menu.ColumnWeight"), this->listRect.x + this->listRect.w - 4 * u, r.y + pad, col::InkSoft, TextAlign::Right, false);
	painter.inset(this->listRect, true);

	const ItemTables &tables = game.getData().getItems();
	const int count = static_cast<int>(c.inventory.size());
	this->scroll = std::clamp(this->scroll, 0, std::max(0, count - this->visibleRows));
	if (count == 0) painter.text(fonts.arena, text.get("Items.Empty"), this->listRect.x + this->listRect.w / 2, this->listRect.y + 6 * u, col::InkSoft, TextAlign::Center, false);
	for (int i = 0; i < this->visibleRows && this->scroll + i < count; i++)
	{
		const int index = this->scroll + i;
		const Item &item = c.inventory[static_cast<size_t>(index)];
		const Rect row{ this->listRect.x + u, this->listRect.y + i * this->rowH, this->listRect.w - 2 * u, this->rowH };
		painter.rowHighlight(row, index == this->selected, kit::hit(row, this->mouseX, this->mouseY), true);
		const int ty = row.y + (this->rowH - fonts.arena.getHeight() * u) / 2;
		if (item.equipped)
		{
			// Losango dourado marca o que está equipado.
			const int cx = row.x + 7 * u, cy = row.y + this->rowH / 2;
			painter.fill(Rect{ cx - u, cy - 2 * u, 2 * u, 4 * u }, col::GoldDim);
			painter.fill(Rect{ cx - 2 * u, cy - u, 4 * u, 2 * u }, col::GoldDim);
		}
		painter.text(fonts.arena, itemName(game, item), row.x + 15 * u, ty, item.equipped ? hud::rgb(120, 60, 0) : col::Ink, TextAlign::Left, false);
		painter.text(fonts.arena, kilogramsText(itemWeight(item, tables)), row.x + row.w - 4 * u, ty, col::InkSoft, TextAlign::Right, false);
	}

	// Detalhes do selecionado e botões.
	const int dy = this->listRect.y + this->listRect.h + 6 * u;
	const std::string weight = ui::format(text.get("Items.Weight"), { kilogramsText(carriedWeight(game)), kilogramsText(maxCarryWeight(game)) });
	painter.text(fonts.arena, weight, r.x + r.w - pad, dy, col::InkSoft, TextAlign::Right, false);
	if (this->selected >= 0 && this->selected < count)
	{
		const Item &item = c.inventory[static_cast<size_t>(this->selected)];
		painter.text(fonts.arena, itemName(game, item), r.x + pad, dy, col::Ink, TextAlign::Left, false);
		painter.text(fonts.arena, itemDetails(game, item), r.x + pad, dy + 12 * u, col::InkSoft, TextAlign::Left, false);
		const bool usable = item.kind == ItemKind::Potion || (item.isMagicItem() && item.magicGroup <= 2)
			|| (item.kind == ItemKind::Trinket && item.artifact >= 0 && (item.type == 14 || item.type == 15)); // poção, item de feitiço, Oghma Infinium, Cofre de Orgnum
		const char *action = usable ? "Menu.Use" : item.equipped ? "Menu.Unequip" : "Menu.Equip";
		this->primaryRect = Rect{ r.x + pad, r.y + r.h - pad - 18 * u, 90 * u, 18 * u };
		this->secondaryRect = Rect{ r.x + pad + 96 * u, r.y + r.h - pad - 18 * u, 80 * u, 18 * u };
		this->primaryEnabled = this->secondaryEnabled = true;
		painter.button(this->primaryRect, text.get(action), kit::hit(this->primaryRect, this->mouseX, this->mouseY), false);
		painter.button(this->secondaryRect, text.get("Menu.Drop"), kit::hit(this->secondaryRect, this->mouseX, this->mouseY), false);
	}
	else
	{
		painter.text(fonts.arena, text.get("Menu.SelectItem"), r.x + pad, dy, col::InkSoft, TextAlign::Left, false);
	}
}

void arena::CharacterMenu::renderSpells(kit::Painter &painter, const Game &game, const MenuState &state)
{
	const int u = this->unitPx;
	const Fonts &fonts = game.getFonts();
	const Localization &text = game.getText();
	const CharacterDraft &c = game.getCharacter();
	const Rect &r = this->content;
	const int pad = 10 * u;

	if (c.spells.empty())
	{
		painter.paragraph(fonts.arena, text.get("Spells.Empty"), Rect{ r.x + pad, r.y + pad, r.w - 2 * pad, r.h - 2 * pad }, col::Ink);
		return;
	}

	// Lista à esquerda (nome e custo), detalhes à direita.
	this->rowH = 13 * u;
	const int listW = (r.w - 3 * pad) * 45 / 100;
	this->listRect = Rect{ r.x + pad, r.y + pad + 14 * u, listW, r.h - 2 * pad - 14 * u };
	this->visibleRows = std::max(1, this->listRect.h / this->rowH);
	this->listRect.h = this->visibleRows * this->rowH;
	painter.text(fonts.arena, text.get("Menu.ColumnSpell"), this->listRect.x + 4 * u, r.y + pad, col::InkSoft, TextAlign::Left, false);
	painter.text(fonts.arena, text.get("Menu.ColumnCost"), this->listRect.x + this->listRect.w - 4 * u, r.y + pad, col::InkSoft, TextAlign::Right, false);
	painter.inset(this->listRect, true);

	const SpellLibrary &library = game.getData().getSpells();
	const int count = static_cast<int>(c.spells.size());
	this->scroll = std::clamp(this->scroll, 0, std::max(0, count - this->visibleRows));
	if (this->selected < 0) this->selected = 0;
	for (int i = 0; i < this->visibleRows && this->scroll + i < count; i++)
	{
		const int index = this->scroll + i;
		const Spell *spell = library.find(c.spells[static_cast<size_t>(index)]);
		if (!spell) continue;
		const Rect row{ this->listRect.x + u, this->listRect.y + i * this->rowH, this->listRect.w - 2 * u, this->rowH };
		painter.rowHighlight(row, index == this->selected, kit::hit(row, this->mouseX, this->mouseY), true);
		const int ty = row.y + (this->rowH - fonts.arena.getHeight() * u) / 2;
		const bool ready = spell->index == c.readySpell;
		painter.text(fonts.arena, spellName(game, *spell), row.x + 4 * u, ty, ready ? hud::rgb(120, 60, 0) : col::Ink, TextAlign::Left, false);
		const int cost = artifact::isGranted(c, spell->index) ? 0 : spell->castingCost();
		painter.text(fonts.arena, std::to_string(cost), row.x + row.w - 4 * u, ty, cost > state.spellPoints ? hud::rgb(160, 30, 20) : col::InkSoft, TextAlign::Right, false);
	}

	// Detalhes do feitiço selecionado.
	const Spell *spell = (this->selected >= 0 && this->selected < count) ? library.find(c.spells[static_cast<size_t>(this->selected)]) : nullptr;
	if (!spell) return;
	const int dx = this->listRect.x + this->listRect.w + pad;
	const int dw = r.x + r.w - pad - dx;
	int y = r.y + pad;
	painter.text(fonts.a, spellName(game, *spell), dx, y, col::Ink, TextAlign::Left, false);
	y += 20 * u;
	if (spell->index == c.readySpell)
	{
		painter.text(fonts.arena, text.get("Menu.Prepared"), dx, y, hud::rgb(120, 60, 0), TextAlign::Left, false);
		y += 13 * u;
	}

	const std::pair<std::string, std::string> facts[] =
	{
		{ text.get("Menu.TargetLabel"), text.get("Spells.Target" + std::to_string(std::min(static_cast<int>(spell->target), 4))) },
		{ text.get("Menu.ElementLabel"), text.get("Spells.Element" + std::to_string(static_cast<int>(spell->element))) },
		{ text.get("Menu.CostLabel"), ui::format(text.get("Menu.SpellCost"), { std::to_string(artifact::isGranted(c, spell->index) ? 0 : spell->castingCost()) }) },
	};
	for (const auto &[label, value] : facts)
	{
		painter.text(fonts.arena, label, dx, y, col::InkSoft, TextAlign::Left, false);
		painter.text(fonts.arena, value, dx + dw, y, col::Ink, TextAlign::Right, false);
		y += 13 * u;
	}

	y += 4 * u;
	painter.divider(dx, y, dw, col::InkSoft);
	y += 8 * u;
	const int buttonY = r.y + r.h - pad - 18 * u;
	for (const std::string &line : spellEffectLines(game, *spell, 1))
	{
		y += painter.paragraph(fonts.arena, line, Rect{ dx, y, dw, buttonY - 6 * u - y }, col::Ink) + 4 * u;
	}

	this->primaryRect = Rect{ dx, buttonY, std::min(dw, 120 * u), 18 * u };
	this->primaryEnabled = spell->index != c.readySpell;
	painter.button(this->primaryRect, text.get("Menu.Prepare"), this->primaryEnabled && kit::hit(this->primaryRect, this->mouseX, this->mouseY),
		false, this->primaryEnabled);
}

// ---------------------------------------------------------------------------
// Saque

void arena::LootMenu::open(Loot *source, std::string lootTitle)
{
	this->loot = source;
	this->title = std::move(lootTitle);
	this->notice.clear();
	this->frameW = this->frameH = 1;
}

int arena::LootMenu::rowAt(int x, int y) const
{
	if (!kit::hit(this->listRect, x, y)) return -1;
	return (y - this->listRect.y) / std::max(1, this->rowH);
}

arena::OverlayResult arena::LootMenu::takeAll(Game &game)
{
	OverlayResult result;
	while (this->loot && !this->isEmpty())
	{
		OverlayResult step = takeLoot(game, *this->loot, 0, this->notice);
		if (!step.inventoryChanged) break;
		result.inventoryChanged = true;
		result.message = step.message;
	}

	result.close = this->isEmpty();
	return result;
}

arena::OverlayResult arena::LootMenu::handleInput(const InputEvent &event, Game &game)
{
	OverlayResult result;
	eventToPixels(event, this->frameW, this->frameH, this->mouseX, this->mouseY);
	if (event.isKey(InputEvent::Key::Escape) || (event.isHotkey('E') && this->isEmpty()))
	{
		result.close = true;
		return result;
	}

	if (event.isHotkey('T')) return this->takeAll(game);
	if (event.isKey(InputEvent::Key::Enter) && this->loot) result = takeLoot(game, *this->loot, 0, this->notice);
	if (event.isClick() && this->frameW > 1)
	{
		const int x = this->mouseX, y = this->mouseY;
		if (kit::hit(this->closeRect, x, y) || !kit::hit(this->window, x, y)) result.close = true;
		else if (kit::hit(this->takeAllRect, x, y)) return this->takeAll(game);
		else if (const int row = this->rowAt(x, y); row >= 0 && this->loot) result = takeLoot(game, *this->loot, row, this->notice);
	}

	if (this->isEmpty() && result.inventoryChanged) result.close = true;
	return result;
}

void arena::LootMenu::render(kit::Painter &painter, const Game &game)
{
	this->frameW = painter.frame().width;
	this->frameH = painter.frame().height;
	const int u = this->unitPx = painter.unit();
	const Fonts &fonts = game.getFonts();
	const Localization &text = game.getText();
	const int w = std::min(this->frameW - 8 * u, 260 * u), h = std::min(this->frameH - 8 * u, 190 * u);
	this->window = Rect{ (this->frameW - w) / 2, (this->frameH - h) / 2, w, h };
	const int pad = 10 * u;

	painter.woodPanel(this->window, 240);
	painter.text(fonts.a, this->title, this->window.x + w / 2, this->window.y + pad, col::GoldBright, TextAlign::Center);
	painter.divider(this->window.x + pad, this->window.y + pad + 20 * u, w - 2 * pad, col::GoldDim);

	this->rowH = 13 * u;
	this->listRect = Rect{ this->window.x + pad, this->window.y + pad + 28 * u, w - 2 * pad, h - 2 * pad - 28 * u - 40 * u };
	this->visibleRows = std::max(1, this->listRect.h / this->rowH);
	painter.inset(this->listRect, false);

	int row = 0;
	auto line = [&](const std::string &label, const std::string &detail, uint32_t tint)
	{
		if (row >= this->visibleRows) return;
		const Rect r{ this->listRect.x + u, this->listRect.y + row * this->rowH, this->listRect.w - 2 * u, this->rowH };
		const bool hovered = kit::hit(r, this->mouseX, this->mouseY);
		painter.rowHighlight(r, false, hovered, false);
		const int ty = r.y + (this->rowH - fonts.arena.getHeight() * u) / 2;
		painter.text(fonts.arena, label, r.x + 4 * u, ty, hovered ? col::GoldBright : tint);
		if (!detail.empty()) painter.text(fonts.arena, detail, r.x + r.w - 4 * u, ty, col::TextDim, TextAlign::Right);
		row++;
	};

	if (this->loot)
	{
		if (this->loot->gold > 0) line(ui::format(text.get("Loot.GoldLine"), { std::to_string(this->loot->gold) }), std::string(), col::GoldBright);
		for (const Item &item : this->loot->items) line(itemName(game, item), kilogramsText(itemWeight(item, game.getData().getItems())) + " kg", col::Text);
	}

	if (this->isEmpty()) painter.text(fonts.arena, text.get("Loot.Empty"), this->listRect.x + this->listRect.w / 2, this->listRect.y + 6 * u, col::TextDim, TextAlign::Center);

	const int by = this->window.y + h - pad - 18 * u;
	this->takeAllRect = Rect{ this->window.x + pad, by, 100 * u, 18 * u };
	this->closeRect = Rect{ this->window.x + w - pad - 80 * u, by, 80 * u, 18 * u };
	painter.button(this->takeAllRect, text.get("Loot.TakeAll"), kit::hit(this->takeAllRect, this->mouseX, this->mouseY), false, !this->isEmpty());
	painter.button(this->closeRect, text.get("Menu.Close"), kit::hit(this->closeRect, this->mouseX, this->mouseY), false);
	const std::string hint = this->notice.empty() ? text.get("Loot.Hint") : this->notice;
	painter.text(fonts.arena, hint, this->window.x + w / 2, by - 14 * u, this->notice.empty() ? col::TextDim : col::Danger, TextAlign::Center);
}

// ---------------------------------------------------------------------------
// Subida de nível

void arena::LevelUpMenu::open(int gained)
{
	this->healthGained = gained;
	this->selected = 0;
	this->frameW = this->frameH = 1;
}

void arena::LevelUpMenu::change(Game &game, int attribute, int delta)
{
	CharacterDraft &c = game.getCharacter();
	auto &value = c.attributes[static_cast<size_t>(attribute)];
	auto &added = c.addedPoints[static_cast<size_t>(attribute)];
	if (delta > 0 && c.levelPoints > 0 && value < MaxAttribute)
	{
		value++; added++; c.levelPoints--;
	}
	else if (delta < 0 && added > 0)
	{
		value--; added--; c.levelPoints++;
	}
}

arena::OverlayResult arena::LevelUpMenu::handleInput(const InputEvent &event, Game &game)
{
	OverlayResult result;
	eventToPixels(event, this->frameW, this->frameH, this->mouseX, this->mouseY);
	CharacterDraft &c = game.getCharacter();
	auto confirm = [&]()
	{
		if (c.levelPoints > 0) return;
		recalculateDerived(c, game.getData().getClass(std::max(0, c.classIndex)));
		c.addedPoints.fill(0);
		result.close = true;
		result.inventoryChanged = true; // grava
		result.message = ui::format(game.getText().get("Level.Confirmed"), { std::to_string(c.level) });
	};

	if (event.isKey(InputEvent::Key::Up)) this->selected = (this->selected + AttributeCount - 1) % AttributeCount;
	else if (event.isKey(InputEvent::Key::Down)) this->selected = (this->selected + 1) % AttributeCount;
	else if (event.isKey(InputEvent::Key::Right)) this->change(game, this->selected, +1);
	else if (event.isKey(InputEvent::Key::Left)) this->change(game, this->selected, -1);
	else if (event.isKey(InputEvent::Key::Enter)) confirm();
	else if (event.isClick() && this->frameW > 1)
	{
		for (int i = 0; i < AttributeCount; i++)
		{
			if (kit::hit(this->plusRects[static_cast<size_t>(i)], this->mouseX, this->mouseY)) { this->selected = i; this->change(game, i, +1); }
			if (kit::hit(this->minusRects[static_cast<size_t>(i)], this->mouseX, this->mouseY)) { this->selected = i; this->change(game, i, -1); }
		}
		if (kit::hit(this->doneRect, this->mouseX, this->mouseY)) confirm();
	}

	return result;
}

void arena::LevelUpMenu::render(kit::Painter &painter, const Game &game)
{
	this->frameW = painter.frame().width;
	this->frameH = painter.frame().height;
	const int u = painter.unit();
	const Fonts &fonts = game.getFonts();
	const Localization &text = game.getText();
	const CharacterDraft &c = game.getCharacter();
	const ClassDefinition &def = game.getData().getClass(std::max(0, c.classIndex));
	const int w = std::min(this->frameW - 8 * u, 300 * u), h = std::min(this->frameH - 8 * u, 268 * u);
	this->window = Rect{ (this->frameW - w) / 2, (this->frameH - h) / 2, w, h };
	const int pad = 12 * u;

	painter.fill(Rect{ 0, 0, this->frameW, this->frameH }, hud::rgb(0, 0, 0, 160));
	painter.woodPanel(this->window, 245);
	painter.text(fonts.a, ui::format(text.get("Level.Title"), { std::to_string(c.level) }), this->window.x + w / 2, this->window.y + pad, col::GoldBright, TextAlign::Center);
	painter.text(fonts.arena, ui::format(text.get("Level.Health"), { std::to_string(this->healthGained), std::to_string(c.maxHealth) }),
		this->window.x + w / 2, this->window.y + pad + 22 * u, col::Text, TextAlign::Center);
	painter.divider(this->window.x + pad, this->window.y + pad + 36 * u, w - 2 * pad, col::GoldDim);
	painter.text(fonts.arena, ui::format(text.get("Level.Points"), { std::to_string(c.levelPoints) }), this->window.x + w / 2, this->window.y + pad + 44 * u,
		c.levelPoints > 0 ? col::GoldBright : col::TextDim, TextAlign::Center);

	const int rowH = 17 * u;
	int y = this->window.y + pad + 60 * u;
	for (int i = 0; i < AttributeCount; i++, y += rowH)
	{
		const Rect row{ this->window.x + pad, y, w - 2 * pad, rowH - 2 * u };
		painter.rowHighlight(row, i == this->selected, kit::hit(row, this->mouseX, this->mouseY), false);
		const int ty = row.y + (row.h - fonts.arena.getHeight() * u) / 2;
		painter.text(fonts.arena, game.attributeName(i, false), row.x + 6 * u, ty, col::Text);
		const int added = c.addedPoints[static_cast<size_t>(i)];
		painter.text(fonts.arena, std::to_string(c.attributes[static_cast<size_t>(i)]), row.x + row.w - 64 * u, ty, added > 0 ? hud::rgb(140, 220, 120) : col::Text, TextAlign::Right);
		this->minusRects[static_cast<size_t>(i)] = Rect{ row.x + row.w - 52 * u, row.y + u, 22 * u, row.h - 2 * u };
		this->plusRects[static_cast<size_t>(i)] = Rect{ row.x + row.w - 26 * u, row.y + u, 22 * u, row.h - 2 * u };
		painter.button(this->minusRects[static_cast<size_t>(i)], "-", kit::hit(this->minusRects[static_cast<size_t>(i)], this->mouseX, this->mouseY), false, added > 0);
		painter.button(this->plusRects[static_cast<size_t>(i)], "+", kit::hit(this->plusRects[static_cast<size_t>(i)], this->mouseX, this->mouseY), false,
			c.levelPoints > 0 && c.attributes[static_cast<size_t>(i)] < MaxAttribute);
	}

	// Prévia de magia e vigor com os novos atributos.
	CharacterDraft preview = c;
	recalculateDerived(preview, def);
	painter.text(fonts.arena, ui::format(text.get("Level.Preview"), { std::to_string(preview.maxSpellPoints), std::to_string(preview.maxStamina) }),
		this->window.x + w / 2, y + 2 * u, col::TextDim, TextAlign::Center);

	this->doneRect = Rect{ this->window.x + w / 2 - 60 * u, this->window.y + h - pad - 18 * u, 120 * u, 18 * u };
	painter.button(this->doneRect, text.get("Level.Confirm"), c.levelPoints == 0 && kit::hit(this->doneRect, this->mouseX, this->mouseY), false, c.levelPoints == 0);
}

// ---------------------------------------------------------------------------
// Salvar e carregar

void arena::SaveLoadMenu::open(const Game &game, Mode openMode)
{
	this->mode = openMode;
	this->confirm = -1;
	this->frameW = this->frameH = 1;
	this->selected = 0;
	this->original = false;
	for (int i = 0; i < Game::SaveSlots; i++)
	{
		this->remakeSlots[static_cast<size_t>(i)] = game.readSaveSummary(game.getSlotPath(i + 1));
		const original::Summary s = original::summary(game.getArenaDir(), i + 1);
		this->originalSlots[static_cast<size_t>(i)] = Game::SaveSummary{ s.exists, s.name, std::string(), game.getText().get("Saves.OriginalTag"), s.level };
	}
	this->slots = this->remakeSlots;
}

arena::SaveLoadMenu::Choice arena::SaveLoadMenu::choose(int slot)
{
	Choice choice;
	if (slot < 0 || slot >= Game::SaveSlots) return choice;
	this->selected = slot;
	const bool occupied = this->slots[static_cast<size_t>(slot)].exists;
	if (this->mode == Mode::Load)
	{
		if (occupied) { choice.slot = slot + 1; choice.original = this->original; }
		return choice;
	}

	// Salvar por cima pede confirmação (segundo clique).
	if (occupied && this->confirm != slot) { this->confirm = slot; return choice; }
	choice.slot = slot + 1;
	return choice;
}

arena::SaveLoadMenu::Choice arena::SaveLoadMenu::handleInput(const InputEvent &event, const Game &)
{
	Choice choice;
	eventToPixels(event, this->frameW, this->frameH, this->mouseX, this->mouseY);
	if (event.isKey(InputEvent::Key::Escape)) { choice.close = true; return choice; }
	if (event.isKey(InputEvent::Key::Up)) { this->selected = (this->selected + Game::SaveSlots - 1) % Game::SaveSlots; this->confirm = -1; }
	else if (event.isKey(InputEvent::Key::Down)) { this->selected = (this->selected + 1) % Game::SaveSlots; this->confirm = -1; }
	else if (event.isKey(InputEvent::Key::Enter)) return this->choose(this->selected);
	else if (event.isClick() && this->frameW > 1)
	{
		if (this->mode == Mode::Load && kit::hit(this->sourceRect, this->mouseX, this->mouseY))
		{
			this->original = !this->original;
			this->slots = this->original ? this->originalSlots : this->remakeSlots;
		}
		else if (kit::hit(this->closeRect, this->mouseX, this->mouseY) || !kit::hit(this->window, this->mouseX, this->mouseY)) choice.close = true;
		else if (kit::hit(this->listRect, this->mouseX, this->mouseY)) return this->choose((this->mouseY - this->listRect.y) / std::max(1, this->rowH));
	}

	return choice;
}

void arena::SaveLoadMenu::render(kit::Painter &painter, const Game &game)
{
	this->frameW = painter.frame().width;
	this->frameH = painter.frame().height;
	const int u = painter.unit();
	const Fonts &fonts = game.getFonts();
	const Localization &text = game.getText();
	const int w = std::min(this->frameW - 8 * u, 360 * u), h = std::min(this->frameH - 8 * u, 262 * u);
	this->window = Rect{ (this->frameW - w) / 2, (this->frameH - h) / 2, w, h };
	const int pad = 12 * u;

	painter.fill(Rect{ 0, 0, this->frameW, this->frameH }, hud::rgb(0, 0, 0, 150));
	painter.woodPanel(this->window, 245);
	painter.text(fonts.a, text.get(this->mode == Mode::Save ? "Saves.TitleSave" : this->original ? "Saves.TitleOriginal" : "Saves.TitleLoad"), this->window.x + w / 2, this->window.y + pad, col::GoldBright, TextAlign::Center);
	painter.divider(this->window.x + pad, this->window.y + pad + 22 * u, w - 2 * pad, col::GoldDim);

	this->rowH = 17 * u;
	this->listRect = Rect{ this->window.x + pad, this->window.y + pad + 30 * u, w - 2 * pad, Game::SaveSlots * this->rowH };
	painter.inset(this->listRect, false);
	for (int i = 0; i < Game::SaveSlots; i++)
	{
		const Game::SaveSummary &slot = this->slots[static_cast<size_t>(i)];
		const Rect row{ this->listRect.x + u, this->listRect.y + i * this->rowH, this->listRect.w - 2 * u, this->rowH };
		const bool usable = this->mode == Mode::Save || slot.exists;
		painter.rowHighlight(row, i == this->selected, usable && kit::hit(row, this->mouseX, this->mouseY), false);
		const int ty = row.y + (row.h - fonts.arena.getHeight() * u) / 2;
		painter.text(fonts.arena, std::to_string(i + 1) + ".", row.x + 6 * u, ty, col::GoldDim);
		if (slot.exists)
		{
			const std::string label = slot.name + " - " + text.get("Menu.Level") + " " + std::to_string(slot.level) + (slot.location.empty() ? "" : " - " + slot.location);
			painter.text(fonts.arena, label, row.x + 24 * u, ty, col::Text);
			painter.text(fonts.arena, slot.date, row.x + row.w - 6 * u, ty, col::TextDim, TextAlign::Right);
		}
		else
		{
			painter.text(fonts.arena, text.get("Saves.Empty"), row.x + 24 * u, ty, col::TextDim);
		}
	}

	const std::string hint = (this->confirm >= 0) ? ui::format(text.get("Saves.ConfirmOverwrite"), { std::to_string(this->confirm + 1) })
		: text.get(this->mode == Mode::Save ? "Saves.HintSave" : "Saves.HintLoad");
	painter.text(fonts.arena, hint, this->window.x + w / 2, this->listRect.y + this->listRect.h + 8 * u, this->confirm >= 0 ? col::Danger : col::TextDim, TextAlign::Center);
	const int by = this->window.y + h - pad - 18 * u;
	if (this->mode == Mode::Load)
	{
		// Botão para os saves do Arena original (ou de volta aos do remake).
		this->sourceRect = Rect{ this->window.x + w / 2 - 135 * u, by, 130 * u, 18 * u };
		painter.button(this->sourceRect, text.get(this->original ? "Saves.ShowRemake" : "Saves.ShowOriginal"), kit::hit(this->sourceRect, this->mouseX, this->mouseY), false);
		this->closeRect = Rect{ this->window.x + w / 2 + 5 * u, by, 100 * u, 18 * u };
	}
	else this->closeRect = Rect{ this->window.x + w / 2 - 50 * u, by, 100 * u, 18 * u };
	painter.button(this->closeRect, text.get("Menu.Close"), kit::hit(this->closeRect, this->mouseX, this->mouseY), false);
}
