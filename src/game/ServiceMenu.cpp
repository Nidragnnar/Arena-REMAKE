// Atendimento nos prédios da cidade (loja, taverna, templo e guilda dos magos).

#include <algorithm>
#include <chrono>
#include <random>

#include "../render/Ui.h"
#include "GameMenus.h"
#include "SpellbookUi.h"
#include "ServiceMenu.h"

namespace
{
	using namespace arena;
	namespace col = kit::color;

	enum Action { None, BuyItem, SellItem, Drink, Room, Heal, BuySpell, BuyPotion, Talk = 20 };

	// Modelos da criação de feitiços: efeito, sub (-1 = o elemento escolhido), se é de ataque
	// (precisa de alvo) e o peso no custo. Os parâmetros saem da potência (ver makerSpell).
	struct MakerTemplate { const char *key; SpellEffectType type; int sub; bool offensive; int weight; };
	constexpr MakerTemplate MakerTemplates[] =
	{
		{ "Maker.Damage", SpellEffectType::Damage, 0, true, 6 },
		{ "Maker.Poison", SpellEffectType::ContinuousDamage, 0, true, 5 },
		{ "Maker.Paralyze", SpellEffectType::Cause, 2, true, 8 },
		{ "Maker.Heal", SpellEffectType::Heal, 1, false, 5 },
		{ "Maker.Shield", SpellEffectType::Create, 0, false, 4 },
		{ "Maker.Light", SpellEffectType::Light, 0, false, 1 },
		{ "Maker.Invisibility", SpellEffectType::Invisibility, 0, false, 8 },
		{ "Maker.Resist", SpellEffectType::ElementalResistance, -1, false, 4 },
		{ "Maker.Strength", SpellEffectType::Fortify, 0, false, 4 },
		{ "Maker.Regenerate", SpellEffectType::Regenerate, 0, false, 6 },
		{ "Maker.Open", SpellEffectType::Open, 0, false, 3 },
		{ "Maker.Levitate", SpellEffectType::Levitate, 0, false, 5 },
	};
	constexpr int MakerTemplateCount = static_cast<int>(sizeof(MakerTemplates) / sizeof(MakerTemplates[0]));
	constexpr SpellTarget MakerTargets[] = { SpellTarget::Touch, SpellTarget::Ranged, SpellTarget::AreaRanged };
	constexpr int MakerTargetCost[] = { 2, 3, 6 }; // ×½: toque 1, à distância 1,5, área 3
	enum MakerButton { MakerPrevEffect = 1, MakerNextEffect, MakerElement, MakerTarget, MakerLess, MakerMore };

	// Quartos da taverna: preço por noite (individual, duplo, suíte, suíte real).
	constexpr int RoomPrices[4] = { 5, 10, 20, 40 };

	// Estoque da loja: armas e peças de armadura com materiais sorteados pela tabela do original.
	std::vector<Item> makeStock(const ItemTables &tables, uint32_t seed)
	{
		std::mt19937 rng(seed);
		auto material = [&]()
		{
			const int roll = static_cast<int>(rng() % 100u);
			for (int m = 0; m < MaterialCount; m++) if (roll < tables.materialChance[static_cast<size_t>(m)]) return m;
			return 1;
		};

		std::vector<Item> stock;
		for (int i = 0; i < 10; i++)
		{
			Item item;
			item.kind = ItemKind::Weapon;
			item.type = static_cast<int>(rng() % WeaponTypeCount);
			item.material = material();
			stock.push_back(item);
		}

		for (int i = 0; i < 10; i++)
		{
			Item item;
			item.kind = ItemKind::Armor;
			item.type = static_cast<int>(rng() % ArmorPieceCount);
			item.armorClass = static_cast<ArmorClass>(rng() % 3u);
			item.material = material();
			if (item.armorClass != ArmorClass::Plate && !item.isShield()) item.material = 1;
			stock.push_back(item);
		}

		// Agrupa: armas, depois armaduras, do mais barato ao mais caro.
		std::stable_sort(stock.begin(), stock.end(), [&tables](const Item &a, const Item &b)
		{
			if (a.kind != b.kind) return a.kind < b.kind;
			return itemPrice(a, tables) < itemPrice(b, tables);
		});
		return stock;
	}
}

