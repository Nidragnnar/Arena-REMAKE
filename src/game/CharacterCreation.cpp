// Criação de personagem completa, seguindo o fluxo do Arena original:
// método de classe -> (perguntas | lista) -> nome -> gênero -> raça no mapa ->
// atributos/bônus -> salvar ou rolar de novo -> aparência (rosto).

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numeric>

#include "../core/KeyValueFile.h"
#include "../core/Utf8.h"
#include "../render/Ui.h"
#include "CharacterCreation.h"
#include "GameWorldScreen.h"
#include "Screens.h"

namespace
{
	using namespace arena;

	// Cores do original (convertidas para o índice mais próximo de cada paleta).
	constexpr Color InkColor{ 48, 12, 12 };
	constexpr Color HoverColor{ 185, 30, 20 };

	uint8_t pick(const Palette &palette, const Color &c) { return nearestColor(palette, c.r, c.g, c.b); }

	std::string signedValue(int value)
	{
		return (value > 0 ? "+" : "") + std::to_string(value);
	}

	// Bônus derivados dos atributos (fórmulas do executável, via OpenTESArena).
	int damageBonus(int strength) { return strength <= 43 ? 0 : (strength - 48) / 5; }
	int magicDefense(int willpower)
	{
		if (willpower <= 38) return -2;
		if (willpower <= 41) return -1;
		if (willpower <= 46) return 0;
		return (willpower - 46) / 9;
	}
	int hitBonus(int agility)
	{
		if (agility <= 45) return -1;
		if (agility <= 46) return 0;
		return (agility - 50) / 5;
	}
	int healthBonus(int endurance)
	{
		const int base256 = (endurance * 256) / 100;
		const int result256 = (base256 - 128 + 12) / 25;
		return static_cast<int>(std::lround(result256 * 100.0 / 256.0));
	}

	void changeTo(Game &game, std::unique_ptr<Screen> screen) { game.changeScreen(std::move(screen)); }
}

// ---------------------------------------------------------------------------

arena::CreationRoomScreen::CreationRoomScreen(Game &game) : Screen(game), background(game.loadImage("STARTGAM.MNU"))
{
	game.playMusic("SHEET.XMI");
	this->palette = this->background.palette ? *this->background.palette : game.getDefaultPalette();
}

void arena::CreationRoomScreen::drawParchmentButton(Canvas &canvas, const std::string &label, int cx, int cy, bool hovered, int w, int h) const
{
	const Rect r = buttonRect(cx, cy, w, h);
	canvas.blit(this->game.getParchment(r.w, r.h), r.x, r.y);
	const BitmapFont &font = this->game.getFonts().a;
	int textW = 0, textH = 0;
	ui::measureBlock(font, label, 1, textW, textH);
	canvas.drawTextBlock(font, label, cx, cy - textH / 2, pick(this->palette, hovered ? HoverColor : InkColor), -1, TextAlign::Center, 1);
}

// ---------------------------------------------------------------------------
// Gerar ou escolher a classe

void arena::ChooseClassCreationScreen::handleInput(const InputEvent &event)
{
	const Localization &text = this->game.getText();
	const std::string generate = text.get("CharacterCreation.ChooseClassCreationGenerate");
	const std::string select = text.get("CharacterCreation.ChooseClassCreationSelect");
	const Rect rects[2] = { buttonRect(159, 120), buttonRect(159, 160) };

	if (event.isKey(InputEvent::Key::Escape))
	{
		changeTo(this->game, std::make_unique<MainMenuScreen>(this->game));
		return;
	}

	int activated = -1;
	if (event.type == InputEvent::Type::MouseMove)
	{
		this->hovered = rects[0].contains(event.x, event.y) ? 0 : rects[1].contains(event.x, event.y) ? 1 : -1;
	}
	else if (event.isClick())
	{
		activated = rects[0].contains(event.x, event.y) ? 0 : rects[1].contains(event.x, event.y) ? 1 : -1;
	}
	else if (event.isKey(InputEvent::Key::Up) || event.isKey(InputEvent::Key::Down))
	{
		this->hovered = (this->hovered == 0) ? 1 : 0;
	}
	else if (event.isKey(InputEvent::Key::Enter))
	{
		activated = this->hovered;
	}
	else if (event.isHotkey(static_cast<char>(std::toupper(static_cast<unsigned char>(generate[0])))))
	{
		activated = 0;
	}
	else if (event.isHotkey(static_cast<char>(std::toupper(static_cast<unsigned char>(select[0])))))
	{
		activated = 1;
	}

	if (activated == 0)
	{
		changeTo(this->game, std::make_unique<ClassQuestionsScreen>(this->game));
	}
	else if (activated == 1)
	{
		changeTo(this->game, std::make_unique<ChooseClassScreen>(this->game));
	}
}

void arena::ChooseClassCreationScreen::render(Canvas &canvas)
{
	const Localization &text = this->game.getText();
	this->drawRoom(canvas);
	this->drawParchmentButton(canvas, text.get("CharacterCreation.ChooseClassCreation"), 159, 80, false);
	this->drawParchmentButton(canvas, text.get("CharacterCreation.ChooseClassCreationGenerate"), 159, 120, this->hovered == 0);
	this->drawParchmentButton(canvas, text.get("CharacterCreation.ChooseClassCreationSelect"), 159, 160, this->hovered == 1);
}

// ---------------------------------------------------------------------------
// Perguntas de classe

namespace
{
	constexpr Rect QuestionBox{ 6, 4, 308, 192 };
	constexpr Rect QuestionContent{ 24, 32, 272, 152 };
}

