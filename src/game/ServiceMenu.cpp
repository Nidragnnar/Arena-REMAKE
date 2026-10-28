// Atendimento nos prédios da cidade (loja, taverna, templo e guilda dos magos).

#include <algorithm>
#include <random>

#include "../render/Ui.h"
#include "GameMenus.h"
#include "ServiceMenu.h"

namespace
{
	using namespace arena;
	namespace col = kit::color;

	enum Action { None, BuyItem, SellItem, Drink, Room, Heal, BuySpell, BuyPotion };

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
		else
		{
			for (int i = 0; i < 4; i++)
				this->rows.push_back({ text.get("Shop.Room" + std::to_string(i)), text.get("Shop.RoomDetail"), this->price(RoomPrices[i]), Room, i, true });
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
				if (spell.goldCost > 0 && std::find(c.spells.begin(), c.spells.end(), spell.index) == c.spells.end()) list.push_back(&spell);
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
	else if ((event.isKey(InputEvent::Key::Left) || event.isKey(InputEvent::Key::Right)) && this->kind != Kind::Temple)
	{
		this->tab = 1 - this->tab; // as outras janelas têm duas abas
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
	case Kind::Tavern: tabs = { text.get("Shop.TabDrinks"), text.get("Shop.TabRooms") }; break;
	case Kind::Temple: tabs = { text.get("Shop.TabTemple") }; break;
	case Kind::MagesGuild: tabs = { text.get("Shop.TabSpells"), text.get("Shop.TabPotions") }; break;
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

	const char *actionKey = this->kind == Kind::Equipment && this->tab == 1 ? "Shop.ActionSell" : this->kind == Kind::Tavern ? (this->tab == 0 ? "Shop.ActionDrink" : "Shop.ActionRent")
		: this->kind == Kind::Temple ? "Shop.ActionHeal" : "Shop.ActionBuy";
	this->actionRect = Rect{ this->window.x + w / 2 - 125 * u, by, 120 * u, 20 * u };
	painter.button(this->actionRect, text.get(actionKey), kit::hit(this->actionRect, this->mouseX, this->mouseY), false, this->selected >= 0);
	this->closeRect = Rect{ this->window.x + w / 2 + 5 * u, by, 120 * u, 20 * u };
	painter.button(this->closeRect, text.get("Menu.Close"), kit::hit(this->closeRect, this->mouseX, this->mouseY), false);
}
