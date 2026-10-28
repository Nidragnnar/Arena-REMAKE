#pragma once

#include <string>
#include <vector>

#include "../render/ImageOps.h"
#include "Game.h"
#include "GameMenus.h"

namespace arena
{
	// Caixa de mensagem simples sobre a tela atual (fecha com clique ou tecla).
	struct MessagePopup
	{
		std::string text;
		bool visible = false;

		void show(std::string message) { this->text = std::move(message); this->visible = true; }
		void render(Canvas &canvas, const Palette &palette, const BitmapFont &font) const;
	};

	// Botão de texto clicável desenhado sobre a arte original.
	struct TextButton
	{
		std::string label;
		Rect bounds;
		char hotkey = 0;
	};

	// Arte de abertura (TITLE.IMG ou TITLE_IMG.png redesenhado) em RGBA.
	struct TitleArt
	{
		std::vector<uint32_t> pixels;
		int width = 0, height = 0;

		void load(const Game &game);
		// Cobre a altura da tela na proporção 4:3 do VGA, centralizada.
		void draw(kit::Painter &painter, int frameW, int frameH) const;
	};

	class TitleScreen : public Screen
	{
	public:
		explicit TitleScreen(Game &game);

		void handleInput(const InputEvent &event) override;
		void render(Canvas &canvas) override;
		const Palette &getPalette() const override;
		bool usesWorldView() const override { return true; }
		bool nativeResolution() const override { return true; }
		void renderWorld(std::vector<uint32_t> &pixels, int width, int height) override;

	private:
		TitleArt art;
	};

	// Menu principal em resolução nativa: arte de abertura ao fundo e painel com Continuar,
	// Carregar jogo, Novo jogo e Sair (carregar abre a lista de 10 espaços).
	class MainMenuScreen : public Screen
	{
	public:
		explicit MainMenuScreen(Game &game);

		void handleInput(const InputEvent &event) override;
		void render(Canvas &canvas) override;
		const Palette &getPalette() const override;
		bool usesWorldView() const override { return true; }
		bool nativeResolution() const override { return true; }
		void renderWorld(std::vector<uint32_t> &pixels, int width, int height) override;

	private:
		enum class Action { Continue, Load, NewGame, Exit };
		void activate(Action action);

		TitleArt art;
		std::vector<std::pair<Rect, Action>> buttons;
		int hovered = -1;
		int frameW = 1, frameH = 1;
		int mouseX = -1, mouseY = -1;
		bool canContinue = false;
		std::string continueLabel;
		std::string notice;
		bool loadOpen = false;
		std::unique_ptr<SaveLoadMenu> loadMenu;
	};
}