arena::ClassQuestionsScreen::ClassQuestionsScreen(Game &game) : CreationRoomScreen(game)
{
	// 10 perguntas sorteadas entre as 40 do QUESTION.TXT.
	const int total = static_cast<int>(game.getData().getQuestions().size());
	this->questionOrder.resize(static_cast<size_t>(total));
	std::iota(this->questionOrder.begin(), this->questionOrder.end(), 0);
	std::shuffle(this->questionOrder.begin(), this->questionOrder.end(), game.getRandom());
	this->questionOrder.resize(static_cast<size_t>(std::min(total, QuestionCount)));

	this->layoutQuestion();
	this->dialog.open(game.getText().get("CharacterCreation.ClassQuestionsIntro"), game.getFonts().a, Dialog::Style::Parchment);
}

void arena::ClassQuestionsScreen::layoutQuestion()
{
	const ClassQuestion &question = this->game.getData().getQuestions()[static_cast<size_t>(this->questionOrder[static_cast<size_t>(this->current)])];
	const BitmapFont &font = this->game.getFonts().arena;
	const int lineHeight = font.getHeight() + 1;
	auto heightOf = [lineHeight](const std::string &wrapped)
	{
		return static_cast<int>(std::count(wrapped.begin(), wrapped.end(), '\n') + 1) * lineHeight;
	};

	this->questionText = ui::wrapText(font, question.text, QuestionContent.w);
	int y = heightOf(this->questionText) + 6;
	for (size_t i = 0; i < 3; i++)
	{
		const std::string label = std::string(1, static_cast<char>('A' + i)) + ") " + question.choices[i];
		this->choiceTexts[i] = ui::wrapText(font, label, QuestionContent.w);
		this->choiceTop[i] = y;
		y += heightOf(this->choiceTexts[i]);
		this->choiceBottom[i] = y;
		y += 5;
	}

	this->contentHeight = y;
	this->scroll = 0;
	this->hovered = -1;
}

void arena::ClassQuestionsScreen::answer(int choice)
{
	const ClassQuestion &question = this->game.getData().getQuestions()[static_cast<size_t>(this->questionOrder[static_cast<size_t>(this->current)])];
	this->categoryCounts[static_cast<size_t>(question.categories[static_cast<size_t>(choice)])]++;
	this->current++;

	if (this->current < static_cast<int>(this->questionOrder.size()))
	{
		this->layoutQuestion();
		return;
	}

	const int mage = this->categoryCounts[static_cast<size_t>(ClassCategory::Mage)];
	const int thief = this->categoryCounts[static_cast<size_t>(ClassCategory::Thief)];
	const int warrior = this->categoryCounts[static_cast<size_t>(ClassCategory::Warrior)];
	this->suggestedClass = this->game.getData().classFromAnswers(warrior, thief, mage);
	if (this->suggestedClass < 0)
	{
		// Combinação inesperada (ex.: tradução com menos de 10 perguntas): usa a categoria dominante.
		this->suggestedClass = (warrior >= thief && warrior >= mage) ? 16 : (thief >= mage) ? 10 : 0;
	}

	const Localization &text = this->game.getText();
	this->dialog.open(ui::format(text.get("CharacterCreation.SuggestedClass"), { this->game.className(this->suggestedClass) }),
		this->game.getFonts().a, Dialog::Style::Parchment, { text.get("Common.Yes"), text.get("Common.No") });
}

void arena::ClassQuestionsScreen::handleInput(const InputEvent &event)
{
	if (this->dialog.isOpen())
	{
		const int result = this->dialog.handleInput(event);
		if (this->suggestedClass >= 0 && result == 0)
		{
			this->game.getCharacter().classIndex = this->suggestedClass;
			changeTo(this->game, std::make_unique<ChooseNameScreen>(this->game));
		}
		else if (this->suggestedClass >= 0 && result == 1)
		{
			changeTo(this->game, std::make_unique<ChooseClassScreen>(this->game));
		}

		return;
	}

	const int maxScroll = std::max(0, this->contentHeight - QuestionContent.h);
	auto choiceAt = [this](int x, int y)
	{
		if (!QuestionContent.contains(x, y)) return -1;
		const int contentY = y - QuestionContent.y + this->scroll;
		for (int i = 0; i < 3; i++)
		{
			if (contentY >= this->choiceTop[static_cast<size_t>(i)] && contentY < this->choiceBottom[static_cast<size_t>(i)]) return i;
		}

		return -1;
	};

	switch (event.type)
	{
	case InputEvent::Type::MouseMove:
		this->hovered = choiceAt(event.x, event.y);
		break;
	case InputEvent::Type::MouseDown:
		if (const int choice = choiceAt(event.x, event.y); choice >= 0) this->answer(choice);
		break;
	case InputEvent::Type::MouseWheel:
		this->scroll = std::clamp(this->scroll - event.wheel * 10, 0, maxScroll);
		break;
	case InputEvent::Type::KeyDown:
		if (event.key == InputEvent::Key::Escape)
		{
			changeTo(this->game, std::make_unique<ChooseClassCreationScreen>(this->game));
		}
		else if (event.key == InputEvent::Key::Up)
		{
			this->scroll = std::max(0, this->scroll - 10);
		}
		else if (event.key == InputEvent::Key::Down)
		{
			this->scroll = std::min(maxScroll, this->scroll + 10);
		}
		else if (event.key == InputEvent::Key::Character && event.character >= 'A' && event.character <= 'C')
		{
			this->answer(event.character - 'A');
		}
		break;
	default:
		break;
	}
}

