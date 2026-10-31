#include <algorithm>

#include "../core/Utf8.h"
#include "CharacterCreation.h"
#include "Cutscene.h"
#include "GameWorldScreen.h"
#include "NewCharacter.h"
#include "../world/SpriteOverrides.h"
#include "Screens.h"

namespace
{
	int measureBlock(const arena::BitmapFont &font, const std::string &text, int &outLines)
	{
		int maxWidth = 0;
		outLines = 0;
		size_t start = 0;
		while (start <= text.size())
		{
			const size_t end = std::min(text.find('\n', start), text.size());
			const std::vector<char32_t> line = arena::decodeUtf8(std::string_view(text).substr(start, end - start));
			maxWidth = std::max(maxWidth, font.measure(std::u32string(line.begin(), line.end())));
			outLines++;
			start = end + 1;
		}

		return maxWidth;
	}

	// Desenha o rótulo com a letra de atalho em vermelho, como no menu original.
	void drawHotkeyLabel(arena::Canvas &canvas, const arena::BitmapFont &font, const arena::TextButton &button,
		uint8_t ink, uint8_t accent, bool hovered)
	{
		const std::vector<char32_t> text = arena::decodeUtf8(button.label);
		const std::u32string label(text.begin(), text.end());
		const int width = font.measure(label);
		int x = button.bounds.x + (button.bounds.w - width) / 2;
		const int y = button.bounds.y + (button.bounds.h - font.getHeight()) / 2;

		bool hotkeyDrawn = false;
		for (char32_t c : label)
		{
			const bool isHotkey = !hotkeyDrawn && button.hotkey != 0 &&
				(c == static_cast<char32_t>(button.hotkey) || c == static_cast<char32_t>(button.hotkey + ('a' - 'A')));
			hotkeyDrawn = hotkeyDrawn || isHotkey;

			const uint8_t color = (hovered || isHotkey) ? accent : ink;
			for (const auto &[gx, gy] : font.getGlyph(c).pixels)
			{
				canvas.setPixel(x + gx, y + gy, color);
			}

			x += font.getGlyph(c).advance;
		}
	}

	int hitTest(const std::vector<arena::TextButton> &buttons, int x, int y)
	{
		for (size_t i = 0; i < buttons.size(); i++)
		{
			if (buttons[i].bounds.contains(x, y)) return static_cast<int>(i);
		}

		return -1;
	}

	char hotkeyFrom(const arena::Localization &text, const std::string &key)
	{
		const std::string &value = text.get(key);
		return value.empty() ? 0 : static_cast<char>(value[0]);
	}
}

void arena::MessagePopup::render(Canvas &canvas, const Palette &palette, const BitmapFont &font) const
{
	if (!this->visible)
	{
		return;
	}

	int lines = 0;
	const int textWidth = measureBlock(font, this->text, lines);
	const int lineHeight = font.getHeight() + 2;
	const int w = textWidth + 20;
	const int h = lines * lineHeight + 16;
	const int x = (Canvas::Width - w) / 2;
	const int y = (Canvas::Height - h) / 2;

	const uint8_t border = nearestColor(palette, 50, 25, 10);
	const uint8_t parchment = nearestColor(palette, 180, 140, 90);
	const uint8_t ink = nearestColor(palette, 45, 20, 10);

	canvas.fillRect(x + 3, y + 3, w, h, nearestColor(palette, 0, 0, 0));
	canvas.fillRect(x, y, w, h, border);
	canvas.fillRect(x + 2, y + 2, w - 4, h - 4, parchment);
	canvas.drawTextBlock(font, this->text, Canvas::Width / 2, y + 9, ink, -1, TextAlign::Center);
}

// ---------------------------------------------------------------------------
// Tela de título

void arena::TitleArt::load(const Game &game)
{
	const IndexedImage title = game.loadImage("TITLE.IMG");
	RgbaImage rgba = loadRgba(title, title.palette ? *title.palette : game.getDefaultPalette(), "TITLE.IMG", 0, 1, false);
	this->pixels = std::move(rgba.pixels);
	this->width = rgba.width;
	this->height = rgba.height;
}

