// Conversa com moradores.

#include <algorithm>

#include "../render/Ui.h"
#include "DialogueMenu.h"
#include "GameMenus.h"

namespace
{
	using namespace arena;
	namespace col = kit::color;

	enum Action { Who = 1, WhereIs = 2, Rumor = 3, Bye = 4, Back = 5, PlaceBase = 100 };
}

void arena::DialogueMenu::open(const std::string &npcName, const std::string &greeting, const std::string &who, std::vector<Place> placeList, std::vector<std::string> rumorList)
{
	this->name = npcName;
	this->text = greeting;
	this->whoAnswer = who;
	this->places = std::move(placeList);
	this->rumors = std::move(rumorList);
	this->nextRumor = 0;
	this->firstRumorIsQuest = this->questRumorHeard = false;
	this->page = Page::Main;
	this->frameW = this->frameH = 1;
}

std::vector<arena::DialogueMenu::Option> arena::DialogueMenu::options(const Game &game) const
{
	const Localization &loc = game.getText();
	std::vector<Option> list;
	if (this->page == Page::Main)
	{
		list = { { loc.get("Dialogue.Who"), Who }, { loc.get("Dialogue.WhereIs"), WhereIs }, { loc.get("Dialogue.Rumors"), Rumor }, { loc.get("Dialogue.Bye"), Bye } };
	}
	else
	{
		for (size_t i = 0; i < this->places.size(); i++) list.push_back({ this->places[i].label, PlaceBase + static_cast<int>(i) });
		list.push_back({ loc.get("Dialogue.Back"), Back });
	}

	return list;
}

bool arena::DialogueMenu::choose(int action, Game &game)
{
	const Localization &loc = game.getText();
	switch (action)
	{
	case Who: this->text = this->whoAnswer; break;
	case WhereIs: this->page = Page::WhereIs; this->text = loc.get("Dialogue.WhereIsPrompt"); break;
	case Rumor:
		if (!this->rumors.empty() && this->firstRumorIsQuest && this->nextRumor % this->rumors.size() == 0) this->questRumorHeard = true;
		if (!this->rumors.empty()) this->text = "\"" + this->rumors[this->nextRumor++ % this->rumors.size()] + "\"";
		break;
	case Back: this->page = Page::Main; this->text = loc.get("Dialogue.Anything"); break;
	case Bye: return true;
	default:
		if (action >= PlaceBase && action - PlaceBase < static_cast<int>(this->places.size()))
		{
			this->text = this->places[static_cast<size_t>(action - PlaceBase)].answer;
			this->page = Page::Main;
		}
		break;
	}

	game.playSound("CLICKS.VOC", 0.4f);
	return false;
}

arena::OverlayResult arena::DialogueMenu::handleInput(const InputEvent &event, Game &game)
{
	OverlayResult result;
	eventToPixels(event, this->frameW, this->frameH, this->mouseX, this->mouseY);
	const std::vector<Option> list = this->options(game);
	if (event.isKey(InputEvent::Key::Escape))
	{
		if (this->page == Page::WhereIs) this->choose(Back, game);
		else result.close = true;
		return result;
	}

	// Teclas 1..9 escolhem a opção.
	if (event.type == InputEvent::Type::KeyDown && event.key == InputEvent::Key::Character && event.character >= '1' && event.character <= '9')
	{
		const size_t index = static_cast<size_t>(event.character - '1');
		if (index < list.size()) result.close = this->choose(list[index].action, game);
		return result;
	}

	if (event.isClick() && this->frameW > 1)
	{
		if (!kit::hit(this->window, this->mouseX, this->mouseY)) { result.close = true; return result; }
		for (const auto &[rect, action] : this->buttons)
			if (kit::hit(rect, this->mouseX, this->mouseY)) { result.close = this->choose(action, game); return result; }
	}

	return result;
}

void arena::DialogueMenu::render(kit::Painter &painter, const Game &game)
{
	this->frameW = painter.frame().width;
	this->frameH = painter.frame().height;
	const int u = painter.unit();
	const Fonts &fonts = game.getFonts();
	const std::vector<Option> list = this->options(game);

	// Janela na parte de baixo da tela, para o morador continuar visível.
	const int rows = static_cast<int>(list.size());
	const int w = std::min(this->frameW - 8 * u, 360 * u);
	const int buttonH = 17 * u, gap = 4 * u;
	const int textH = 46 * u;
	const int h = 12 * u + 20 * u + textH + 10 * u + ((rows + 1) / 2) * (buttonH + gap) + 8 * u;
	this->window = Rect{ (this->frameW - w) / 2, std::max(4 * u, this->frameH - h - 72 * u), w, h }; // acima das barras do HUD
	const int pad = 12 * u;

	painter.woodPanel(this->window, 240);
	painter.text(fonts.a, this->name, this->window.x + pad, this->window.y + 8 * u, col::GoldBright);
	const Rect textArea{ this->window.x + pad, this->window.y + 30 * u, w - 2 * pad, textH };
	painter.parchmentPanel(textArea);
	painter.paragraph(fonts.arena, this->text, Rect{ textArea.x + 8 * u, textArea.y + 6 * u, textArea.w - 16 * u, textArea.h - 12 * u }, col::Ink);

	// Opções em duas colunas, numeradas.
	this->buttons.clear();
	const int bw = (w - 2 * pad - gap) / 2;
	int y = textArea.y + textArea.h + 10 * u;
	for (int i = 0; i < rows; i++)
	{
		const Rect r{ this->window.x + pad + (i % 2) * (bw + gap), y + (i / 2) * (buttonH + gap), bw, buttonH };
		painter.button(r, std::to_string(i + 1) + ". " + list[static_cast<size_t>(i)].label, kit::hit(r, this->mouseX, this->mouseY), false);
		this->buttons.push_back({ r, list[static_cast<size_t>(i)].action });
	}
}