void arena::ClassQuestionsScreen::render(Canvas &canvas)
{
	this->drawRoom(canvas);
	if (this->current < static_cast<int>(this->questionOrder.size()))
	{
		const BitmapFont &font = this->game.getFonts().arena;
		const uint8_t ink = pick(this->palette, InkColor);
		const uint8_t choiceInk = pick(this->palette, Color{ 85, 44, 20 });
		const uint8_t hover = pick(this->palette, HoverColor);

		canvas.blit(this->game.getParchment(QuestionBox.w, QuestionBox.h), QuestionBox.x, QuestionBox.y);
		const std::string header = ui::format(this->game.getText().get("CharacterCreation.QuestionHeader"),
			{ std::to_string(this->current + 1), std::to_string(this->questionOrder.size()) });
		canvas.drawText(this->game.getFonts().a, header, Canvas::Width / 2, QuestionBox.y + 12, ink, -1, TextAlign::Center);

		canvas.setClip(QuestionContent.x, QuestionContent.y, QuestionContent.w, QuestionContent.h);
		const int baseY = QuestionContent.y - this->scroll;
		canvas.drawTextBlock(font, this->questionText, QuestionContent.x, baseY, ink, -1, TextAlign::Left, 1);
		for (size_t i = 0; i < 3; i++)
		{
			canvas.drawTextBlock(font, this->choiceTexts[i], QuestionContent.x, baseY + this->choiceTop[i],
				static_cast<int>(i) == this->hovered ? hover : choiceInk, -1, TextAlign::Left, 1);
		}
		canvas.resetClip();

		// Indicadores de rolagem quando o texto não cabe.
		const int maxScroll = std::max(0, this->contentHeight - QuestionContent.h);
		if (this->scroll > 0) canvas.drawText(font, "^", QuestionContent.x + QuestionContent.w, QuestionContent.y - 2, ink);
		if (this->scroll < maxScroll) canvas.drawText(font, "v", QuestionContent.x + QuestionContent.w, QuestionContent.y + QuestionContent.h - 8, ink);
	}

	this->dialog.render(canvas, this->game, this->palette);
}

// ---------------------------------------------------------------------------
// Lista de classes

namespace
{
	constexpr int ListX = 84;
	constexpr int ListY = 45;
	constexpr int ListW = 150;
	constexpr int RowHeight = 11;
	constexpr Rect ScrollUpRect{ 60, 15, 17, 17 };
	constexpr Rect ScrollDownRect{ 60, 110, 17, 17 };
}

arena::ChooseClassScreen::ChooseClassScreen(Game &game) : CreationRoomScreen(game), popup(game.loadImage("POPUP2.IMG"))
{
	this->order.resize(ClassCount);
	std::iota(this->order.begin(), this->order.end(), 0);
	std::sort(this->order.begin(), this->order.end(), [&game](int a, int b) { return ui::sortKey(game.className(a)) < ui::sortKey(game.className(b)); });
}

int arena::ChooseClassScreen::rowAt(int x, int y) const
{
	if (x < ListX || x >= ListX + ListW || y < ListY || y >= ListY + VisibleRows * RowHeight)
	{
		return -1;
	}

	const int row = this->scroll + (y - ListY) / RowHeight;
	return row < ClassCount ? row : -1;
}

std::string arena::ChooseClassScreen::describe(int classIndex) const
{
	const Localization &text = this->game.getText();
	const ClassDefinition &def = this->game.getData().getClass(classIndex);
	const std::string category = text.get("ClassInfo.Category" + std::to_string(static_cast<int>(def.category)));
	const std::string armor = text.get("ClassInfo.Armor" + std::to_string(static_cast<int>(def.armor)));
	const std::string preferred = this->game.localized("Classes.Preferred" + std::to_string(classIndex), def.originalPreferred);

	std::string magic = text.get("ClassInfo.NoMagic");
	if (def.castsMagic)
	{
		char multiplier[16];
		std::snprintf(multiplier, sizeof(multiplier), "%.2g", def.spellPointsMultiplier);
		magic = ui::format(text.get("ClassInfo.Magic"), { multiplier });
	}

	return ui::format(text.get("ClassInfo.Description"),
		{ this->game.className(classIndex), category, armor, preferred, std::to_string(def.healthDie), magic });
}

void arena::ChooseClassScreen::handleInput(const InputEvent &event)
{
	const int maxScroll = ClassCount - VisibleRows;
	switch (event.type)
	{
	case InputEvent::Type::MouseMove:
		this->hovered = this->rowAt(event.x, event.y);
		break;
	case InputEvent::Type::MouseWheel:
		this->scroll = std::clamp(this->scroll - event.wheel, 0, maxScroll);
		break;
	case InputEvent::Type::MouseDown:
		if (ScrollUpRect.contains(event.x, event.y)) this->scroll = std::max(0, this->scroll - 1);
		else if (ScrollDownRect.contains(event.x, event.y)) this->scroll = std::min(maxScroll, this->scroll + 1);
		else if (const int row = this->rowAt(event.x, event.y); row >= 0)
		{
			this->game.getCharacter().classIndex = this->order[static_cast<size_t>(row)];
			changeTo(this->game, std::make_unique<ChooseNameScreen>(this->game));
		}
		break;
	case InputEvent::Type::KeyDown:
		if (event.key == InputEvent::Key::Escape)
		{
			changeTo(this->game, std::make_unique<ChooseClassCreationScreen>(this->game));
		}
		else if (event.key == InputEvent::Key::Up || event.key == InputEvent::Key::Down)
		{
			const int delta = event.key == InputEvent::Key::Up ? -1 : 1;
			this->hovered = std::clamp(this->hovered < 0 ? this->scroll : this->hovered + delta, 0, ClassCount - 1);
			if (this->hovered < this->scroll) this->scroll = this->hovered;
			if (this->hovered >= this->scroll + VisibleRows) this->scroll = this->hovered - VisibleRows + 1;
		}
		else if (event.key == InputEvent::Key::Enter && this->hovered >= 0)
		{
			this->game.getCharacter().classIndex = this->order[static_cast<size_t>(this->hovered)];
			changeTo(this->game, std::make_unique<ChooseNameScreen>(this->game));
		}
		break;
	default:
		break;
	}
}

