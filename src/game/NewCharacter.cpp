// Criação de personagem em resolução nativa (assistente em passos), com as regras do original.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>

#include "../render/ImageOps.h"
#include "../render/Ui.h"
#include "GameWorldScreen.h"
#include "InventoryUi.h"
#include "Items.h"
#include "Spells.h"
#include "Leveling.h"
#include "NewCharacter.h"
#include "Screens.h"
#include "../world/SpriteOverrides.h"

namespace
{
	using namespace arena;
	namespace col = kit::color;

	enum ButtonId
	{
		Back = 1, Next = 2,
		ByQuestions = 10, ByList = 11,
		Answer = 20, AcceptSuggestion = 23, OtherClass = 24,
		Male = 30, Female = 31,
		Reroll = 40,
		FacePrevious = 50, FaceNext = 51,
		ClassRow = 100, Plus = 200, Minus = 300
	};

	constexpr int QuestionCount = 10;

	// Rótulos em inglês no TAMRIEL.MNU (apagados) e onde escrever os nomes em PT-BR (coordenadas 320x200).
	struct MapLabel { Rect erase; int centerX, topY; };
	constexpr MapLabel MapLabels[9] =
	{
		{ { 52, 50, 45, 11 }, 74, 52 }, { { 72, 75, 51, 11 }, 97, 77 }, { { 142, 43, 34, 11 }, 158, 45 },
		{ { 222, 84, 52, 11 }, 247, 86 }, { { 38, 148, 46, 18 }, 60, 150 }, { { 105, 146, 51, 11 }, 130, 148 },
		{ { 148, 126, 38, 11 }, 166, 128 }, { { 216, 143, 55, 11 }, 243, 145 }, { { 133, 104, 83, 11 }, 174, 106 }
	};
	constexpr Rect MapTitle{ 48, 2, 92, 19 };
	constexpr Rect MapExit{ 278, 186, 42, 14 }; // "EXIT" do mapa original (aqui não é botão)

	// Apaga as letras do título preenchendo cada pixel com a fita da mesma linha (a fita tem
	// sombreado vertical, então copiar na horizontal não deixa remendo chapado).
	void eraseTitleRow(arena::IndexedImage &image, const arena::Palette &palette, const arena::Rect &r)
	{
		auto isLetter = [&](int x, int y) { const arena::Color &c = palette[image.pixels[static_cast<size_t>(y) * image.width + x]]; return (c.r * 3 + c.g * 6 + c.b) / 10 < 70; };
		for (int y = r.y; y < r.y + r.h; y++)
		{
			std::vector<bool> letter(static_cast<size_t>(r.w), false);
			for (int x = r.x; x < r.x + r.w; x++)
				for (int dx = -1; dx <= 1; dx++)
					if (x + dx >= 0 && x + dx < image.width && isLetter(x + dx, y)) letter[static_cast<size_t>(x - r.x)] = true;
			for (int x = r.x; x < r.x + r.w; x++)
			{
				if (!letter[static_cast<size_t>(x - r.x)]) continue;
				int left = x, right = x;
				while (left > r.x && letter[static_cast<size_t>(left - r.x)]) left--;
				while (right < r.x + r.w - 1 && letter[static_cast<size_t>(right - r.x)]) right++;
				const bool leftOk = !letter[static_cast<size_t>(left - r.x)], rightOk = !letter[static_cast<size_t>(right - r.x)];
				const int source = (leftOk && (!rightOk || x - left <= right - x)) ? left : rightOk ? right : x;
				image.pixels[static_cast<size_t>(y) * image.width + x] = image.pixels[static_cast<size_t>(y) * image.width + source];
			}
		}
	}
}

arena::NewCharacterScreen::NewCharacterScreen(Game &game) : Screen(game)
{
	game.playMusic("SHEET.XMI");

	// 10 perguntas sorteadas entre as 40 do QUESTION.TXT.
	const int total = static_cast<int>(game.getData().getQuestions().size());
	this->questionOrder.resize(static_cast<size_t>(total));
	std::iota(this->questionOrder.begin(), this->questionOrder.end(), 0);
	std::shuffle(this->questionOrder.begin(), this->questionOrder.end(), game.getRandom());
	this->questionOrder.resize(static_cast<size_t>(std::min(total, QuestionCount)));

	// Classes agrupadas por caminho (magia, furtividade, combate), em ordem alfabética dentro de cada um.
	this->classOrder.resize(ClassCount);
	std::iota(this->classOrder.begin(), this->classOrder.end(), 0);
	std::stable_sort(this->classOrder.begin(), this->classOrder.end(), [&game](int a, int b)
	{
		if (a / 6 != b / 6) return a / 6 < b / 6;
		return ui::sortKey(game.className(a)) < ui::sortKey(game.className(b));
	});

	// Mapa de Tamriel sem os rótulos em inglês. Um TAMRIEL_MNU.png redesenhado entra como está
	// (com o título, sem os nomes das províncias, que o jogo escreve em PT-BR por cima).
	IndexedImage tamriel = game.loadImage("TAMRIEL.MNU");
	const Palette mapPalette = tamriel.palette ? *tamriel.palette : game.getDefaultPalette();
	RgbaImage redrawn = loadRgba(tamriel, mapPalette, "TAMRIEL.MNU", 0, 1);
	if (redrawn.fromPng)
	{
		this->map = Image{ std::move(redrawn.pixels), redrawn.width, redrawn.height };
	}
	else
	{
		for (const MapLabel &label : MapLabels) eraseInkInRegion(tamriel, mapPalette, label.erase, isRedInk);
		const auto isDarkInk = [](const Color &c) { return (c.r * 3 + c.g * 6 + c.b) / 10 < 100; };
		eraseTitleRow(tamriel, mapPalette, MapTitle);
		eraseInkInRegion(tamriel, mapPalette, MapExit, isDarkInk);

		// "Império de" escrito na própria arte (escala do mapa), na cor das letras de "Tamriel".
		if (tamriel.width == Canvas::Width && tamriel.height == Canvas::Height)
		{
			Canvas canvas;
			canvas.blit(tamriel, 0, 0, false);
			canvas.drawText(game.getFonts().c, game.getText().get("Map.TitleLeft"), MapTitle.x + 42, MapTitle.y + 3, nearestColor(mapPalette, 76, 32, 12), -1, TextAlign::Center);
			tamriel.pixels.assign(canvas.getPixels().begin(), canvas.getPixels().end());
		}

		this->map = this->convert(tamriel, mapPalette, {});
	}

	CharacterDraft &c = game.getCharacter();
	c = CharacterDraft{};
	c.male = true;
}

