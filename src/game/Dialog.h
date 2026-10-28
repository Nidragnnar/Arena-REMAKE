#pragma once

#include <string>
#include <vector>

#include "../render/Ui.h"
#include "Game.h"

namespace arena
{
	// Janela modal no estilo do Arena: texto + opções opcionais (ex.: Sim/Não).
	// Sem opções, qualquer clique/tecla fecha.
	class Dialog
	{
	public:
		enum class Style { Parchment, Dark };

		static constexpr int None = -2;      // evento não decidiu nada
		static constexpr int Dismissed = -1; // diálogo sem opções foi fechado

		void open(std::string text, const BitmapFont &font, Style style, std::vector<std::string> options = {}, int centerY = Canvas::Height / 2);
		void close() { this->visible = false; }
		bool isOpen() const { return this->visible; }

		// Retorna None, Dismissed ou o índice da opção escolhida (fecha o diálogo).
		int handleInput(const InputEvent &event);
		void render(Canvas &canvas, Game &game, const Palette &palette) const;

	private:
		void layout();

		bool visible = false;
		std::string text;
		const BitmapFont *font = nullptr;
		Style style = Style::Parchment;
		std::vector<std::string> options;
		int centerY = Canvas::Height / 2;
		int hovered = -1;

		Rect titleRect;
		std::vector<Rect> optionRects;
	};
}
