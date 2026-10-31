#include <algorithm>
#include <cstdio>

#include "../render/Ui.h"
#include "GameClock.h"
#include "GameMenus.h"
#include "JournalMenu.h"
#include "MainQuest.h"

namespace
{
	namespace col = arena::kit::color;
}

void arena::JournalMenu::open(std::string currentObjective, std::string currentSideJobs)
{
	this->objective = std::move(currentObjective);
	this->sideJobs = std::move(currentSideJobs);
	this->scroll = 0;
	this->reading = -1;
}

arena::OverlayResult arena::JournalMenu::handleInput(const InputEvent &event, Game &game)
{
	OverlayResult result;
	eventToPixels(event, this->frameW, this->frameH, this->mouseX, this->mouseY);
	const int count = static_cast<int>(game.getCharacter().journal.size());

	if (event.isKey(InputEvent::Key::Escape) || event.isHotkey('J'))
	{
		if (this->reading >= 0 && !event.isHotkey('J')) this->reading = -1;
		else result.close = true;
		return result;
	}

	if (this->reading < 0)
	{
		if (event.type == InputEvent::Type::MouseWheel) this->scroll = std::clamp(this->scroll - (event.dy > 0 ? 1 : -1), 0, std::max(0, count - 1));
		else if (event.isKey(InputEvent::Key::Down) && this->moreBelow) this->scroll++;
		else if (event.isKey(InputEvent::Key::Up)) this->scroll = std::max(0, this->scroll - 1);
	}

	if (event.isClick())
	{
		if (kit::hit(this->closeButton, this->mouseX, this->mouseY)) { result.close = true; return result; }
		if (this->reading >= 0)
		{
			if (kit::hit(this->backButton, this->mouseX, this->mouseY)) this->reading = -1;
			return result;
		}
		for (const auto &[rect, index] : this->rows)
		{
			if (!kit::hit(rect, this->mouseX, this->mouseY)) continue;
			if (!game.getCharacter().journal[static_cast<size_t>(index)].detail.empty())
			{
				this->reading = index;
				game.playSound("CLICKS.VOC", 0.4f);
			}
			break;
		}
	}

	return result;
}