arena::NewCharacterScreen::Image arena::NewCharacterScreen::convert(const IndexedImage &image, const Palette &palette, const std::string &source, int frame, int frameCount) const
{
	RgbaImage rgba = loadRgba(image, palette, source, frame, frameCount);
	Image out;
	out.width = rgba.width;
	out.height = rgba.height;
	out.pixels = std::move(rgba.pixels);
	return out;
}

void arena::NewCharacterScreen::goTo(Step next)
{
	this->step = next;
	this->notice.clear();
	if (next == Step::Attributes && this->game.getCharacter().attributes[0] == 0) this->rollAttributes();
	if (next == Step::Portrait) this->loadAppearance();
}

bool arena::NewCharacterScreen::canAdvance() const
{
	const CharacterDraft &c = this->game.getCharacter();
	switch (this->step)
	{
	case Step::ClassList: return c.classIndex >= 0;
	case Step::Identity: return !c.name.empty();
	case Step::Province: return c.raceIndex >= 0;
	case Step::Attributes: return c.bonusPoints == 0;
	case Step::Portrait:
	case Step::Summary: return true;
	default: return false;
	}
}

void arena::NewCharacterScreen::advance()
{
	if (this->step == Step::Questions && this->suggestedClass >= 0)
	{
		this->game.getCharacter().classIndex = this->suggestedClass;
		this->goTo(Step::Identity);
		return;
	}

	if (!this->canAdvance())
	{
		if (this->step == Step::Attributes) this->notice = this->game.getText().get("Creation.SpendPoints");
		return;
	}

	switch (this->step)
	{
	case Step::ClassList: this->goTo(Step::Identity); break;
	case Step::Identity: this->goTo(Step::Province); break;
	case Step::Province: this->goTo(Step::Attributes); break;
	case Step::Attributes: this->goTo(Step::Portrait); break;
	case Step::Portrait: this->goTo(Step::Summary); break;
	case Step::Summary: this->finish(); break;
	default: break;
	}
}

void arena::NewCharacterScreen::back()
{
	switch (this->step)
	{
	case Step::Method: this->game.changeScreen(std::make_unique<MainMenuScreen>(this->game)); break;
	case Step::Questions:
	case Step::ClassList: this->goTo(Step::Method); break;
	case Step::Identity: this->goTo(Step::ClassList); break;
	case Step::Province: this->goTo(Step::Identity); break;
	case Step::Attributes: this->goTo(Step::Province); break;
	case Step::Portrait: this->goTo(Step::Attributes); break;
	case Step::Summary: this->goTo(Step::Portrait); break;
	}
}

void arena::NewCharacterScreen::answer(int choice)
{
	if (this->currentQuestion >= static_cast<int>(this->questionOrder.size())) return;
	const ClassQuestion &question = this->game.getData().getQuestions()[static_cast<size_t>(this->questionOrder[static_cast<size_t>(this->currentQuestion)])];
	this->categoryCounts[static_cast<size_t>(question.categories[static_cast<size_t>(choice)])]++;
	this->currentQuestion++;
	if (this->currentQuestion < static_cast<int>(this->questionOrder.size())) return;

	const int mage = this->categoryCounts[static_cast<size_t>(ClassCategory::Mage)];
	const int thief = this->categoryCounts[static_cast<size_t>(ClassCategory::Thief)];
	const int warrior = this->categoryCounts[static_cast<size_t>(ClassCategory::Warrior)];
	this->suggestedClass = this->game.getData().classFromAnswers(warrior, thief, mage);
	if (this->suggestedClass < 0) this->suggestedClass = (warrior >= thief && warrior >= mage) ? 16 : (thief >= mage) ? 10 : 0;
}

