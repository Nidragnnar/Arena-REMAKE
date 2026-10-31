#pragma once

#include <string>
#include <vector>

#include "../render3d/UiKit.h"
#include "InventoryUi.h"

namespace arena
{
	// Atendimento nos prédios da cidade: loja (comprar e vender), taverna (bebidas e quartos),
	// templo (cura) e guilda dos magos (feitiços e poções). Os preços vêm do executável e
	// mudam com a qualidade do lugar (cidade, vila ou aldeia).
	class ServiceMenu
	{
	public:
		enum class Kind { Equipment, Tavern, Temple, MagesGuild };

		// stockSeed sorteia o estoque da loja (o mesmo a cada visita); priceFactor em %.
		void open(const Game &game, Kind kind, const std::string &title, uint32_t stockSeed, int priceFactor, int health, int spellPoints);
		OverlayResult handleInput(const InputEvent &event, Game &game);
		void render(kit::Painter &painter, const Game &game);

	private:
		struct Row
		{
			std::string label, detail;
			int price = 0;
			int action = 0, index = 0; // o que fazer ao confirmar
			bool enabled = true;
		};

		void rebuild(const Game &game);
		int tabCount() const { return this->kind == Kind::Temple ? 1 : this->kind == Kind::MagesGuild ? 4 : this->kind == Kind::Tavern ? 3 : 2; }

		// Criação de feitiços (aba 3 da Guilda dos Magos): um efeito, elemento, alvo, potência e nome.
		bool makerOpen() const { return this->kind == Kind::MagesGuild && this->tab == 2; }
		Spell makerSpell(const Game &game) const;
		OverlayResult createSpell(Game &game);
		void renderMaker(kit::Painter &painter, const Game &game, const Rect &page);
		int makerEffect = 0, makerElement = 0, makerTarget = 1, makerPotency = 3;
		std::string makerName;
		std::vector<std::pair<Rect, int>> makerButtons;
		OverlayResult confirm(Game &game, const Row &row);
		int price(int base) const { return std::max(1, base * this->priceFactor / 100); }

		Kind kind = Kind::Equipment;
		std::string title;
		int tab = 0;
		int priceFactor = 100;
		int health = 0, spellPoints = 0;
		std::vector<Item> stock;
		std::vector<Row> rows;
		int selected = -1, scroll = 0;
		std::string notice;
		bool noticeGood = false;
		int frameW = 1, frameH = 1;
		int mouseX = -1, mouseY = -1;
		Rect window, listRect, closeRect, actionRect;
		std::vector<Rect> tabRects;
		int rowH = 1, visibleRows = 1;
	};
}