void arena::ChooseClassScreen::render(Canvas &canvas)
{
	this->drawRoom(canvas);
	canvas.blit(this->popup, 55, 9);

	const BitmapFont &font = this->game.getFonts().a;
	canvas.drawText(font, this->game.getText().get("CharacterCreation.ChooseClassList"), ListX + ListW / 2, 28,
		pick(this->palette, InkColor), -1, TextAlign::Center);

	const uint8_t normal = pick(this->palette, Color{ 85, 44, 20 });
	const uint8_t highlight = pick(this->palette, Color{ 231, 162, 0 });
	for (int row = 0; row < VisibleRows && this->scroll + row < ClassCount; row++)
	{
		const int index = this->scroll + row;
		canvas.drawText(font, this->game.className(this->order[static_cast<size_t>(index)]), ListX, ListY + row * RowHeight,
			index == this->hovered ? highlight : normal);
	}

	if (this->hovered >= 0)
	{
		const BitmapFont &descFont = this->game.getFonts().d;
		canvas.drawTextBlock(descFont, this->describe(this->order[static_cast<size_t>(this->hovered)]), Canvas::Width / 2, 140,
			pick(this->palette, Color{ 255, 255, 255 }), pick(this->palette, Color{ 0, 0, 0 }), TextAlign::Center, 2);
	}
}

// ---------------------------------------------------------------------------
// Nome

arena::ChooseNameScreen::ChooseNameScreen(Game &game) : CreationRoomScreen(game), name(game.getCharacter().name) { }

void arena::ChooseNameScreen::handleInput(const InputEvent &event)
{
	constexpr int MaxNameWidth = 220;
	const BitmapFont &font = this->game.getFonts().a;

	if (event.type == InputEvent::Type::TextInput)
	{
		for (char32_t c : decodeUtf8(event.text))
		{
			if (!font.hasGlyph(c) || (c == U' ' && this->name.empty())) continue;

			std::string candidate = this->name;
			// Reconverte o codepoint aceito para UTF-8.
			if (c < 0x80) candidate.push_back(static_cast<char>(c));
			else if (c < 0x800) { candidate.push_back(static_cast<char>(0xC0 | (c >> 6))); candidate.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
			else { candidate.push_back(static_cast<char>(0xE0 | (c >> 12))); candidate.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F))); candidate.push_back(static_cast<char>(0x80 | (c & 0x3F))); }

			if (ui::textWidth(font, candidate) <= MaxNameWidth) this->name = candidate;
		}
	}
	else if (event.isKey(InputEvent::Key::Backspace) && !this->name.empty())
	{
		// Remove o último codepoint UTF-8 inteiro.
		size_t cut = this->name.size() - 1;
		while (cut > 0 && (static_cast<unsigned char>(this->name[cut]) & 0xC0) == 0x80) cut--;
		this->name.erase(cut);
	}
	else if (event.isKey(InputEvent::Key::Enter))
	{
		const std::string trimmed = trimWhitespace(this->name);
		if (!trimmed.empty())
		{
			this->game.getCharacter().name = trimmed;
			changeTo(this->game, std::make_unique<ChooseGenderScreen>(this->game));
		}
	}
	else if (event.isKey(InputEvent::Key::Escape))
	{
		changeTo(this->game, std::make_unique<ChooseClassCreationScreen>(this->game));
	}
}

void arena::ChooseNameScreen::render(Canvas &canvas)
{
	this->drawRoom(canvas);
	canvas.blit(this->game.getParchment(300, 60), 10, 70);

	const BitmapFont &font = this->game.getFonts().a;
	const uint8_t ink = pick(this->palette, InkColor);
	const std::string title = ui::format(this->game.getText().get("CharacterCreation.ChooseName"),
		{ this->game.className(this->game.getCharacter().classIndex) });
	canvas.drawText(font, title, Canvas::Width / 2, 82, ink, -1, TextAlign::Center);

	const bool cursorOn = static_cast<int>(this->blink * 2.0) % 2 == 0;
	canvas.drawText(font, this->name + (cursorOn ? "_" : ""), 50, 101, ink);
}

// ---------------------------------------------------------------------------
// Gênero

void arena::ChooseGenderScreen::choose(bool male)
{
	this->game.getCharacter().male = male;
	changeTo(this->game, std::make_unique<ChooseRaceScreen>(this->game));
}

void arena::ChooseGenderScreen::handleInput(const InputEvent &event)
{
	const Localization &text = this->game.getText();
	const Rect rects[2] = { buttonRect(160, 120), buttonRect(160, 160) };
	const std::string male = text.get("CharacterCreation.ChooseGenderMale");
	const std::string female = text.get("CharacterCreation.ChooseGenderFemale");

	if (event.type == InputEvent::Type::MouseMove)
	{
		this->hovered = rects[0].contains(event.x, event.y) ? 0 : rects[1].contains(event.x, event.y) ? 1 : -1;
	}
	else if (event.isClick())
	{
		if (rects[0].contains(event.x, event.y)) this->choose(true);
		else if (rects[1].contains(event.x, event.y)) this->choose(false);
	}
	else if (event.isKey(InputEvent::Key::Escape))
	{
		changeTo(this->game, std::make_unique<ChooseNameScreen>(this->game));
	}
	else if (event.isKey(InputEvent::Key::Up) || event.isKey(InputEvent::Key::Down))
	{
		this->hovered = (this->hovered == 0) ? 1 : 0;
	}
	else if (event.isKey(InputEvent::Key::Enter) && this->hovered >= 0)
	{
		this->choose(this->hovered == 0);
	}
	else if (event.isHotkey(static_cast<char>(std::toupper(static_cast<unsigned char>(male[0])))))
	{
		this->choose(true);
	}
	else if (event.isHotkey(static_cast<char>(std::toupper(static_cast<unsigned char>(female[0])))))
	{
		this->choose(false);
	}
}