void arena::NewCharacterScreen::rollAttributes()
{
	// Base da raça + rolagem do original; pontos de bônus para distribuir.
	CharacterDraft &c = this->game.getCharacter();
	std::mt19937 &rng = this->game.getRandom();
	const ClassDefinition &def = this->game.getData().getClass(std::max(0, c.classIndex));
	c.attributes = this->game.getData().getBaseAttributes(std::max(0, c.raceIndex), c.male);
	for (int &value : c.attributes) value += rollClassic(20, rng);
	c.addedPoints.fill(0);
	c.maxHealth = 25 + 1 + static_cast<int>(rng() % static_cast<uint32_t>(std::max(1, def.healthDie)));
	c.gold = 50 + static_cast<int>(rng() % 150u);
	c.bonusPoints = rollClassic(25, rng);
	recalculateDerived(c, def);
}

void arena::NewCharacterScreen::changeAttribute(int index, int delta)
{
	CharacterDraft &c = this->game.getCharacter();
	auto &value = c.attributes[static_cast<size_t>(index)];
	auto &added = c.addedPoints[static_cast<size_t>(index)];
	if (delta > 0 && c.bonusPoints > 0 && value < MaxAttribute) { value++; added++; c.bonusPoints--; }
	else if (delta < 0 && added > 0) { value--; added--; c.bonusPoints++; }
	recalculateDerived(c, this->game.getData().getClass(std::max(0, c.classIndex)));
}

void arena::NewCharacterScreen::loadAppearance()
{
	const CharacterDraft &c = this->game.getCharacter();
	const Palette sheetPalette = this->game.loadPalette("CHARSHT.COL").value_or(this->game.getDefaultPalette());
	this->faces.clear();
	const std::string facesName = std::string(c.male ? "FACES1" : "FACESF1") + std::to_string(std::max(0, c.raceIndex)) + ".CIF";
	const std::vector<IndexedImage> faceImages = this->game.loadFrames(facesName);
	for (const IndexedImage &face : faceImages)
	{
		this->faces.push_back(this->convert(face, sheetPalette, facesName, static_cast<int>(this->faces.size()), static_cast<int>(faceImages.size())));
	}

	// Boneco com a roupa da classe (o equipamento entra no fim).
	this->doll.load(this->game);
	this->doll.compose(this->game, this->dollImage.pixels, this->dollImage.width, this->dollImage.height);
}

void arena::NewCharacterScreen::finish()
{
	// Equipamento e livro iniciais da classe; a ficha é gravada e o jogo começa na cela.
	CharacterDraft &c = this->game.getCharacter();
	const ClassDefinition &def = this->game.getData().getClass(c.classIndex);
	c.addedPoints.fill(0);
	c.inventory = startingKit(c.classIndex, static_cast<int>(def.armor));
	c.spells = starterSpells(c.classIndex, def.castsMagic);
	c.readySpell = c.spells.empty() ? -1 : c.spells.front();
	c.spellPoints = -1;
	c.spellbookKnown = true;
	c.experience = 0;
	c.level = 1;
	c.levelPoints = 0;
	c.health = -1;
	this->game.saveCharacter();
	this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, this->game.getText().get("World.StartDungeonMif")));
}

void arena::NewCharacterScreen::addButton(kit::Painter &p, const Rect &r, const std::string &label, int id, bool enabled, bool active)
{
	p.button(r, label, enabled && kit::hit(r, this->mouseX, this->mouseY), active, enabled);
	this->buttons.push_back(Button{ r, id, enabled });
}