void arena::TitleArt::draw(kit::Painter &painter, int frameW, int frameH) const
{
	const int drawH = frameH, drawW = std::min(frameW, frameH * 4 / 3);
	painter.image(this->pixels, this->width, this->height, Rect{ (frameW - drawW) / 2, 0, drawW, drawH });
}

arena::TitleScreen::TitleScreen(Game &game) : Screen(game)
{
	game.playMusic("PERCNTRO.XMI");
	this->art.load(game);
}

void arena::TitleScreen::renderWorld(std::vector<uint32_t> &pixels, int width, int height)
{
	pixels.assign(static_cast<size_t>(width) * height, 0xFF000000u);
	hud::Frame frame{ pixels, width, height };
	kit::Painter painter(frame, this->game.getFonts());
	this->art.draw(painter, width, height);
}

void arena::TitleScreen::handleInput(const InputEvent &event)
{
	if (event.type == InputEvent::Type::KeyDown || event.type == InputEvent::Type::MouseDown)
	{
		this->game.changeScreen(std::make_unique<MainMenuScreen>(this->game));
	}
}

void arena::TitleScreen::render(Canvas &canvas)
{
	canvas.clear(0);
}

const arena::Palette &arena::TitleScreen::getPalette() const
{
	return this->game.getDefaultPalette();
}

// ---------------------------------------------------------------------------
// Menu principal

arena::MainMenuScreen::MainMenuScreen(Game &game) : Screen(game), loadMenu(std::make_unique<SaveLoadMenu>())
{
	game.playMusic("PERCNTRO.XMI");

	this->art.load(game);

	const Game::SaveSummary latest = game.readSaveSummary(game.getSavePath());
	this->canContinue = latest.exists;
	if (latest.exists)
	{
		this->continueLabel = latest.name + " - " + game.getText().get("Menu.Level") + " " + std::to_string(latest.level)
			+ (latest.location.empty() ? std::string() : " - " + latest.location);
	}
}

void arena::MainMenuScreen::activate(Action action)
{
	switch (action)
	{
	case Action::Continue:
		if (this->game.loadSavedCharacter())
		{
			this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, this->game.startingMap()));
		}
		else
		{
			this->notice = this->game.getText().get("MainMenu.NoSavedCharacter");
		}
		break;
	case Action::Load:
		this->loadMenu->open(this->game, SaveLoadMenu::Mode::Load);
		this->loadOpen = true;
		break;
	case Action::NewGame:
		// Novo jogo: o Imperador e a traição de Tharn (KING e MORPH) antes da criação.
		this->game.getCharacter() = CharacterDraft{};
		setCustomSpells({});
		this->game.changeScreen(std::make_unique<CutsceneScreen>(this->game, std::vector<std::string>{ "KING.FLC", "MORPH.FLC" },
			[](Game &owner) -> std::unique_ptr<Screen> { return std::make_unique<NewCharacterScreen>(owner); }));
		break;
	case Action::Exit:
		this->game.requestQuit();
		break;
	}
}

void arena::MainMenuScreen::handleInput(const InputEvent &event)
{
	if (this->loadOpen)
	{
		const SaveLoadMenu::Choice choice = this->loadMenu->handleInput(event, this->game);
		if (choice.slot > 0 && this->game.loadSavedCharacter(this->game.getSlotPath(choice.slot)))
		{
			this->game.changeScreen(std::make_unique<GameWorldScreen>(this->game, this->game.startingMap()));
		}
		else if (choice.close)
		{
			this->loadOpen = false;
		}
		return;
	}

	eventToPixels(event, this->frameW, this->frameH, this->mouseX, this->mouseY);
	const int count = static_cast<int>(this->buttons.size());
	if (event.type == InputEvent::Type::MouseMove)
	{
		this->hovered = -1;
		for (int i = 0; i < count; i++) if (kit::hit(this->buttons[static_cast<size_t>(i)].first, this->mouseX, this->mouseY)) this->hovered = i;
	}
	else if (event.isClick())
	{
		this->notice.clear();
		for (const auto &[rect, action] : this->buttons) if (kit::hit(rect, this->mouseX, this->mouseY)) { this->activate(action); return; }
	}
	else if ((event.isKey(InputEvent::Key::Left) || event.isKey(InputEvent::Key::Up)) && count > 0) this->hovered = (this->hovered <= 0) ? count - 1 : this->hovered - 1;
	else if ((event.isKey(InputEvent::Key::Right) || event.isKey(InputEvent::Key::Down)) && count > 0) this->hovered = (this->hovered + 1) % count;
	else if (event.isKey(InputEvent::Key::Enter) && this->hovered >= 0 && this->hovered < count) this->activate(this->buttons[static_cast<size_t>(this->hovered)].second);
	else if (event.isHotkey('C') && this->canContinue) this->activate(Action::Continue);
	else if (event.isHotkey('L') || event.isHotkey('J')) this->activate(Action::Load);
	else if (event.isHotkey('N')) this->activate(Action::NewGame);
	else if (event.isHotkey('S') || event.isKey(InputEvent::Key::Escape)) this->activate(Action::Exit);
}