void arena::ServiceMenu::open(const Game &game, Kind openKind, const std::string &openTitle, uint32_t stockSeed, int factor, int currentHealth, int currentSpellPoints)
{
	this->kind = openKind;
	this->title = openTitle;
	this->tab = 0;
	this->priceFactor = factor;
	this->health = currentHealth;
	this->spellPoints = currentSpellPoints;
	this->selected = -1;
	this->scroll = 0;
	this->notice.clear();
	this->frameW = this->frameH = 1;
	if (openKind == Kind::Equipment) this->stock = makeStock(game.getData().getItems(), stockSeed);
	this->rebuild(game);
}

void arena::ServiceMenu::rebuild(const Game &game)
{
	const Localization &text = game.getText();
	const ItemTables &tables = game.getData().getItems();
	const CharacterDraft &c = game.getCharacter();
	this->rows.clear();
	switch (this->kind)
	{
	case Kind::Equipment:
		if (this->tab == 0)
		{
			for (size_t i = 0; i < this->stock.size(); i++)
				this->rows.push_back({ itemName(game, this->stock[i]), itemDetails(game, this->stock[i]), this->price(itemPrice(this->stock[i], tables)), BuyItem, static_cast<int>(i), true });
		}
		else
		{
			// Vende pela metade do preço; equipados não entram.
			for (size_t i = 0; i < c.inventory.size(); i++)
			{
				const Item &item = c.inventory[i];
				Row row{ itemName(game, item), item.equipped ? text.get("Shop.Equipped") : itemDetails(game, item), std::max(1, this->price(itemPrice(item, tables)) / 2), SellItem, static_cast<int>(i), !item.equipped };
				this->rows.push_back(row);
			}
		}
		break;
	case Kind::Tavern:
		if (this->tab == 0)
		{
			for (int i = 0; i < 15; i++)
				this->rows.push_back({ text.getOr("Shop.Drink" + std::to_string(i), tables.drinkNames[static_cast<size_t>(i)]), std::string(), this->price(tables.drinkPrice[static_cast<size_t>(i)]), Drink, i, true });
		}
		else if (this->tab == 1)
		{
			for (int i = 0; i < 4; i++)
				this->rows.push_back({ text.get("Shop.Room" + std::to_string(i)), text.get("Shop.RoomDetail"), this->price(RoomPrices[i]), Room, i, true });
		}
		else
		{
			// Conversa: trabalho com o taverneiro e o estranho que vende o paradeiro de artefatos.
			this->rows.push_back({ text.get("Shop.AskWork"), text.get("Shop.AskWorkDetail"), 0, Talk, 1, true });
			this->rows.push_back({ text.get("Shop.AskArtifacts"), text.get("Shop.AskArtifactsDetail"), 0, Talk, 2, true });
		}
		break;
	case Kind::Temple:
	{
		const int missing = std::max(0, c.maxHealth - this->health);
		this->rows.push_back({ text.get("Shop.Heal"), ui::format(text.get("Shop.HealDetail"), { std::to_string(this->health), std::to_string(c.maxHealth) }),
			this->price(std::max(10, missing * 2)), Heal, 0, missing > 0 });
		break;
	}
	case Kind::MagesGuild:
		if (this->tab == 0)
		{
			// Feitiços do livro do original que o personagem ainda não conhece.
			std::vector<const Spell *> list;
			for (const Spell &spell : game.getData().getSpells().getSpells())
				if (spell.goldCost > 0 && !spell.artifactPower() && std::find(c.spells.begin(), c.spells.end(), spell.index) == c.spells.end()) list.push_back(&spell);
			std::stable_sort(list.begin(), list.end(), [](const Spell *a, const Spell *b) { return a->goldCost < b->goldCost; });
			for (const Spell *spell : list)
				this->rows.push_back({ spellName(game, *spell), ui::format(text.get("Shop.SpellDetail"), { std::to_string(spell->castingCost()) }), this->price(spell->goldCost * 2), BuySpell, spell->index, true });
		}
		else
		{
			for (int i = 0; i < PotionTypeCount; i++)
				this->rows.push_back({ text.getOr("Potions.Name" + std::to_string(i), tables.potionNames[static_cast<size_t>(i)]), std::string(), this->price(tables.potionPrice[static_cast<size_t>(i)]), BuyPotion, i, true });
		}
		break;
	}

	if (this->selected >= static_cast<int>(this->rows.size())) this->selected = -1;
}