void arena::NewCharacterScreen::handleInput(const InputEvent &event)
{
	eventToPixels(event, this->frameW, this->frameH, this->mouseX, this->mouseY);
	CharacterDraft &c = this->game.getCharacter();

	// Nome: digitação direta no passo de identidade.
	if (this->step == Step::Identity)
	{
		if (event.type == InputEvent::Type::TextInput)
		{
			for (char ch : event.text) if (static_cast<unsigned char>(ch) >= 32 && c.name.size() < 25) c.name.push_back(ch);
			return;
		}
		if (event.isKey(InputEvent::Key::Backspace) && !c.name.empty())
		{
			// Remove o último caractere UTF-8 inteiro.
			do { c.name.pop_back(); } while (!c.name.empty() && (static_cast<unsigned char>(c.name.back()) & 0xC0) == 0x80);
			return;
		}
	}

	if (event.isKey(InputEvent::Key::Escape)) { this->back(); return; }
	if (event.isKey(InputEvent::Key::Enter)) { this->advance(); return; }
	if (this->step == Step::Method && (event.isHotkey('P') || event.isHotkey('L')))
	{
		if (event.isHotkey('L')) this->goTo(Step::ClassList);
		else { this->currentQuestion = 0; this->categoryCounts.fill(0); this->suggestedClass = -1; this->goTo(Step::Questions); }
		return;
	}
	if (this->step == Step::Questions && this->suggestedClass < 0 && event.type == InputEvent::Type::KeyDown && event.key == InputEvent::Key::Character
		&& event.character >= 'A' && event.character <= 'C')
	{
		this->answer(event.character - 'A');
		return;
	}
	if (this->step == Step::Portrait && !this->faces.empty() && (event.isKey(InputEvent::Key::Left) || event.isKey(InputEvent::Key::Right)))
	{
		const int count = static_cast<int>(this->faces.size());
		c.portraitIndex = (c.portraitIndex + (event.isKey(InputEvent::Key::Left) ? count - 1 : 1)) % count;
		return;
	}

	if (event.type == InputEvent::Type::MouseMove && this->step == Step::Province)
	{
		this->hoveredProvince = -1;
		if (kit::hit(this->mapRect, this->mouseX, this->mouseY))
		{
			const int province = this->game.getData().provinceAt((this->mouseX - this->mapRect.x) * Canvas::Width / std::max(1, this->mapRect.w),
				(this->mouseY - this->mapRect.y) * Canvas::Height / std::max(1, this->mapRect.h));
			if (province >= 0 && province < 8) this->hoveredProvince = province;
		}
	}

	if (!event.isClick() || this->frameW <= 1) return;
	this->notice.clear();

	if (this->step == Step::Province && kit::hit(this->mapRect, this->mouseX, this->mouseY))
	{
		const int province = this->game.getData().provinceAt((this->mouseX - this->mapRect.x) * Canvas::Width / std::max(1, this->mapRect.w),
			(this->mouseY - this->mapRect.y) * Canvas::Height / std::max(1, this->mapRect.h));
		if (province >= 0 && province < 8 && province != c.raceIndex)
		{
			c.raceIndex = province;
			c.attributes.fill(0); // nova raça: os atributos serão rolados de novo
		}
		return;
	}

	for (const Button &button : this->buttons)
	{
		if (!button.enabled || !kit::hit(button.rect, this->mouseX, this->mouseY)) continue;
		const int id = button.id;
		if (id == Back) this->back();
		else if (id == Next) this->advance();
		else if (id == ByQuestions) { this->currentQuestion = 0; this->categoryCounts.fill(0); this->suggestedClass = -1; this->goTo(Step::Questions); }
		else if (id == ByList) this->goTo(Step::ClassList);
		else if (id >= Answer && id < Answer + 3) this->answer(id - Answer);
		else if (id == AcceptSuggestion) { c.classIndex = this->suggestedClass; this->goTo(Step::Identity); }
		else if (id == OtherClass) { c.classIndex = this->suggestedClass; this->goTo(Step::ClassList); }
		else if (id == Male || id == Female)
		{
			if (c.male != (id == Male)) c.attributes.fill(0);
			c.male = id == Male;
		}
		else if (id == Reroll) this->rollAttributes();
		else if (id == FacePrevious || id == FaceNext)
		{
			const int count = std::max(1, static_cast<int>(this->faces.size()));
			c.portraitIndex = (c.portraitIndex + (id == FacePrevious ? count - 1 : 1)) % count;
		}
		else if (id >= ClassRow && id < ClassRow + ClassCount) c.classIndex = id - ClassRow;
		else if (id >= Plus && id < Plus + AttributeCount) this->changeAttribute(id - Plus, +1);
		else if (id >= Minus && id < Minus + AttributeCount) this->changeAttribute(id - Minus, -1);
		return;
	}
}

void arena::NewCharacterScreen::renderWorld(std::vector<uint32_t> &pixels, int width, int height)
{
	this->frameW = width;
	this->frameH = height;
	pixels.assign(static_cast<size_t>(width) * height, 0xFF000000u);
	hud::Frame frame{ pixels, width, height };
	kit::Painter p(frame, this->game.getFonts());
	const int u = this->unitPx = p.unit();
	const Fonts &fonts = this->game.getFonts();
	const Localization &text = this->game.getText();
	this->buttons.clear();

	// Fundo: madeira escura em tela cheia; janela principal com cabeçalho de passos.
	p.woodPanel(Rect{ 0, 0, width, height }, 255);
	const int w = std::min(width - 16 * u, 560 * u), h = std::min(height - 16 * u, 340 * u);
	const Rect window{ (width - w) / 2, (height - h) / 2, w, h };
	p.text(fonts.a, text.get("Creation.Title"), width / 2, window.y + 2 * u, col::GoldBright, TextAlign::Center);

	// Trilha dos passos (o atual em destaque).
	const char *stepKeys[] = { "Creation.StepClass", "Creation.StepIdentity", "Creation.StepProvince", "Creation.StepAttributes", "Creation.StepFace", "Creation.StepSummary" };
	const int stepIndex = (this->step <= Step::ClassList) ? 0 : static_cast<int>(this->step) - 2;
	const int chipW = (w - 10 * u) / 6;
	for (int i = 0; i < 6; i++)
	{
		const Rect chip{ window.x + 5 * u + i * chipW, window.y + 22 * u, chipW - 4 * u, 14 * u };
		p.fill(chip, i == stepIndex ? hud::rgb(201, 158, 82, 80) : hud::rgb(0, 0, 0, 90));
		p.fill(Rect{ chip.x, chip.y + chip.h - u, chip.w, u }, i <= stepIndex ? col::Gold : col::GoldDim);
		p.text(fonts.arena, std::to_string(i + 1) + ". " + text.get(stepKeys[i]), chip.x + chip.w / 2, chip.y + 3 * u, i == stepIndex ? col::GoldBright : col::TextDim, TextAlign::Center);
	}

	const Rect content{ window.x, window.y + 42 * u, w, h - 42 * u - 28 * u };
	p.parchmentPanel(content);
	const Rect area{ content.x + 14 * u, content.y + 12 * u, content.w - 28 * u, content.h - 24 * u };
	switch (this->step)
	{
	case Step::Method: this->drawMethod(p, area); break;
	case Step::Questions: this->drawQuestions(p, area); break;
	case Step::ClassList: this->drawClassList(p, area); break;
	case Step::Identity: this->drawIdentity(p, area); break;
	case Step::Province: this->drawProvince(p, area); break;
	case Step::Attributes: this->drawAttributes(p, area); break;
	case Step::Portrait: this->drawPortrait(p, area); break;
	case Step::Summary: this->drawSummary(p, area); break;
	}

	// Navegação.
	const int by = window.y + h - 22 * u;
	this->addButton(p, Rect{ window.x, by, 100 * u, 20 * u }, text.get(this->step == Step::Method ? "Creation.BackToMenu" : "Creation.Back"), Back);
	const bool hasNext = this->step >= Step::ClassList && !(this->step == Step::Questions);
	if (hasNext)
	{
		const std::string label = text.get(this->step == Step::Summary ? "Creation.Start" : "Creation.Next");
		this->addButton(p, Rect{ window.x + w - 120 * u, by, 120 * u, 20 * u }, label, Next, this->canAdvance());
	}
	if (!this->notice.empty()) p.text(fonts.arena, this->notice, width / 2, by + 6 * u, col::Danger, TextAlign::Center);
}

