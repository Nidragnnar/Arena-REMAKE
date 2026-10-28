// Viagem pelo mapa de Tamriel.

#include <algorithm>

#include "../render/Ui.h"
#include "../world/SpriteOverrides.h"
#include "GameMenus.h"
#include "TravelMenu.h"

namespace
{
	using namespace arena;
	namespace col = kit::color;
}

void arena::TravelMenu::open(const Game &game, const CityLocation *from, int currentMonth)
{
	if (this->map.empty())
	{
		// Mapa de Tamriel como está no jogo (com os nomes em inglês da arte; os da lista são traduzidos).
		const IndexedImage tamriel = game.loadImage("TAMRIEL.MNU");
		RgbaImage rgba = loadRgba(tamriel, tamriel.palette ? *tamriel.palette : game.getDefaultPalette(), "TAMRIEL.MNU", 0, 1, false);
		this->map = std::move(rgba.pixels);
		this->mapW = rgba.width;
		this->mapH = rgba.height;
	}

	this->current = from;
	this->month = currentMonth;
	this->frameW = this->frameH = 1;
	this->selectProvince(game, from ? from->province : 0);
}

void arena::TravelMenu::selectProvince(const Game &game, int newProvince)
{
	this->province = newProvince;
	this->places.clear();
	this->selected = this->confirm = -1;
	this->scroll = 0;
	for (const CityLocation &city : game.getData().getCities())
	{
		if (city.province != newProvince || city.name.empty() || &city == this->current) continue;
		const int days = this->current ? game.getData().travelDays(*this->current, city, this->month) : 1;
		this->places.emplace_back(&city, days);
	}

	// Cidades-estado, depois vilas e aldeias; dentro de cada tipo, as mais próximas primeiro.
	std::stable_sort(this->places.begin(), this->places.end(), [](const auto &a, const auto &b)
	{
		if (a.first->type != b.first->type) return a.first->type < b.first->type;
		return a.second < b.second;
	});
}

arena::TravelMenu::Choice arena::TravelMenu::handleInput(const InputEvent &event, const Game &game)
{
	Choice choice;
	eventToPixels(event, this->frameW, this->frameH, this->mouseX, this->mouseY);
	const int count = static_cast<int>(this->places.size());
	auto provinceUnderMouse = [&]() -> int
	{
		if (!kit::hit(this->mapRect, this->mouseX, this->mouseY)) return -1;
		const int px = (this->mouseX - this->mapRect.x) * 320 / std::max(1, this->mapRect.w);
		const int py = (this->mouseY - this->mapRect.y) * 200 / std::max(1, this->mapRect.h);
		const int p = game.getData().provinceAt(px, py);
		if (p >= 0) return p;
		// A Província Imperial fica no meio (o teste de máscara só cobre as 8 de fora).
		return (px >= 103 && px < 234 && py >= 72 && py < 156) ? CenterProvinceID : -1;
	};

	auto go = [&](int index)
	{
		if (index < 0 || index >= count) return;
		if (this->confirm != index) { this->confirm = index; this->selected = index; return; }
		choice.destination = this->places[static_cast<size_t>(index)].first;
		choice.days = this->places[static_cast<size_t>(index)].second;
		choice.close = true;
	};

	if (event.isKey(InputEvent::Key::Escape)) { choice.close = true; return choice; }
	if (event.isKey(InputEvent::Key::Up) && count > 0) { this->selected = std::max(0, this->selected - 1); this->confirm = -1; }
	else if (event.isKey(InputEvent::Key::Down) && count > 0) { this->selected = std::min(count - 1, this->selected + 1); this->confirm = -1; }
	else if (event.isKey(InputEvent::Key::Enter)) go(this->selected);
	else if (event.type == InputEvent::Type::MouseWheel) this->scroll = std::clamp(this->scroll - (event.dy > 0 ? 1 : -1) * 3, 0, std::max(0, count - this->visibleRows));
	else if (event.type == InputEvent::Type::MouseMove) this->hoveredProvince = provinceUnderMouse();
	else if (event.isClick() && this->frameW > 1)
	{
		if (kit::hit(this->closeRect, this->mouseX, this->mouseY) || !kit::hit(this->window, this->mouseX, this->mouseY)) { choice.close = true; return choice; }
		if (kit::hit(this->goRect, this->mouseX, this->mouseY)) { if (this->confirm < 0) this->confirm = this->selected; go(this->selected); return choice; }
		const int p = provinceUnderMouse();
		if (p >= 0) { this->selectProvince(game, p); return choice; }
		if (kit::hit(this->listRect, this->mouseX, this->mouseY))
		{
			const int row = this->scroll + (this->mouseY - this->listRect.y) / std::max(1, this->rowH);
			if (row >= 0 && row < count) { if (row == this->selected) go(row); else { this->selected = row; this->confirm = -1; } }
		}
	}

	if (this->selected >= 0)
	{
		if (this->selected < this->scroll) this->scroll = this->selected;
		else if (this->selected >= this->scroll + this->visibleRows) this->scroll = this->selected - this->visibleRows + 1;
	}

	return choice;
}