arena::OverlayResult arena::ServiceMenu::confirm(Game &game, const Row &row)
{
	OverlayResult result;
	const Localization &text = game.getText();
	const ItemTables &tables = game.getData().getItems();
	CharacterDraft &c = game.getCharacter();
	auto fail = [&](const std::string &message) { this->notice = message; this->noticeGood = false; return result; };
	auto done = [&](const std::string &message) { this->notice = message; this->noticeGood = true; };

	if (!row.enabled)
	{
		if (row.action == SellItem) return fail(text.get("Shop.UnequipFirst"));
		if (row.action == Heal) return fail(text.get("Shop.NotHurt"));
		return result;
	}

	if (row.action != SellItem && c.gold < row.price) return fail(ui::format(text.get("Shop.NoGold"), { std::to_string(row.price) }));

	switch (row.action)
	{
	case BuyItem:
	{
		const Item &item = this->stock[static_cast<size_t>(row.index)];
		if (carriedWeight(game) + itemWeight(item, tables) > maxCarryWeight(game)) return fail(text.get("Loot.TooHeavy"));
		c.inventory.push_back(item);
		c.gold -= row.price;
		result.inventoryChanged = true;
		done(ui::format(text.get("Shop.Bought"), { row.label, std::to_string(row.price) }));
		break;
	}
	case SellItem:
		c.gold += row.price;
		c.inventory.erase(c.inventory.begin() + row.index);
		result.inventoryChanged = true;
		done(ui::format(text.get("Shop.Sold"), { row.label, std::to_string(row.price) }));
		break;
	case Drink:
		c.gold -= row.price;
		result.healthDelta = 1;
		done(ui::format(text.get("Shop.Drank"), { row.label }));
		break;
	case Room:
		c.gold -= row.price;
		result.rested = true;
		result.close = true;
		result.message = ui::format(text.get("Shop.Slept"), { row.label });
		break;
	case Heal:
		c.gold -= row.price;
		result.healthDelta = c.maxHealth;
		this->health = c.maxHealth;
		done(text.get("Shop.Healed"));
		break;
	case BuySpell:
		if (!game.getData().getClass(std::max(0, c.classIndex)).castsMagic) return fail(text.get("Shop.CannotCast"));
		c.gold -= row.price;
		c.spells.push_back(row.index);
		result.inventoryChanged = true;
		done(ui::format(text.get("Shop.Learned"), { row.label }));
		break;
	case BuyPotion + 1: // criar feitiço (makerOpen)
		break;
	case Talk:
		result.request = row.index;
		result.close = true;
		break;
	case BuyPotion:
	{
		Item potion;
		potion.kind = ItemKind::Potion;
		potion.type = row.index;
		if (carriedWeight(game) + itemWeight(potion, tables) > maxCarryWeight(game)) return fail(text.get("Loot.TooHeavy"));
		c.inventory.push_back(potion);
		c.gold -= row.price;
		result.inventoryChanged = true;
		done(ui::format(text.get("Shop.Bought"), { row.label, std::to_string(row.price) }));
		break;
	}
	default:
		break;
	}

	game.playSound("CLICKS.VOC", 0.6f);
	this->rebuild(game);
	return result;
}