void arena::NewCharacterScreen::drawMethod(kit::Painter &p, const Rect &area)
{
	const int u = this->unitPx;
	const Fonts &fonts = this->game.getFonts();
	const Localization &text = this->game.getText();
	p.text(fonts.a, text.get("Creation.MethodTitle"), area.x + area.w / 2, area.y + 10 * u, col::Ink, TextAlign::Center, false);
	p.paragraph(fonts.arena, text.get("Creation.MethodIntro"), Rect{ area.x + 30 * u, area.y + 34 * u, area.w - 60 * u, 40 * u }, col::InkSoft);
	const int bw = 220 * u, bh = 26 * u;
	this->addButton(p, Rect{ area.x + area.w / 2 - bw / 2, area.y + 90 * u, bw, bh }, text.get("Creation.ByQuestions"), ByQuestions);
	p.text(fonts.arena, text.get("Creation.ByQuestionsHint"), area.x + area.w / 2, area.y + 120 * u, col::InkSoft, TextAlign::Center, false);
	this->addButton(p, Rect{ area.x + area.w / 2 - bw / 2, area.y + 146 * u, bw, bh }, text.get("Creation.ByList"), ByList);
	p.text(fonts.arena, text.get("Creation.ByListHint"), area.x + area.w / 2, area.y + 176 * u, col::InkSoft, TextAlign::Center, false);
}

void arena::NewCharacterScreen::drawQuestions(kit::Painter &p, const Rect &area)
{
	const int u = this->unitPx;
	const Fonts &fonts = this->game.getFonts();
	const Localization &text = this->game.getText();
	if (this->suggestedClass >= 0)
	{
		// Resultado: a classe sugerida pelas respostas.
		p.text(fonts.a, ui::format(text.get("Creation.SuggestedTitle"), { this->game.className(this->suggestedClass) }), area.x + area.w / 2, area.y + 10 * u, col::Ink, TextAlign::Center, false);
		const ClassDefinition &def = this->game.getData().getClass(this->suggestedClass);
		const std::string description = ui::format(text.get("ClassInfo.Description"), { this->game.className(this->suggestedClass),
			text.get("ClassInfo.Category" + std::to_string(static_cast<int>(def.category))), text.get("ClassInfo.Armor" + std::to_string(static_cast<int>(def.armor))),
			this->game.localized("Classes.Preferred" + std::to_string(this->suggestedClass), def.originalPreferred), std::to_string(def.healthDie),
			def.castsMagic ? text.get("Creation.Caster") : text.get("ClassInfo.NoMagic") });
		p.paragraph(fonts.arena, description, Rect{ area.x + 40 * u, area.y + 40 * u, area.w - 80 * u, 80 * u }, col::Ink);
		const int bw = 180 * u;
		this->addButton(p, Rect{ area.x + area.w / 2 - bw - 6 * u, area.y + area.h - 30 * u, bw, 22 * u }, text.get("Creation.Accept"), AcceptSuggestion);
		this->addButton(p, Rect{ area.x + area.w / 2 + 6 * u, area.y + area.h - 30 * u, bw, 22 * u }, text.get("Creation.OtherClass"), OtherClass);
		return;
	}

	const ClassQuestion &question = this->game.getData().getQuestions()[static_cast<size_t>(this->questionOrder[static_cast<size_t>(this->currentQuestion)])];
	p.text(fonts.arena, ui::format(text.get("CharacterCreation.QuestionHeader"), { std::to_string(this->currentQuestion + 1), std::to_string(this->questionOrder.size()) }),
		area.x + area.w / 2, area.y, col::InkSoft, TextAlign::Center, false);
	int y = area.y + 16 * u;
	y += p.paragraph(fonts.arena, question.text, Rect{ area.x + 10 * u, y, area.w - 20 * u, area.h / 2 }, col::Ink) + 8 * u;
	for (int i = 0; i < 3; i++)
	{
		const std::string label = std::string(1, static_cast<char>('A' + i)) + ") " + question.choices[static_cast<size_t>(i)];
		const std::string wrapped = ui::wrapText(fonts.arena, label, (area.w - 40 * u) / u);
		const int lines = 1 + static_cast<int>(std::count(wrapped.begin(), wrapped.end(), '\n'));
		const Rect r{ area.x + 10 * u, y, area.w - 20 * u, lines * p.lineHeight(fonts.arena) + 8 * u };
		const bool hovered = kit::hit(r, this->mouseX, this->mouseY);
		p.rowHighlight(r, false, hovered, true);
		p.fill(Rect{ r.x, r.y, 2 * u, r.h }, hovered ? col::Gold : col::GoldDim);
		p.paragraph(fonts.arena, wrapped, Rect{ r.x + 10 * u, r.y + 4 * u, r.w - 14 * u, r.h }, hovered ? hud::rgb(120, 60, 0) : col::Ink);
		this->buttons.push_back(Button{ r, Answer + i, true });
		y += r.h + 6 * u;
	}
	p.text(fonts.arena, text.get("Creation.AnswerHint"), area.x + area.w / 2, area.y + area.h - 10 * u, col::InkSoft, TextAlign::Center, false);
}

