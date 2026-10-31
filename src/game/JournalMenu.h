#pragma once

#include <string>
#include <vector>

#include "../render3d/UiKit.h"
#include "InventoryUi.h"

namespace arena
{
	// Diário (tecla J): missão atual, progresso do Cajado e o registro do que aconteceu, do mais
	// recente ao mais antigo. Linhas com texto completo (sonhos, achados) abrem para reler.
	class JournalMenu
	{
	public:
		void open(std::string objective);
		OverlayResult handleInput(const InputEvent &event, Game &game);
		void render(kit::Painter &painter, const Game &game);

	private:
		std::string objective;
		int scroll = 0;     // primeira linha do registro mostrada
		int reading = -1;   // linha aberta para reler (-1 = lista)
		int frameW = 1, frameH = 1;
		int mouseX = -1, mouseY = -1;
		Rect window, closeButton, backButton;
		std::vector<std::pair<Rect, int>> rows;
		bool moreBelow = false;
	};
}