void arena::ChooseGenderScreen::render(Canvas &canvas)
{
	const Localization &text = this->game.getText();
	this->drawRoom(canvas);
	this->drawParchmentButton(canvas, text.get("CharacterCreation.ChooseGender"), 160, 80, false);
	this->drawParchmentButton(canvas, text.get("CharacterCreation.ChooseGenderMale"), 160, 120, this->hovered == 0);
	this->drawParchmentButton(canvas, text.get("CharacterCreation.ChooseGenderFemale"), 160, 160, this->hovered == 1);
}

// ---------------------------------------------------------------------------
// Raça (mapa de Tamriel)

namespace
{
	struct MapLabel
	{
		Rect erase;   // onde está o nome em inglês no TAMRIEL.MNU
		int centerX;  // onde desenhar o nome traduzido
		int topY;
	};

	// Índices 0..7 = províncias jogáveis, 8 = Província Imperial (mesma ordem das raças).
	constexpr MapLabel MapLabels[9] =
	{
		{ { 52, 50, 45, 11 }, 74, 52 },    // High Rock
		{ { 72, 75, 51, 11 }, 97, 77 },    // Hammerfell
		{ { 142, 43, 34, 11 }, 158, 45 },  // Skyrim
		{ { 222, 84, 52, 11 }, 247, 86 },  // Morrowind
		{ { 38, 148, 46, 18 }, 60, 150 },  // Summerset Isle (duas linhas)
		{ { 105, 146, 51, 11 }, 130, 148 },// Valenwood
		{ { 148, 126, 38, 11 }, 166, 128 },// Elsweyr
		{ { 216, 143, 55, 11 }, 243, 145 },// Black Marsh
		{ { 133, 104, 83, 11 }, 174, 106 } // Imperial Province
	};

	// Faixa esquerda do título "Empire of" (a direita, "Tamriel", é igual em PT-BR e fica a original).
	constexpr Rect TitleLeft{ 50, 2, 88, 15 };

	bool isTitleInk(const Color &c) { return (c.r * 3 + c.g * 6 + c.b) / 10 < 100; }

	// Descrição da raça (TEMPLATE.DAT #1409..#1416, traduzida).
	constexpr int RaceDescriptionWidth = 260;
}

arena::ChooseRaceScreen::ChooseRaceScreen(Game &game) : Screen(game), map(game.loadImage("TAMRIEL.MNU")), noExit(game.loadImage("NOEXIT.IMG"))
{
	game.playMusic("SHEET.XMI");
	this->palette = this->map.palette ? *this->map.palette : game.getDefaultPalette();
	for (const MapLabel &label : MapLabels)
	{
		eraseInkInRegion(this->map, this->palette, label.erase, isRedInk);
	}

	eraseInkInRegion(this->map, this->palette, TitleLeft, isTitleInk);
	this->openStage(Stage::Intro);
}

void arena::ChooseRaceScreen::openStage(Stage next)
{
	this->stage = next;
	Game &g = this->game;
	const Localization &text = g.getText();
	const CharacterDraft &character = g.getCharacter();
	const int race = this->selectedProvince;

	switch (next)
	{
	case Stage::Intro:
		this->dialog.open(ui::format(g.gendered("CharacterCreation.ChooseRace"), { character.name, g.className(character.classIndex) }),
			g.getFonts().a, Dialog::Style::Parchment);
		break;
	case Stage::Map:
		break;
	case Stage::Confirm:
		this->dialog.open(ui::format(text.get("CharacterCreation.ConfirmRace"), { g.provinceName(race), g.raceName(race, true) }),
			g.getFonts().a, Dialog::Style::Parchment, { text.get("Common.Yes"), text.get("Common.No") }, 90);
		break;
	case Stage::Confirmed1:
		this->dialog.open(ui::format(g.gendered("CharacterCreation.ConfirmedRace1"),
			{ g.className(character.classIndex), character.name, g.provinceName(race), g.raceName(race, true) }),
			g.getFonts().arena, Dialog::Style::Parchment);
		break;
	case Stage::Confirmed2:
		this->dialog.open(text.get("CharacterCreation.ConfirmedRace2") + "\n" +
			ui::wrapText(g.getFonts().arena, text.get("Races.Description" + std::to_string(race)), RaceDescriptionWidth),
			g.getFonts().arena, Dialog::Style::Parchment);
		break;
	case Stage::Confirmed3:
		this->dialog.open(ui::format(g.gendered("CharacterCreation.ConfirmedRace3"),
			{ g.localized("Classes.Preferred" + std::to_string(character.classIndex), g.getData().getClass(character.classIndex).originalPreferred),
			  g.className(character.classIndex) }),
			g.getFonts().arena, Dialog::Style::Parchment);
		break;
	case Stage::Confirmed4:
		this->dialog.open(text.get("CharacterCreation.ConfirmedRace4"), g.getFonts().arena, Dialog::Style::Parchment);
		break;
	}
}