void arena::TravelMenu::render(kit::Painter &painter, const Game &game)
{
	this->frameW = painter.frame().width;
	this->frameH = painter.frame().height;
	const int u = painter.unit();
	const Fonts &fonts = game.getFonts();
	const Localization &text = game.getText();
	const int w = std::min(this->frameW - 8 * u, 600 * u), h = std::min(this->frameH - 8 * u, 330 * u);
	this->window = Rect{ (this->frameW - w) / 2, (this->frameH - h) / 2, w, h };
	const int pad = 10 * u;

	painter.fill(Rect{ 0, 0, this->frameW, this->frameH }, hud::rgb(0, 0, 0, 150));
	painter.woodPanel(this->window, 245);
	painter.text(fonts.a, text.get("Travel.Title"), this->window.x + pad, this->window.y + pad, col::GoldBright);
	if (this->current)
		painter.text(fonts.arena, ui::format(text.get("Travel.Here"), { this->current->name }), this->window.x + w - pad, this->window.y + pad + 4 * u, col::TextDim, TextAlign::Right);

	// Mapa à esquerda (proporção 4:3 do VGA) com os locais da província escolhida.
	const int areaH = h - 2 * pad - 26 * u, areaW = std::min(areaH * 4 / 3, w * 58 / 100);
	this->mapRect = Rect{ this->window.x + pad, this->window.y + pad + 24 * u, areaW, areaW * 3 / 4 };
	painter.image(this->map, this->mapW, this->mapH, this->mapRect);
	auto mapPoint = [&](int gx, int gy) { return std::pair<int, int>{ this->mapRect.x + gx * this->mapRect.w / 320, this->mapRect.y + gy * this->mapRect.h / 200 }; };
	for (size_t i = 0; i < this->places.size(); i++)
	{
		const CityLocation *city = this->places[i].first;
		const auto [mx, my] = mapPoint(city->globalX, city->globalY);
		const int r = (city->type == CityLocation::Type::CityState ? 3 : 2) * u;
		const bool chosen = static_cast<int>(i) == this->selected;
		const uint32_t tint = chosen ? col::GoldBright : city->type == CityLocation::Type::CityState ? hud::rgb(230, 60, 40)
			: city->type == CityLocation::Type::Dungeon ? hud::rgb(120, 70, 160) : hud::rgb(220, 200, 150);
		painter.fill(Rect{ mx - r - u, my - r - u, 2 * r + 2 * u, 2 * r + 2 * u }, hud::rgb(0, 0, 0, 200));
		painter.fill(Rect{ mx - r, my - r, 2 * r, 2 * r }, tint);
	}
	if (this->current)
	{
		// Onde o jogador está: um losango dourado.
		const auto [mx, my] = mapPoint(this->current->globalX, this->current->globalY);
		for (int k = 0; k <= 4 * u; k++)
		{
			painter.fill(Rect{ mx - k, my - 4 * u + k, 2 * k + u, u }, col::GoldBright);
			painter.fill(Rect{ mx - k, my + 4 * u - k, 2 * k + u, u }, col::GoldBright);
		}
	}

	// Lista à direita.
	const int lx = this->mapRect.x + this->mapRect.w + pad, lw = this->window.x + w - pad - lx;
	const int shownProvince = this->hoveredProvince >= 0 ? this->hoveredProvince : this->province;
	painter.text(fonts.a, game.provinceName(std::max(0, shownProvince)), lx, this->mapRect.y, this->hoveredProvince >= 0 ? col::TextDim : col::GoldBright);
	const Rect page{ lx, this->mapRect.y + 22 * u, lw, h - (this->mapRect.y + 22 * u - this->window.y) - 34 * u };
	painter.parchmentPanel(page);
	this->rowH = 14 * u;
	this->listRect = Rect{ page.x + 4 * u, page.y + 4 * u, page.w - 8 * u, page.h - 8 * u };
	this->visibleRows = std::max(1, this->listRect.h / this->rowH);
	this->listRect.h = this->visibleRows * this->rowH;
	const int count = static_cast<int>(this->places.size());
	this->scroll = std::clamp(this->scroll, 0, std::max(0, count - this->visibleRows));
	for (int i = 0; i < this->visibleRows && this->scroll + i < count; i++)
	{
		const auto &[city, days] = this->places[static_cast<size_t>(this->scroll + i)];
		const Rect r{ this->listRect.x, this->listRect.y + i * this->rowH, this->listRect.w, this->rowH - u };
		const bool isSelected = this->scroll + i == this->selected;
		painter.rowHighlight(r, isSelected, kit::hit(r, this->mouseX, this->mouseY), true);
		const bool dungeon = city->type == CityLocation::Type::Dungeon;
		painter.text(fonts.arena, dungeon ? ui::format(text.get(city->mainQuest() ? "Travel.MainDungeon" : "Travel.Dungeon"), { city->name }) : city->name,
			r.x + 4 * u, r.y + 3 * u, isSelected ? hud::rgb(120, 60, 0) : dungeon ? hud::rgb(110, 30, 20) : col::Ink, TextAlign::Left, false);
		painter.text(fonts.arena, ui::format(text.get(days == 1 ? "Travel.Day" : "Travel.Days"), { std::to_string(days) }), r.x + r.w - 4 * u, r.y + 3 * u, col::InkSoft, TextAlign::Right, false);
	}

	// Botões e confirmação.
	const int by = this->window.y + h - pad - 18 * u;
	if (this->confirm >= 0 && this->confirm < count)
	{
		const auto &[city, days] = this->places[static_cast<size_t>(this->confirm)];
		painter.text(fonts.arena, ui::format(text.get("Travel.Confirm"), { city->name, std::to_string(days) }), this->window.x + pad, by + 4 * u, col::GoldBright);
	}
	else
	{
		painter.text(fonts.arena, text.get("Travel.Hint"), this->window.x + pad, by + 4 * u, col::TextDim);
	}

	this->goRect = Rect{ this->window.x + w - pad - 190 * u, by, 90 * u, 18 * u };
	painter.button(this->goRect, text.get("Travel.Go"), kit::hit(this->goRect, this->mouseX, this->mouseY), this->confirm >= 0, this->selected >= 0);
	this->closeRect = Rect{ this->window.x + w - pad - 92 * u, by, 92 * u, 18 * u };
	painter.button(this->closeRect, text.get("Menu.Close"), kit::hit(this->closeRect, this->mouseX, this->mouseY), false);
}