arena::OverlayResult arena::ServiceMenu::handleInput(const InputEvent &event, Game &game)
{
	OverlayResult result;
	eventToPixels(event, this->frameW, this->frameH, this->mouseX, this->mouseY);
	const int count = static_cast<int>(this->rows.size());
	if (event.isKey(InputEvent::Key::Escape)) { result.close = true; return result; }
	if (event.isKey(InputEvent::Key::Up) && count > 0) this->selected = std::max(0, this->selected - 1);
	else if (event.isKey(InputEvent::Key::Down) && count > 0) this->selected = std::min(count - 1, this->selected + 1);
	else if (this->makerOpen() && event.type == InputEvent::Type::TextInput)
	{
		for (char ch : event.text) if (static_cast<unsigned char>(ch) >= 32 && ch != '|' && this->makerName.size() < 24) this->makerName.push_back(ch);
		return result;
	}
	else if (this->makerOpen() && event.isKey(InputEvent::Key::Backspace))
	{
		while (!this->makerName.empty() && (static_cast<unsigned char>(this->makerName.back()) & 0xC0) == 0x80) this->makerName.pop_back();
		if (!this->makerName.empty()) this->makerName.pop_back();
		return result;
	}
	else if (this->makerOpen() && event.isKey(InputEvent::Key::Enter)) return this->createSpell(game);
	else if ((event.isKey(InputEvent::Key::Left) || event.isKey(InputEvent::Key::Right)) && this->kind != Kind::Temple)
	{
		this->tab = (this->tab + (event.isKey(InputEvent::Key::Left) ? this->tabCount() - 1 : 1)) % this->tabCount();
		this->selected = -1;
		this->scroll = 0;
		this->rebuild(game);
	}
	else if (event.isKey(InputEvent::Key::Enter) && this->selected >= 0 && this->selected < count) return this->confirm(game, this->rows[static_cast<size_t>(this->selected)]);
	else if (event.type == InputEvent::Type::MouseWheel)
	{
		this->scroll = std::clamp(this->scroll - (event.dy > 0 ? 1 : -1) * 3, 0, std::max(0, count - this->visibleRows));
	}
	else if (event.isClick() && this->frameW > 1)
	{
		if (kit::hit(this->closeRect, this->mouseX, this->mouseY) || !kit::hit(this->window, this->mouseX, this->mouseY)) { result.close = true; return result; }
		for (size_t t = 0; t < this->tabRects.size(); t++)
		{
			if (!kit::hit(this->tabRects[t], this->mouseX, this->mouseY)) continue;
			this->tab = static_cast<int>(t);
			this->selected = -1;
			this->scroll = 0;
			this->notice.clear();
			this->rebuild(game);
			return result;
		}

		if (this->makerOpen())
		{
			if (kit::hit(this->actionRect, this->mouseX, this->mouseY)) return this->createSpell(game);
			for (const auto &[rect, button] : this->makerButtons)
			{
				if (!kit::hit(rect, this->mouseX, this->mouseY)) continue;
				switch (button)
				{
				case MakerPrevEffect: this->makerEffect = (this->makerEffect + MakerTemplateCount - 1) % MakerTemplateCount; break;
				case MakerNextEffect: this->makerEffect = (this->makerEffect + 1) % MakerTemplateCount; break;
				case MakerElement: this->makerElement = (this->makerElement + 1) % 5; break;
				case MakerTarget: this->makerTarget = (this->makerTarget + 1) % 3; break;
				case MakerLess: this->makerPotency = std::max(1, this->makerPotency - 1); break;
				case MakerMore: this->makerPotency = std::min(10, this->makerPotency + 1); break;
				default: break;
				}
				this->notice.clear();
				game.playSound("CLICKS.VOC", 0.4f);
				return result;
			}
			return result;
		}

		if (kit::hit(this->actionRect, this->mouseX, this->mouseY) && this->selected >= 0 && this->selected < count)
			return this->confirm(game, this->rows[static_cast<size_t>(this->selected)]);

		if (kit::hit(this->listRect, this->mouseX, this->mouseY))
		{
			// Primeiro clique seleciona; clicar de novo na mesma linha confirma.
			const int row = this->scroll + (this->mouseY - this->listRect.y) / std::max(1, this->rowH);
			if (row >= 0 && row < count)
			{
				if (row == this->selected) return this->confirm(game, this->rows[static_cast<size_t>(row)]);
				this->selected = row;
				this->notice.clear();
			}
		}
	}

	if (this->selected >= 0)
	{
		if (this->selected < this->scroll) this->scroll = this->selected;
		else if (this->selected >= this->scroll + this->visibleRows) this->scroll = this->selected - this->visibleRows + 1;
	}

	return result;
}

