#pragma once

#include <string>
#include <vector>

#include "../render3d/UiKit.h"
#include "InventoryUi.h"

namespace arena
{
	// Conversa com um morador, como no original: "Quem és tu?", "Onde fica...?", "Boatos" e sair.
	// As respostas vêm prontas de quem abre a janela (o mundo sabe onde ficam os prédios).
	class DialogueMenu
	{
	public:
		struct Place { std::string label, answer; };

		void open(const std::string &name, const std::string &greeting, const std::string &whoAnswer, std::vector<Place> places, std::vector<std::string> rumors);
		OverlayResult handleInput(const InputEvent &event, Game &game);
		void render(kit::Painter &painter, const Game &game);

		// O primeiro boato é a pista da missão principal; true depois que o jogador o ouviu.
		bool firstRumorIsQuest = false;
		bool questRumorHeard = false;

	private:
		enum class Page { Main, WhereIs };
		struct Option { std::string label; int action; };

		std::vector<Option> options(const Game &game) const;
		bool choose(int action, Game &game);

		std::string name, text, whoAnswer;
		std::vector<Place> places;
		std::vector<std::string> rumors;
		size_t nextRumor = 0;
		Page page = Page::Main;
		int frameW = 1, frameH = 1;
		int mouseX = -1, mouseY = -1;
		Rect window;
		std::vector<std::pair<Rect, int>> buttons;
	};
}