void arena::MainMenuScreen::render(Canvas &canvas)
{
	canvas.clear(0);
}

void arena::MainMenuScreen::renderWorld(std::vector<uint32_t> &pixels, int width, int height)
{
	this->frameW = width;
	this->frameH = height;
	pixels.assign(static_cast<size_t>(width) * height, 0xFF000000u);
	hud::Frame frame{ pixels, width, height };
	kit::Painter painter(frame, this->game.getFonts());
	const int u = painter.unit();
	const Fonts &fonts = this->game.getFonts();
	const Localization &text = this->game.getText();
	namespace col = kit::color;

	// Arte de abertura cobrindo a tela (proporção 320x200 corrigida para 4:3 do VGA) e escurecida embaixo.
	this->art.draw(painter, width, height);
	for (int y = height / 2; y < height; y++)
	{
		const uint8_t a = static_cast<uint8_t>(std::min(220, (y - height / 2) * 440 / height));
		hud::fillRect(frame, 0, y, width, 1, hud::rgb(0, 0, 0, a));
	}

	// Faixa de madeira embaixo, com os botões lado a lado (a arte do título fica livre).
	std::vector<std::pair<std::string, Action>> entries;
	if (this->canContinue) entries.push_back({ text.get("MainMenu.Continue"), Action::Continue });
	entries.push_back({ text.get("MainMenu.LoadGame"), Action::Load });
	entries.push_back({ text.get("MainMenu.NewGame"), Action::NewGame });
	entries.push_back({ text.get("MainMenu.Exit"), Action::Exit });
	const int count = static_cast<int>(entries.size());
	const int gap = 8 * u, bh = 22 * u;
	const int bw = std::min(120 * u, (width - 16 * u - (count - 1) * gap) / count);
	const Rect strip{ -2 * u, height - 58 * u, width + 4 * u, 58 * u + 2 * u };
	painter.woodPanel(strip, 235);
	this->buttons.clear();
	int x = width / 2 - (count * bw + (count - 1) * gap) / 2;
	for (int i = 0; i < count; i++)
	{
		const Rect r{ x, strip.y + 10 * u, bw, bh };
		painter.button(r, entries[static_cast<size_t>(i)].first, i == this->hovered, false);
		this->buttons.push_back({ r, entries[static_cast<size_t>(i)].second });
		if (entries[static_cast<size_t>(i)].second == Action::Continue)
		{
			painter.text(fonts.arena, this->continueLabel, r.x + r.w / 2, r.y + r.h + 5 * u, col::TextDim, TextAlign::Center);
		}
		x += bw + gap;
	}

	if (!this->notice.empty()) painter.text(fonts.arena, this->notice, width / 2, strip.y - 16 * u, col::Danger, TextAlign::Center);
	painter.text(fonts.arena, text.get("MainMenu.Footer"), width - 8 * u, height - 12 * u, col::TextDim, TextAlign::Right);
	if (this->loadOpen) this->loadMenu->render(painter, this->game);
}

const arena::Palette &arena::MainMenuScreen::getPalette() const
{
	return this->game.getDefaultPalette();
}