void arena::NewCharacterScreen::drawClassList(kit::Painter &p, const Rect &area)
{
	const int u = this->unitPx;
	const Fonts &fonts = this->game.getFonts();
	const Localization &text = this->game.getText();
	const CharacterDraft &c = this->game.getCharacter();

	// Três colunas (magia, furtividade, combate) e a descrição embaixo.
	const int colW = (area.w - 16 * u) / 3;
	for (int category = 0; category < 3; category++)
	{
		const int x = area.x + category * (colW + 8 * u);
		p.text(fonts.arena, text.get("ClassInfo.Category" + std::to_string(category)), x + colW / 2, area.y, col::InkSoft, TextAlign::Center, false);
		int row = 0;
		for (int index : this->classOrder)
		{
			if (index / 6 != category) continue;
			const Rect r{ x, area.y + 14 * u + row * 16 * u, colW, 14 * u };
			const bool selected = c.classIndex == index;
			p.rowHighlight(r, selected, kit::hit(r, this->mouseX, this->mouseY), true);
			p.text(fonts.arena, this->game.className(index), r.x + 6 * u, r.y + 3 * u, selected ? hud::rgb(120, 60, 0) : col::Ink, TextAlign::Left, false);
			this->buttons.push_back(Button{ r, ClassRow + index, true });
			row++;
		}
	}

	if (c.classIndex >= 0)
	{
		const ClassDefinition &def = this->game.getData().getClass(c.classIndex);
		const int y = area.y + 14 * u + 6 * 16 * u + 8 * u;
		p.divider(area.x, y, area.w, col::InkSoft);
		std::string magic = text.get("ClassInfo.NoMagic");
		if (def.castsMagic)
		{
			char multiplier[16];
			std::snprintf(multiplier, sizeof(multiplier), "%.2g", def.spellPointsMultiplier);
			magic = ui::format(text.get("ClassInfo.Magic"), { multiplier });
		}
		const std::string description = ui::format(text.get("ClassInfo.Description"), { this->game.className(c.classIndex),
			text.get("ClassInfo.Category" + std::to_string(static_cast<int>(def.category))), text.get("ClassInfo.Armor" + std::to_string(static_cast<int>(def.armor))),
			this->game.localized("Classes.Preferred" + std::to_string(c.classIndex), def.originalPreferred), std::to_string(def.healthDie), magic });
		p.paragraph(fonts.arena, description, Rect{ area.x + 10 * u, y + 8 * u, area.w - 20 * u, area.y + area.h - y - 8 * u }, col::Ink);
	}
}

void arena::NewCharacterScreen::drawIdentity(kit::Painter &p, const Rect &area)
{
	const int u = this->unitPx;
	const Fonts &fonts = this->game.getFonts();
	const Localization &text = this->game.getText();
	const CharacterDraft &c = this->game.getCharacter();

	p.text(fonts.a, ui::format(text.get("CharacterCreation.ChooseName"), { this->game.className(c.classIndex) }), area.x + area.w / 2, area.y + 10 * u, col::Ink, TextAlign::Center, false);
	const Rect field{ area.x + area.w / 2 - 130 * u, area.y + 40 * u, 260 * u, 24 * u };
	p.inset(field, true);
	const bool blink = (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 500) % 2 == 0;
	p.text(fonts.a, c.name + (blink ? "_" : " "), field.x + 8 * u, field.y + 5 * u, col::Ink, TextAlign::Left, false);
	p.text(fonts.arena, text.get("Creation.NameHint"), area.x + area.w / 2, field.y + field.h + 6 * u, col::InkSoft, TextAlign::Center, false);

	p.text(fonts.a, text.get("CharacterCreation.ChooseGender"), area.x + area.w / 2, area.y + 110 * u, col::Ink, TextAlign::Center, false);
	const int bw = 120 * u;
	this->addButton(p, Rect{ area.x + area.w / 2 - bw - 6 * u, area.y + 136 * u, bw, 22 * u }, text.get("CharacterCreation.ChooseGenderMale"), Male, true, c.male);
	this->addButton(p, Rect{ area.x + area.w / 2 + 6 * u, area.y + 136 * u, bw, 22 * u }, text.get("CharacterCreation.ChooseGenderFemale"), Female, true, !c.male);
}