void arena::ChooseRaceScreen::handleInput(const InputEvent &event)
{
	if (this->dialog.isOpen())
	{
		const int result = this->dialog.handleInput(event);
		if (result == Dialog::None)
		{
			return;
		}

		switch (this->stage)
		{
		case Stage::Intro: this->stage = Stage::Map; break;
		case Stage::Confirm:
			if (result == 0)
			{
				this->game.getCharacter().raceIndex = this->selectedProvince;
				this->openStage(Stage::Confirmed1);
			}
			else
			{
				this->stage = Stage::Map;
			}
			break;
		case Stage::Confirmed1: this->openStage(Stage::Confirmed2); break;
		case Stage::Confirmed2: this->openStage(Stage::Confirmed3); break;
		case Stage::Confirmed3: this->openStage(Stage::Confirmed4); break;
		case Stage::Confirmed4: changeTo(this->game, std::make_unique<ChooseAttributesScreen>(this->game)); break;
		default: break;
		}

		return;
	}

	if (event.type == InputEvent::Type::MouseMove)
	{
		this->hoveredProvince = this->game.getData().provinceAt(event.x, event.y);
	}
	else if (event.isClick())
	{
		const int province = this->game.getData().provinceAt(event.x, event.y);
		if (province >= 0)
		{
			this->selectedProvince = province;
			this->openStage(Stage::Confirm);
		}
	}
	else if (event.isKey(InputEvent::Key::Escape))
	{
		changeTo(this->game, std::make_unique<ChooseGenderScreen>(this->game));
	}
}

void arena::ChooseRaceScreen::render(Canvas &canvas)
{
	canvas.blit(this->map, 0, 0, false);
	canvas.blit(this->noExit, Canvas::Width - this->noExit.width, Canvas::Height - this->noExit.height, false);

	const Localization &text = this->game.getText();
	const BitmapFont &titleFont = this->game.getFonts().c;
	const uint8_t titleInk = nearestColor(this->palette, 60, 28, 12);
	canvas.drawText(titleFont, text.get("Map.TitleLeft"), TitleLeft.centerX(), TitleLeft.y + 2, titleInk, -1, TextAlign::Center);

	const BitmapFont &labelFont = this->game.getFonts().four;
	const uint8_t red = nearestColor(this->palette, 210, 20, 20);
	const uint8_t highlight = nearestColor(this->palette, 255, 230, 80);
	const uint8_t shadow = nearestColor(this->palette, 20, 0, 0);
	for (int i = 0; i < 9; i++)
	{
		const MapLabel &label = MapLabels[i];
		const bool hovered = (i == this->hoveredProvince) && !this->dialog.isOpen();
		canvas.drawTextBlock(labelFont, text.getOr("Map.Label" + std::to_string(i), this->game.provinceName(i)), label.centerX, label.topY, hovered ? highlight : red, shadow, TextAlign::Center, 0);
	}

	this->dialog.render(canvas, this->game, this->palette);
}

// ---------------------------------------------------------------------------
// Atributos e aparência

namespace
{
	constexpr int AttributeValueX = 26;
	constexpr int AttributeFirstY = 52;
	constexpr int AttributeRowH = 8;
	constexpr Rect DoneRect{ 12, 180, 34, 14 };
	constexpr Rect PortraitRect{ 218, 4, 60, 42 };

	// Regiões com os rótulos em inglês no CHARSTAT.IMG (texto dourado).
	constexpr Rect SheetLabelRegions[] =
	{
		{ 2, 50, 24, 64 }, { 46, 50, 40, 57 }, { 106, 50, 39, 49 },
		{ 2, 124, 43, 28 }, { 0, 155, 45, 20 }, { 12, 179, 30, 14 }
	};
}

arena::ChooseAttributesScreen::ChooseAttributesScreen(Game &game) : Screen(game)
{
	game.playMusic("SHEET.XMI");
	const CharacterDraft &character = game.getCharacter();
	const bool male = character.male;
	const int race = character.raceIndex;
	const bool magic = game.getData().getClass(character.classIndex).castsMagic;

	this->palette = game.loadPalette("CHARSHT.COL").value_or(game.getDefaultPalette());
	this->sheet = game.loadImage("CHARSTAT.IMG");
	this->body = game.loadImage(std::string(male ? "CHARBK0" : "CHRBKF0") + std::to_string(race) + ".IMG");
	this->shirt = game.loadImage(male ? (magic ? "MRSHIRT.IMG" : "MSSHIRT.IMG") : (magic ? "FRSHIRT.IMG" : "FSSHIRT.IMG"));
	this->pants = game.loadImage(male ? "MPANTS.IMG" : "FPANTS.IMG");
	this->upDown = game.loadImage("UPDOWN.IMG");
	this->bonusImage = game.loadImage("BONUS.IMG");
	this->heads = game.loadFrames(std::string(male ? "FACES1" : "FACESF1") + std::to_string(race) + ".CIF");
	this->shirtOffset = male ? (magic ? std::array<int, 2>{ 215, 35 } : std::array<int, 2>{ 186, 12 })
		: (magic ? std::array<int, 2>{ 220, 33 } : std::array<int, 2>{ 220, 35 });
	this->pantsOffset = male ? std::array<int, 2>{ 229, 82 } : std::array<int, 2>{ 212, 74 };

	for (const Rect &region : SheetLabelRegions)
	{
		eraseInkInRegion(this->sheet, this->palette, region, isGoldInk);
	}

	eraseInkInRegion(this->bonusImage, this->palette, Rect{ 2, 2, this->bonusImage.width - 4, this->bonusImage.height - 4 }, isGoldInk);

	game.getCharacter().portraitIndex = 0;
	this->rollStats();
	this->dialog.open(game.getText().get("CharacterCreation.DistributeClassPoints"), game.getFonts().arena, Dialog::Style::Dark);
}

void arena::ChooseAttributesScreen::rollStats()
{
	CharacterDraft &character = this->game.getCharacter();
	std::mt19937 &rng = this->game.getRandom();
	const ClassDefinition &def = this->game.getData().getClass(character.classIndex);

	character.attributes = this->game.getData().getBaseAttributes(character.raceIndex, character.male);
	for (int &value : character.attributes)
	{
		value += rollClassic(20, rng);
	}

	character.addedPoints.fill(0);
	character.maxHealth = 25 + 1 + static_cast<int>(rng() % static_cast<uint32_t>(std::max(1, def.healthDie)));
	character.gold = 50 + static_cast<int>(rng() % 150u);
	character.bonusPoints = rollClassic(25, rng);
	this->recalculate();
}