void arena::ServiceMenu::render(kit::Painter &painter, const Game &game)
{
	this->frameW = painter.frame().width;
	this->frameH = painter.frame().height;
	const int u = painter.unit();
	const Fonts &fonts = game.getFonts();
	const Localization &text = game.getText();
	const CharacterDraft &c = game.getCharacter();
	const int w = std::min(this->frameW - 8 * u, 420 * u), h = std::min(this->frameH - 8 * u, 300 * u);
	this->window = Rect{ (this->frameW - w) / 2, (this->frameH - h) / 2, w, h };
	const int pad = 12 * u;

	painter.fill(Rect{ 0, 0, this->frameW, this->frameH }, hud::rgb(0, 0, 0, 140));
	painter.woodPanel(this->window, 245);
	painter.text(fonts.a, this->title, this->window.x + pad, this->window.y + pad, col::GoldBright);
	painter.text(fonts.arena, ui::format(text.get("Shop.Gold"), { std::to_string(c.gold) }), this->window.x + w - pad, this->window.y + pad + 4 * u, col::GoldBright, TextAlign::Right);

	// Abas.
	std::vector<std::string> tabs;
	switch (this->kind)
	{
	case Kind::Equipment: tabs = { text.get("Shop.TabBuy"), text.get("Shop.TabSell") }; break;
	case Kind::Tavern: tabs = { text.get("Shop.TabDrinks"), text.get("Shop.TabRooms"), text.get("Shop.TabTalk") }; break;
	case Kind::Temple: tabs = { text.get("Shop.TabTemple") }; break;
	case Kind::MagesGuild: tabs = { text.get("Shop.TabSpells"), text.get("Shop.TabPotions"), text.get("Shop.TabMaker") }; break;
	}

	this->tabRects.clear();
	const int tabW = 110 * u, tabY = this->window.y + pad + 24 * u;
	for (size_t t = 0; t < tabs.size(); t++)
	{
		const Rect r{ this->window.x + pad + static_cast<int>(t) * (tabW + 6 * u), tabY, tabW, 18 * u };
		painter.button(r, tabs[t], kit::hit(r, this->mouseX, this->mouseY), static_cast<int>(t) == this->tab);
		this->tabRects.push_back(r);
	}

	// Lista no pergaminho: nome, detalhe e preço.
	const Rect page{ this->window.x + pad, tabY + 24 * u, w - 2 * pad, h - (tabY + 24 * u - this->window.y) - 56 * u };
	painter.parchmentPanel(page);
	if (this->makerOpen())
	{
		this->renderMaker(painter, game, page);
		const int byMaker = this->window.y + h - pad - 20 * u;
		if (!this->notice.empty()) painter.text(fonts.arena, this->notice, this->window.x + w / 2, byMaker - 16 * u, this->noticeGood ? col::GoldBright : col::Danger, TextAlign::Center);
		else painter.text(fonts.arena, text.get("Maker.Hint"), this->window.x + w / 2, byMaker - 16 * u, col::TextDim, TextAlign::Center);
		const Spell spell = this->makerSpell(game);
		this->actionRect = Rect{ this->window.x + w / 2 - 125 * u, byMaker, 120 * u, 20 * u };
		painter.button(this->actionRect, ui::format(text.get("Maker.Create"), { std::to_string(this->price(spell.goldCost * 2)) }), kit::hit(this->actionRect, this->mouseX, this->mouseY), false);
		this->closeRect = Rect{ this->window.x + w / 2 + 5 * u, byMaker, 120 * u, 20 * u };
		painter.button(this->closeRect, text.get("Menu.Close"), kit::hit(this->closeRect, this->mouseX, this->mouseY), false);
		return;
	}
	this->rowH = 22 * u;
	this->listRect = Rect{ page.x + 6 * u, page.y + 6 * u, page.w - 12 * u, page.h - 12 * u };
	this->visibleRows = std::max(1, this->listRect.h / this->rowH);
	this->listRect.h = this->visibleRows * this->rowH;
	const int count = static_cast<int>(this->rows.size());
	this->scroll = std::clamp(this->scroll, 0, std::max(0, count - this->visibleRows));
	if (count == 0) painter.text(fonts.arena, text.get("Shop.Empty"), page.x + page.w / 2, page.y + page.h / 2 - 4 * u, col::InkSoft, TextAlign::Center, false);
	for (int i = 0; i < this->visibleRows && this->scroll + i < count; i++)
	{
		const Row &row = this->rows[static_cast<size_t>(this->scroll + i)];
		const Rect r{ this->listRect.x, this->listRect.y + i * this->rowH, this->listRect.w, this->rowH - u };
		const bool isSelected = this->scroll + i == this->selected;
		painter.rowHighlight(r, isSelected, kit::hit(r, this->mouseX, this->mouseY), true);
		const uint32_t ink = !row.enabled ? col::InkSoft : isSelected ? hud::rgb(120, 60, 0) : col::Ink;
		painter.text(fonts.arena, row.label, r.x + 6 * u, r.y + 2 * u, ink, TextAlign::Left, false);
		if (!row.detail.empty()) painter.text(fonts.arena, row.detail, r.x + 6 * u, r.y + 11 * u, col::InkSoft, TextAlign::Left, false);
		const bool affordable = row.action == SellItem || c.gold >= row.price;
		if (row.action != Talk)
			painter.text(fonts.arena, ui::format(text.get("Shop.Price"), { std::to_string(row.price) }), r.x + r.w - 6 * u, r.y + 6 * u,
				affordable ? hud::rgb(110, 70, 10) : col::Danger, TextAlign::Right, false);
	}

	if (count > this->visibleRows)
	{
		// Barra de rolagem fina à direita.
		const int trackH = this->listRect.h, thumbH = std::max(8 * u, trackH * this->visibleRows / count);
		const int thumbY = this->listRect.y + (trackH - thumbH) * this->scroll / std::max(1, count - this->visibleRows);
		painter.fill(Rect{ page.x + page.w - 4 * u, thumbY, 2 * u, thumbH }, col::InkSoft);
	}

	// Aviso e botões.
	const int by = this->window.y + h - pad - 20 * u;
	if (!this->notice.empty())
		painter.text(fonts.arena, this->notice, this->window.x + w / 2, by - 16 * u, this->noticeGood ? col::GoldBright : col::Danger, TextAlign::Center);
	else
		painter.text(fonts.arena, text.get("Shop.Hint"), this->window.x + w / 2, by - 16 * u, col::TextDim, TextAlign::Center);

	const char *actionKey = this->kind == Kind::Equipment && this->tab == 1 ? "Shop.ActionSell" : this->kind == Kind::Tavern ? (this->tab == 0 ? "Shop.ActionDrink" : this->tab == 1 ? "Shop.ActionRent" : "Shop.ActionAsk")
		: this->kind == Kind::Temple ? "Shop.ActionHeal" : "Shop.ActionBuy";
	this->actionRect = Rect{ this->window.x + w / 2 - 125 * u, by, 120 * u, 20 * u };
	painter.button(this->actionRect, text.get(actionKey), kit::hit(this->actionRect, this->mouseX, this->mouseY), false, this->selected >= 0);
	this->closeRect = Rect{ this->window.x + w / 2 + 5 * u, by, 120 * u, 20 * u };
	painter.button(this->closeRect, text.get("Menu.Close"), kit::hit(this->closeRect, this->mouseX, this->mouseY), false);
}