void arena::NewCharacterScreen::drawProvince(kit::Painter &p, const Rect &area)
{
	const int u = this->unitPx;
	const Fonts &fonts = this->game.getFonts();
	const Localization &text = this->game.getText();
	const CharacterDraft &c = this->game.getCharacter();

	// Mapa à esquerda (proporção 320x200 corrigida para 4:3), descrição à direita.
	const int mapH = area.h;
	const int mapW = std::min(area.w * 62 / 100, mapH * 4 / 3);
	this->mapRect = Rect{ area.x, area.y, mapW, mapW * 3 / 4 };
	p.image(this->map.pixels, this->map.width, this->map.height, this->mapRect);
	for (int i = 0; i < 9; i++)
	{
		const MapLabel &label = MapLabels[i];
		const int lx = this->mapRect.x + label.centerX * this->mapRect.w / Canvas::Width;
		const int ly = this->mapRect.y + label.topY * this->mapRect.h / Canvas::Height;
		const bool active = i == c.raceIndex || i == this->hoveredProvince;
		const uint32_t tint = (i == c.raceIndex) ? col::GoldBright : active ? hud::rgb(255, 240, 160) : hud::rgb(200, 30, 20);
		// Rótulos de duas linhas (Ilha Summerset) vêm separados por \n.
		const std::string name = text.getOr("Map.Label" + std::to_string(i), this->game.provinceName(i));
		size_t start = 0;
		for (int line = 0; start <= name.size(); line++)
		{
			const size_t end = std::min(name.find('\n', start), name.size());
			p.text(fonts.arena, name.substr(start, end - start), lx, ly + line * p.lineHeight(fonts.arena), tint, TextAlign::Center, true);
			start = end + 1;
		}
	}

	const Rect info{ this->mapRect.x + this->mapRect.w + 12 * u, area.y, area.x + area.w - (this->mapRect.x + this->mapRect.w + 12 * u), area.h };
	const int shown = this->hoveredProvince >= 0 ? this->hoveredProvince : c.raceIndex;
	if (shown < 0)
	{
		p.paragraph(fonts.arena, ui::format(this->game.gendered("Creation.ProvinceIntro"), { c.name, this->game.className(c.classIndex) }), info, col::Ink);
		return;
	}

	p.text(fonts.a, this->game.provinceName(shown), info.x, info.y, col::Ink, TextAlign::Left, false);
	p.text(fonts.arena, ui::format(text.get("Creation.RaceOf"), { this->game.raceName(shown, true) }), info.x, info.y + 20 * u, col::InkSoft, TextAlign::Left, false);
	p.paragraph(fonts.arena, text.get("Races.Description" + std::to_string(shown)), Rect{ info.x, info.y + 36 * u, info.w, info.h - 36 * u }, col::Ink);
}

void arena::NewCharacterScreen::drawAttributes(kit::Painter &p, const Rect &area)
{
	const int u = this->unitPx;
	const Fonts &fonts = this->game.getFonts();
	const Localization &text = this->game.getText();
	const CharacterDraft &c = this->game.getCharacter();

	p.text(fonts.arena, ui::format(text.get("Creation.BonusPoints"), { std::to_string(c.bonusPoints) }), area.x, area.y, c.bonusPoints > 0 ? hud::rgb(140, 60, 0) : col::InkSoft, TextAlign::Left, false);
	p.text(fonts.arena, ui::format(text.get("Creation.Ideal"), { this->game.localized("Classes.Preferred" + std::to_string(c.classIndex),
		this->game.getData().getClass(c.classIndex).originalPreferred) }), area.x + area.w, area.y, col::InkSoft, TextAlign::Right, false);

	const int rowH = 18 * u, listW = area.w * 55 / 100;
	for (int i = 0; i < AttributeCount; i++)
	{
		const Rect row{ area.x, area.y + 16 * u + i * rowH, listW, rowH - 3 * u };
		p.rowHighlight(row, false, kit::hit(row, this->mouseX, this->mouseY), true);
		const int ty = row.y + (row.h - fonts.arena.getHeight() * u) / 2;
		p.text(fonts.arena, this->game.attributeName(i, false), row.x + 6 * u, ty, col::Ink, TextAlign::Left, false);
		const int added = c.addedPoints[static_cast<size_t>(i)];
		p.text(fonts.arena, std::to_string(c.attributes[static_cast<size_t>(i)]), row.x + row.w - 60 * u, ty, added > 0 ? hud::rgb(40, 110, 30) : col::Ink, TextAlign::Right, false);
		this->addButton(p, Rect{ row.x + row.w - 50 * u, row.y + u, 22 * u, row.h - 2 * u }, "-", Minus + i, added > 0);
		this->addButton(p, Rect{ row.x + row.w - 25 * u, row.y + u, 22 * u, row.h - 2 * u }, "+", Plus + i, c.bonusPoints > 0 && c.attributes[static_cast<size_t>(i)] < MaxAttribute);
	}

	// Derivados.
	const int dx = area.x + listW + 20 * u;
	int y = area.y + 20 * u;
	const std::pair<std::string, std::string> derived[] =
	{
		{ text.get("Sheet.Health"), std::to_string(c.maxHealth) }, { text.get("Sheet.Fatigue"), std::to_string(c.maxStamina) },
		{ text.get("Sheet.SpellPoints"), std::to_string(c.maxSpellPoints) }, { text.get("Menu.Gold"), std::to_string(c.gold) },
		{ text.get("Menu.Damage"), (damageBonusFor(c.attributes[0]) > 0 ? "+" : "") + std::to_string(damageBonusFor(c.attributes[0])) },
		{ text.get("Menu.ToHit"), (hitBonusFor(c.attributes[3]) > 0 ? "+" : "") + std::to_string(hitBonusFor(c.attributes[3])) },
	};
	for (const auto &[label, value] : derived)
	{
		p.text(fonts.arena, label, dx, y, col::InkSoft, TextAlign::Left, false);
		p.text(fonts.arena, value, area.x + area.w, y, col::Ink, TextAlign::Right, false);
		y += 15 * u;
	}

	this->addButton(p, Rect{ dx, area.y + area.h - 24 * u, area.x + area.w - dx, 22 * u }, text.get("Creation.Reroll"), Reroll);
}