void arena::ChooseAttributesScreen::recalculate()
{
	CharacterDraft &character = this->game.getCharacter();
	const ClassDefinition &def = this->game.getData().getClass(character.classIndex);
	character.maxStamina = character.attributes[0] + character.attributes[5];
	character.maxSpellPoints = static_cast<int>(character.attributes[1] * def.spellPointsMultiplier);
}

void arena::ChooseAttributesScreen::changeAttribute(int index, int delta)
{
	CharacterDraft &character = this->game.getCharacter();
	if (delta > 0 && character.bonusPoints > 0)
	{
		character.attributes[static_cast<size_t>(index)]++;
		character.addedPoints[static_cast<size_t>(index)]++;
		character.bonusPoints--;
	}
	else if (delta < 0 && character.addedPoints[static_cast<size_t>(index)] > 0)
	{
		character.attributes[static_cast<size_t>(index)]--;
		character.addedPoints[static_cast<size_t>(index)]--;
		character.bonusPoints++;
	}

	this->recalculate();
}

void arena::ChooseAttributesScreen::finish()
{
	// Equipamento inicial da classe e gravação da ficha.
	CharacterDraft &character = this->game.getCharacter();
	character.inventory = startingKit(character.classIndex, static_cast<int>(this->game.getData().getClass(character.classIndex).armor));
	character.spells = starterSpells(character.classIndex, this->game.getData().getClass(character.classIndex).castsMagic);
	character.readySpell = character.spells.empty() ? -1 : character.spells.front();
	character.spellPoints = -1;
	character.spellbookKnown = true;
	character.experience = 0;
	character.health = -1;
	this->game.saveCharacter();

	this->stage = Stage::Finished;
	this->dialog.open(ui::format(this->game.gendered("CharacterCreation.CreationComplete"),
		{ character.name, this->game.className(character.classIndex), this->game.raceName(character.raceIndex, false) }),
		this->game.getFonts().arena, Dialog::Style::Dark);
}

void arena::ChooseAttributesScreen::handleInput(const InputEvent &event)
{
	const Localization &text = this->game.getText();
	CharacterDraft &character = this->game.getCharacter();

	if (this->dialog.isOpen())
	{
		const int result = this->dialog.handleInput(event);
		if (result == Dialog::None)
		{
			return;
		}

		switch (this->stage)
		{
		case Stage::Intro: this->stage = Stage::Editing; break;
		case Stage::SaveReroll:
			if (result == 0)
			{
				this->stage = Stage::Appearance;
				this->dialog.open(text.get("CharacterCreation.ChooseAppearance"), this->game.getFonts().arena, Dialog::Style::Dark);
			}
			else
			{
				this->rollStats();
				this->stage = Stage::Editing;
			}
			break;
		case Stage::Appearance: this->stage = Stage::Portrait; break;
		case Stage::Finished:
			changeTo(this->game, std::make_unique<GameWorldScreen>(this->game, text.get("World.StartDungeonMif")));
			break;
		default: break; // aviso de pontos restantes: volta à edição
		}

		return;
	}

	const bool editing = this->stage == Stage::Editing;
	const int headCount = std::max<int>(1, static_cast<int>(this->heads.size()));

	if (event.isClick())
	{
		const int row = (event.y - AttributeFirstY) / AttributeRowH;
		const bool inUp = event.x >= 38 && event.x < 46 && event.y >= 48 + this->selected * 8 && event.y < 56 + this->selected * 8;
		const bool inDown = event.x >= 38 && event.x < 46 && event.y >= 56 + this->selected * 8 && event.y < 64 + this->selected * 8;

		if (editing && inUp) this->changeAttribute(this->selected, +1);
		else if (editing && inDown) this->changeAttribute(this->selected, -1);
		else if (editing && event.x >= 2 && event.x < 36 && event.y >= AttributeFirstY && row >= 0 && row < AttributeCount) this->selected = row;
		else if (this->stage == Stage::Portrait && PortraitRect.contains(event.x, event.y))
		{
			const int step = (event.button == 3) ? -1 : 1;
			character.portraitIndex = (character.portraitIndex + step + headCount) % headCount;
		}
		else if (DoneRect.contains(event.x, event.y))
		{
			if (editing && character.bonusPoints > 0)
			{
				this->dialog.open(text.get("CharacterCreation.ChooseAttributesBonusPointsRemaining"), this->game.getFonts().arena, Dialog::Style::Dark);
			}
			else if (editing)
			{
				this->stage = Stage::SaveReroll;
				this->dialog.open(text.get("CharacterCreation.ChooseAttributes"), this->game.getFonts().a, Dialog::Style::Dark,
					{ text.get("CharacterCreation.ChooseAttributesSave"), text.get("CharacterCreation.ChooseAttributesReroll") });
			}
			else if (this->stage == Stage::Portrait)
			{
				this->finish();
			}
		}
	}
	else if (event.type == InputEvent::Type::KeyDown)
	{
		if (editing && event.key == InputEvent::Key::Up) this->selected = (this->selected + AttributeCount - 1) % AttributeCount;
		else if (editing && event.key == InputEvent::Key::Down) this->selected = (this->selected + 1) % AttributeCount;
		else if (editing && event.key == InputEvent::Key::Right) this->changeAttribute(this->selected, +1);
		else if (editing && event.key == InputEvent::Key::Left) this->changeAttribute(this->selected, -1);
		else if (this->stage == Stage::Portrait && (event.key == InputEvent::Key::Left || event.key == InputEvent::Key::Right))
		{
			const int step = (event.key == InputEvent::Key::Left) ? -1 : 1;
			character.portraitIndex = (character.portraitIndex + step + headCount) % headCount;
		}
		else if (event.key == InputEvent::Key::Enter)
		{
			this->handleInput(InputEvent{ InputEvent::Type::MouseDown, InputEvent::Key::None, 0, DoneRect.centerX(), DoneRect.centerY() });
		}
		else if (event.key == InputEvent::Key::Escape && editing)
		{
			changeTo(this->game, std::make_unique<ChooseRaceScreen>(this->game));
		}
	}
}

