#pragma once

#include <string>
#include <vector>

#include "../render3d/UiKit.h"
#include "ArenaData.h"
#include "InventoryUi.h"

namespace arena
{
	// Viagem pelo mapa de Tamriel (tecla T): escolhe a província no mapa e o destino na lista,
	// com os dias de viagem calculados como no original. Os locais são marcados no mapa.
	class TravelMenu
	{
	public:
		struct Choice { bool close = false; const CityLocation *destination = nullptr; int days = 0; };

		void open(const Game &game, const CityLocation *current, int month);
		Choice handleInput(const InputEvent &event, const Game &game);
		void render(kit::Painter &painter, const Game &game);

	private:
		void selectProvince(const Game &game, int province);

		std::vector<uint32_t> map;
		int mapW = 0, mapH = 0;
		const CityLocation *current = nullptr;
		int month = 1;
		int province = -1, hoveredProvince = -1;
		std::vector<std::pair<const CityLocation *, int>> places; // destino e dias
		int selected = -1, confirm = -1, scroll = 0;
		// Pontos do mapa (Ã¡rea clicÃ¡vel de cada local da lista) e o que estÃ¡ sob o mouse.
		std::vector<std::pair<Rect, int>> dots;
		int hoveredPlace = -1;
		int placeUnderMouse() const;
		int frameW = 1, frameH = 1;
		int mouseX = -1, mouseY = -1;
		Rect window, mapRect, listRect, closeRect, goRect;
		int rowH = 1, visibleRows = 1;
	};
}