void arena::NewCharacterScreen::drawPortrait(kit::Painter &p, const Rect &area)
{
	const int u = this->unitPx;
	const Fonts &fonts = this->game.getFonts();
	const Localization &text = this->game.getText();
	const CharacterDraft &c = this->game.getCharacter();

	// Boneco à esquerda; rosto grande com setas à direita.
	const int dollH = area.h, dollW = dollH * std::max(1, this->dollImage.width) / std::max(1, this->dollImage.height);
	const Rect dollRect{ area.x, area.y, dollW, dollH };
	p.fill(Rect{ dollRect.x - 2 * u, dollRect.y - 2 * u, dollRect.w + 4 * u, dollRect.h + 4 * u }, col::GoldDim);
	p.image(this->dollImage.pixels, this->dollImage.width, this->dollImage.height, dollRect);

	const int fx = dollRect.x + dollRect.w + 30 * u;
	const int fw = area.x + area.w - fx;
	p.text(fonts.a, text.get("Creation.FaceTitle"), fx + fw / 2, area.y, col::Ink, TextAlign::Center, false);
	if (!this->faces.empty())
	{
		const Image &face = this->faces[static_cast<size_t>(c.portraitIndex) % this->faces.size()];
		const int size = std::min(fw - 80 * u, area.h - 70 * u);
		const Rect frame{ fx + fw / 2 - size / 2, area.y + 26 * u, size, size * face.height / std::max(1, face.width) };
		p.fill(Rect{ frame.x - 2 * u, frame.y - 2 * u, frame.w + 4 * u, frame.h + 4 * u }, col::GoldDim);
		p.fill(frame, hud::rgb(40, 30, 20));
		p.image(face.pixels, face.width, face.height, frame);
		this->addButton(p, Rect{ frame.x - 34 * u, frame.y + frame.h / 2 - 12 * u, 26 * u, 24 * u }, "<", FacePrevious);
		this->addButton(p, Rect{ frame.x + frame.w + 8 * u, frame.y + frame.h / 2 - 12 * u, 26 * u, 24 * u }, ">", FaceNext);
		p.text(fonts.arena, ui::format(text.get("Creation.FaceCount"), { std::to_string(c.portraitIndex % this->faces.size() + 1), std::to_string(this->faces.size()) }),
			fx + fw / 2, frame.y + frame.h + 8 * u, col::InkSoft, TextAlign::Center, false);
	}
}

void arena::NewCharacterScreen::drawSummary(kit::Painter &p, const Rect &area)
{
	const int u = this->unitPx;
	const Fonts &fonts = this->game.getFonts();
	const Localization &text = this->game.getText();
	const CharacterDraft &c = this->game.getCharacter();

	if (!this->faces.empty())
	{
		const Image &face = this->faces[static_cast<size_t>(c.portraitIndex) % this->faces.size()];
		const Rect frame{ area.x, area.y, 90 * u, 90 * u * face.height / std::max(1, face.width) };
		p.fill(Rect{ frame.x - 2 * u, frame.y - 2 * u, frame.w + 4 * u, frame.h + 4 * u }, col::GoldDim);
		p.image(face.pixels, face.width, face.height, frame);
	}

	const int x = area.x + 110 * u;
	p.text(fonts.a, c.name, x, area.y, col::Ink, TextAlign::Left, false);
	p.text(fonts.arena, this->game.raceName(c.raceIndex, false) + " - " + this->game.className(c.classIndex) + " - " + this->game.provinceName(c.raceIndex),
		x, area.y + 22 * u, col::InkSoft, TextAlign::Left, false);
	int y = area.y + 44 * u;
	const int colW = (area.x + area.w - x) / 2;
	for (int i = 0; i < AttributeCount; i++)
	{
		const int cx = x + (i / 4) * colW, cy = y + (i % 4) * 14 * u;
		p.text(fonts.arena, this->game.attributeName(i, false), cx, cy, col::InkSoft, TextAlign::Left, false);
		p.text(fonts.arena, std::to_string(c.attributes[static_cast<size_t>(i)]), cx + colW - 16 * u, cy, col::Ink, TextAlign::Right, false);
	}

	y += 4 * 14 * u + 10 * u;
	p.text(fonts.arena, text.get("Sheet.Health") + " " + std::to_string(c.maxHealth) + "    " + text.get("Sheet.SpellPoints") + " " + std::to_string(c.maxSpellPoints)
		+ "    " + text.get("Menu.Gold") + " " + std::to_string(c.gold), x, y, col::Ink, TextAlign::Left, false);
	p.paragraph(fonts.arena, text.get("CharacterCreation.ConfirmedRace4"), Rect{ area.x, area.y + area.h - 40 * u, area.w, 40 * u }, col::InkSoft);
}
