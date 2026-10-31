#pragma once

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "../assets/Flc.h"
#include "../render3d/UiKit.h"
#include "Game.h"

namespace arena
{
	// Cutscenes do original (.FLC) tocadas em sequência, em tela cheia. PNGs em data/mods com os
	// nomes da exportação (INTRO_000.png, INTRO_001.png, ...) substituem os quadros: com até o
	// número original de quadros, trocam um por um no tempo original; com mais quadros, a cena
	// passa a ser só os PNGs, na mesma duração total (ex.: o dobro de quadros = o dobro de fluidez).
	class Cutscene
	{
	public:
		void start(Game &game, std::vector<std::string> files);
		bool active() const { return this->playing; }
		void update(double dt);
		void skip();          // pula a cena atual
		void stop() { this->queue.clear(); this->playing = false; }
		void draw(kit::Painter &painter, int width, int height) const;

	private:
		bool openNext();
		void showFrame(int index);

		Game *game = nullptr;
		std::deque<std::string> queue;
		std::string name; // "INTRO"
		FlcPlayer player;
		bool playing = false;
		int frames = 0, frame = -1;
		double frameSeconds = 0.1, timer = 0.0;
		bool pngOnly = false;
		std::vector<uint32_t> image;
		int imageW = 0, imageH = 0;
		bool imageFromPng = false;
	};

	// Tela só de cutscene (abertura, novo jogo); ao terminar ou ser pulada, segue para `next`.
	class CutsceneScreen : public Screen
	{
	public:
		using NextScreen = std::unique_ptr<Screen> (*)(Game &game);
		CutsceneScreen(Game &game, std::vector<std::string> files, NextScreen next);

		void handleInput(const InputEvent &event) override;
		void update(double dt) override;
		void render(Canvas &canvas) override;
		const Palette &getPalette() const override { return this->game.getDefaultPalette(); }
		bool usesWorldView() const override { return true; }
		bool nativeResolution() const override { return true; }
		void renderWorld(std::vector<uint32_t> &pixels, int width, int height) override;

	private:
		Cutscene cutscene;
		NextScreen next;
		bool finished = false;
	};
}