void arena::JournalMenu::render(kit::Painter &painter, const Game &game)
{
	this->frameW = painter.frame().width;
	this->frameH = painter.frame().height;
	const int u = painter.unit();
	const Fonts &fonts = game.getFonts();
	const Localization &text = game.getText();
	const CharacterDraft &character = game.getCharacter();
	const auto &journal = character.journal;

	const int w = std::min(this->frameW - 16 * u, 440 * u), h = std::min(this->frameH - 16 * u, 300 * u);
	this->window = Rect{ (this->frameW - w) / 2, (this->frameH - h) / 2, w, h };
	const int pad = 14 * u;
	painter.fill(Rect{ 0, 0, this->frameW, this->frameH }, hud::rgb(0, 0, 0, 120));
	painter.woodPanel(this->window, 245);
	painter.text(fonts.a, text.get("Journal.Title"), this->window.x + w / 2, this->window.y + 10 * u, col::GoldBright, TextAlign::Center);
	painter.divider(this->window.x + pad, this->window.y + 32 * u, w - 2 * pad, col::GoldDim);

	const Rect page{ this->window.x + pad, this->window.y + 40 * u, w - 2 * pad, h - 40 * u - 34 * u };
	painter.parchmentPanel(page);
	const int left = page.x + 10 * u, width = page.w - 20 * u, bottom = page.y + page.h - 8 * u;
	auto dateOf = [&](double minutes)
	{
		char time[16];
		std::snprintf(time, sizeof(time), "%02d:%02d", clock::hour(minutes), clock::minute(minutes));
		return ui::format(text.get("Calendar.Date"), { std::to_string(clock::dayOfMonth(minutes)), text.get("Calendar.Month" + std::to_string(clock::month(minutes))) }) + ", " + time;
	};

	this->rows.clear();
	this->moreBelow = false;
	const int buttonW = 110 * u, buttonH = 18 * u, buttonY = this->window.y + h - 26 * u;
	if (this->reading >= 0 && this->reading < static_cast<int>(journal.size()))
	{
		// Releitura: o texto completo (sonho, achado) com a data.
		const JournalEntry &entry = journal[static_cast<size_t>(this->reading)];
		int y = page.y + 8 * u;
		painter.text(fonts.arena, dateOf(entry.minutes), left, y, col::InkSoft, TextAlign::Left, false);
		y += 14 * u;
		painter.paragraph(fonts.arena, ui::format(text.get(entry.detail), { character.name, character.name, character.name }), Rect{ left, y, width, bottom - y }, col::Ink);
		this->backButton = Rect{ this->window.x + w / 2 - buttonW - 4 * u, buttonY, buttonW, buttonH };
		painter.button(this->backButton, text.get("Journal.Back"), kit::hit(this->backButton, this->mouseX, this->mouseY), false);
		this->closeButton = Rect{ this->window.x + w / 2 + 4 * u, buttonY, buttonW, buttonH };
	}
	else
	{
		// Missão atual e progresso do Cajado.
		int y = page.y + 8 * u;
		painter.text(fonts.a, text.get("Journal.Current"), left, y, col::Ink, TextAlign::Left, false);
		y += 16 * u;
		y += painter.paragraph(fonts.arena, this->objective, Rect{ left, y, width, 40 * u }, col::Ink) + 4 * u;
		painter.text(fonts.arena, ui::format(text.get("Journal.Staff"), { std::to_string(character.staffPieces) }), left, y, col::InkSoft, TextAlign::Left, false);
		y += 14 * u;
		if (!this->sideJobs.empty())
		{
			painter.text(fonts.a, text.get("Journal.SideJobs"), left, y, col::Ink, TextAlign::Left, false);
			y += 16 * u;
			y += painter.paragraph(fonts.arena, this->sideJobs, Rect{ left, y, width, 52 * u }, col::Ink) + 4 * u;
		}
		painter.divider(left, y, width, col::InkSoft);
		y += 8 * u;
		painter.text(fonts.a, text.get("Journal.Log"), left, y, col::Ink, TextAlign::Left, false);
		y += 16 * u;

		// Registro, do mais recente ao mais antigo; a roda do mouse rola.
		if (journal.empty()) painter.text(fonts.arena, text.get("Journal.Empty"), left, y, col::InkSoft, TextAlign::Left, false);
		const int count = static_cast<int>(journal.size());
		this->scroll = std::clamp(this->scroll, 0, std::max(0, count - 1));
		for (int i = count - 1 - this->scroll; i >= 0; i--)
		{
			if (y + 24 * u > bottom) { this->moreBelow = true; break; }
			const JournalEntry &entry = journal[static_cast<size_t>(i)];
			const int top = y;
			const bool readable = !entry.detail.empty();
			painter.text(fonts.arena, dateOf(entry.minutes) + (readable ? "   " + text.get("Journal.ReadHint") : std::string()), left, y, col::InkSoft, TextAlign::Left, false);
			y += 12 * u;
			y += painter.paragraph(fonts.arena, entry.text, Rect{ left, y, width, bottom - y }, col::Ink) + 6 * u;
			const Rect row{ left - 4 * u, top - 2 * u, width + 8 * u, y - top };
			if (readable && kit::hit(row, this->mouseX, this->mouseY)) painter.rowHighlight(row, false, true, true);
			this->rows.push_back({ row, i });
		}
		if (this->moreBelow || this->scroll > 0)
			painter.text(fonts.arena, text.get("Journal.ScrollHint"), page.x + page.w - 10 * u, page.y + 8 * u, col::InkSoft, TextAlign::Right, false);
		this->closeButton = Rect{ this->window.x + (w - buttonW) / 2, buttonY, buttonW, buttonH };
	}

	painter.button(this->closeButton, text.get("Journal.Close"), kit::hit(this->closeButton, this->mouseX, this->mouseY), false);
}