arena::Spell arena::ServiceMenu::makerSpell(const Game &game) const
{
	// Parâmetros pela potência P (1..10), no formato das tabelas do original (ver Spells.h):
	// pontos "%0 a %1 + %2 a %3 a cada %4 níveis", chance "%0% + %1% a cada %4", rodadas "%5 × nível/%2".
	const MakerTemplate &t = MakerTemplates[this->makerEffect];
	const int P = this->makerPotency;
	Spell spell;
	spell.index = CustomSpellBase; // definido ao criar
	spell.target = t.offensive ? MakerTargets[this->makerTarget] : SpellTarget::Caster;
	spell.element = (t.offensive || t.sub < 0) ? static_cast<SpellElement>(this->makerElement) : SpellElement::Magic;
	SpellEffect &e = spell.effects[0];
	e.type = static_cast<int>(t.type);
	e.sub = t.sub < 0 ? this->makerElement : t.sub;
	switch (t.type)
	{
	case SpellEffectType::Damage: e.params = { 2 * P, 4 * P, 1, P, 2, 0 }; break;
	case SpellEffectType::ContinuousDamage: e.params = { P, 2 * P, 4, 4, 4, 2 }; break;
	case SpellEffectType::Cause: e.params = { 30 + 5 * P, 2, 4, 0, 2, 1 + P / 3 }; break;
	case SpellEffectType::Heal: e.params = { 3 * P, 5 * P, 1, 2, 2, 0 }; break;
	case SpellEffectType::Create: e.params = { 5 * P, P, 0, 0, 2, 0 }; break;
	case SpellEffectType::Light: e.params = { 0, 0, 10, 0, 0, 20 + 10 * P }; break;
	case SpellEffectType::Invisibility: e.params = { 3 * P, 1, 0, 0, 2, 0 }; break;
	case SpellEffectType::ElementalResistance: e.params = { 20 + 6 * P, 2, 5, 0, 2, 5 + 2 * P }; break;
	case SpellEffectType::Fortify: e.params = { 2 + P, 0, 0, 0, 0, 1 + P / 3 }; break;
	case SpellEffectType::Regenerate: e.params = { 1 + P / 3, 1, 0, 0, 1 + P / 2, 0 }; break;
	case SpellEffectType::Open: e.params = { 20 + 8 * P, 2, 0, 0, 2, 0 }; break;
	case SpellEffectType::Levitate: e.params = { 0, 0, 10, 0, 0, 5 + 3 * P }; break;
	default: break;
	}

	// Custo: peso do efeito × potência × alvo (castingCost = ouro ÷ 10).
	spell.goldCost = std::max(10, t.weight * P * 4 * (t.offensive ? MakerTargetCost[this->makerTarget] : 2) / 2);
	const Localization &text = game.getText();
	spell.originalName = this->makerName.empty() ? text.get(t.key) : this->makerName;
	return spell;
}