void arena::ChooseAttributesScreen::render(Canvas &canvas)
{
	const CharacterDraft &character = this->game.getCharacter();
	const Localization &text = this->game.getText();
	const BitmapFont &font = this->game.getFonts().arena;
	const uint8_t value = nearestColor(this->palette, 199, 199, 199);
	const uint8_t label = nearestColor(this->palette, 211, 142, 40);
	const uint8_t shadow = nearestColor(this->palette, 0, 0, 0);
	const uint8_t selectedColor = nearestColor(this->palette, 255, 230, 80);

	canvas.clear(0);
	canvas.blit(this->body, Canvas::Width - this->body.width, 0);
	canvas.blit(this->sheet, 0, 0, false);
	canvas.blit(this->pants, this->pantsOffset[0], this->pantsOffset[1]);
	canvas.blit(this->shirt, this->shirtOffset[0], this->shirtOffset[1]);
	if (!this->heads.empty())
	{
		const IndexedImage &head = this->heads[static_cast<size_t>(character.portraitIndex) % this->heads.size()];
		canvas.blit(head, head.offsetX, head.offsetY);
	}

	// Rótulos traduzidos (alinhados à direita onde estavam os originais).
	for (int i = 0; i < AttributeCount; i++)
	{
		canvas.drawText(font, this->game.attributeName(i, true), AttributeValueX - 2, AttributeFirstY + i * AttributeRowH, label, shadow, TextAlign::Right);
	}

	const std::pair<const char*, std::array<int, 2>> sheetLabels[] =
	{
		{ "Sheet.Damage", { 85, 52 } }, { "Sheet.SpellPoints", { 85, 60 } }, { "Sheet.MagicDefense", { 85, 68 } },
		{ "Sheet.ToHit", { 85, 76 } }, { "Sheet.HealthBonus", { 85, 92 } }, { "Sheet.Charisma", { 85, 100 } },
		{ "Sheet.MaxKilos", { 144, 52 } }, { "Sheet.ToDefend", { 144, 76 } }, { "Sheet.HealMod", { 144, 92 } },
		{ "Sheet.Health", { 44, 127 } }, { "Sheet.Fatigue", { 44, 135 } }, { "Sheet.Gold", { 44, 143 } },
		{ "Sheet.Experience", { 44, 158 } }, { "Sheet.Level", { 44, 167 } }
	};

	for (const auto &[key, pos] : sheetLabels)
	{
		canvas.drawText(font, text.get(key), pos[0], pos[1], label, shadow, TextAlign::Right);
	}

	canvas.drawText(font, text.get("Sheet.Done"), DoneRect.centerX(), DoneRect.y + 3, label, shadow, TextAlign::Center);

	// Identificação.
	canvas.drawText(font, character.name, 10, 8, value);
	canvas.drawText(font, this->game.raceName(character.raceIndex, false), 10, 17, value);
	canvas.drawText(font, this->game.className(character.classIndex), 10, 26, value);

	// Atributos primários.
	const bool editing = this->stage == Stage::Editing;
	for (int i = 0; i < AttributeCount; i++)
	{
		const bool isSelected = editing && i == this->selected;
		canvas.drawText(font, std::to_string(character.attributes[static_cast<size_t>(i)]), AttributeValueX, AttributeFirstY + i * AttributeRowH,
			isSelected ? selectedColor : value);
	}

	if (editing)
	{
		canvas.blit(this->upDown, 38, 48 + this->selected * 8);
	}

	// Atributos derivados.
	const auto &a = character.attributes;
	const int hit = hitBonus(a[3]);
	const int health = healthBonus(a[5]);
	canvas.drawText(font, signedValue(damageBonus(a[0])), 86, 52, value);
	canvas.drawText(font, std::to_string(character.maxSpellPoints), 86, 60, value);
	canvas.drawText(font, signedValue(magicDefense(a[2])), 86, 68, value);
	canvas.drawText(font, signedValue(hit), 86, 76, value);
	canvas.drawText(font, signedValue(health), 86, 92, value);
	canvas.drawText(font, signedValue(hitBonus(a[6])), 86, 100, value);
	canvas.drawText(font, std::to_string(a[0] * 2), 145, 52, value);
	canvas.drawText(font, signedValue(hit), 145, 76, value);
	canvas.drawText(font, signedValue(health), 145, 92, value);

	canvas.drawText(font, std::to_string(character.maxHealth) + "/" + std::to_string(character.maxHealth), 45, 127, value);
	canvas.drawText(font, std::to_string(character.maxStamina) + "/" + std::to_string(character.maxStamina), 45, 135, value);
	canvas.drawText(font, std::to_string(character.gold), 45, 143, value);
	canvas.drawText(font, "0", 45, 158, value);
	canvas.drawText(font, "1", 45, 167, value);

	if (editing || this->stage == Stage::Intro)
	{
		canvas.blit(this->bonusImage, 45, 109, false);
		canvas.drawText(font, text.get("Sheet.BonusPoints"), 90, 113, label, shadow, TextAlign::Right);
		canvas.drawText(font, std::to_string(character.bonusPoints), 92, 113, value);
	}

	if (this->stage == Stage::Portrait)
	{
		canvas.drawText(this->game.getFonts().d, text.get("Sheet.PortraitHint"), 108, 186,
			value, shadow, TextAlign::Center);
	}

	this->dialog.render(canvas, this->game, this->palette);
}