arena::OverlayResult arena::ServiceMenu::createSpell(Game &game)
{
	OverlayResult result;
	const Localization &text = game.getText();
	CharacterDraft &c = game.getCharacter();
	Spell spell = this->makerSpell(game);
	const int cost = this->price(spell.goldCost * 2);
	this->noticeGood = false;
	if (!game.getData().getClass(std::max(0, c.classIndex)).castsMagic) { this->notice = text.get("Shop.CannotCast"); return result; }
	if (c.gold < cost) { this->notice = ui::format(text.get("Shop.NoGold"), { std::to_string(cost) }); return result; }
	if (c.spells.size() >= 32) { this->notice = text.get("Maker.BookFull"); return result; }

	int next = CustomSpellBase;
	for (const Spell &existing : c.customSpells) next = std::max(next, existing.index + 1);
	spell.index = next;
	c.customSpells.push_back(spell);
	setCustomSpells(c.customSpells);
	c.spells.push_back(spell.index);
	c.gold -= cost;
	this->makerName.clear();
	this->notice = ui::format(text.get("Maker.Created"), { spell.originalName });
	this->noticeGood = true;
	result.inventoryChanged = true;
	game.playSound("CLICKS.VOC", 0.6f);
	return result;
}

void arena::ServiceMenu::renderMaker(kit::Painter &painter, const Game &game, const Rect &page)
{
	const int u = painter.unit();
	const Fonts &fonts = game.getFonts();
	const Localization &text = game.getText();
	const MakerTemplate &t = MakerTemplates[this->makerEffect];
	const Spell spell = this->makerSpell(game);
	this->makerButtons.clear();
	const int left = page.x + 12 * u, labelW = 80 * u, valueX = left + labelW, fieldH = 22 * u;
	int y = page.y + 10 * u;
	auto field = [&](const std::string &label, const std::string &value, int button, int altButton = 0)
	{
		painter.text(fonts.arena, label, left, y + 4 * u, col::InkSoft, TextAlign::Left, false);
		const Rect r{ valueX, y, 170 * u, 17 * u };
		painter.button(r, value, kit::hit(r, this->mouseX, this->mouseY), false);
		this->makerButtons.push_back({ r, button });
		if (altButton)
		{
			const Rect back{ valueX + 176 * u, y, 22 * u, 17 * u };
			painter.button(back, "<", kit::hit(back, this->mouseX, this->mouseY), false);
			this->makerButtons.push_back({ back, altButton });
		}
		y += fieldH;
	};

	field(text.get("Maker.Effect"), text.get(t.key), MakerNextEffect, MakerPrevEffect);
	if (t.offensive || t.sub < 0) field(text.get("Maker.Element"), text.get("Maker.Element" + std::to_string(this->makerElement)), MakerElement);
	if (t.offensive) field(text.get("Maker.Target"), text.get("Maker.Target" + std::to_string(this->makerTarget)), MakerTarget);

	// Potência: - valor +
	painter.text(fonts.arena, text.get("Maker.Potency"), left, y + 4 * u, col::InkSoft, TextAlign::Left, false);
	const Rect less{ valueX, y, 22 * u, 17 * u }, more{ valueX + 148 * u, y, 22 * u, 17 * u };
	painter.button(less, "-", kit::hit(less, this->mouseX, this->mouseY), false);
	painter.button(more, "+", kit::hit(more, this->mouseX, this->mouseY), false);
	painter.text(fonts.arena, std::to_string(this->makerPotency) + " / 10", valueX + 85 * u, y + 4 * u, col::Ink, TextAlign::Center, false);
	this->makerButtons.push_back({ less, MakerLess });
	this->makerButtons.push_back({ more, MakerMore });
	y += fieldH;

	// Nome (digitado).
	painter.text(fonts.arena, text.get("Maker.Name"), left, y + 4 * u, col::InkSoft, TextAlign::Left, false);
	const Rect nameBox{ valueX, y, 198 * u, 17 * u };
	painter.fill(nameBox, hud::rgb(255, 250, 235, 90));
	const auto now = std::chrono::steady_clock::now().time_since_epoch();
	const bool blink = (std::chrono::duration_cast<std::chrono::milliseconds>(now).count() / 500) % 2 == 0;
	painter.text(fonts.arena, (this->makerName.empty() ? text.get(t.key) : this->makerName) + (blink ? "_" : ""), nameBox.x + 5 * u, y + 4 * u,
		this->makerName.empty() ? col::InkSoft : col::Ink, TextAlign::Left, false);
	y += fieldH + 4 * u;

	// Resumo: o que o feitiço faz no nível atual e quanto custa.
	painter.divider(left, y, page.w - 24 * u, col::InkSoft);
	y += 8 * u;
	for (const std::string &line : spellEffectLines(game, spell, game.getCharacter().level))
	{
		y += painter.paragraph(fonts.arena, line, Rect{ left, y, page.w - 24 * u, 30 * u }, col::Ink) + 2 * u;
	}
	painter.text(fonts.arena, ui::format(text.get("Maker.Cost"), { std::to_string(spell.castingCost()) }), left, y + 2 * u, col::InkSoft, TextAlign::Left, false);
}
