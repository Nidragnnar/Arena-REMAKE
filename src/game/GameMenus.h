#pragma once

#include <array>
#include <string>
#include <vector>

#include "../render3d/UiKit.h"
#include "InventoryUi.h"
#include "SpellbookUi.h"

namespace arena
{
	// Estado do jogador mostrado nas janelas (vem da tela do mundo).
	struct MenuState
	{
		int health = 0;
		int spellPoints = 0;
		int experience = 0;
		std::vector<std::string> effects; // efeitos mágicos ativos, já descritos ("Luz - 40 s")
	};

	// Janela do personagem em resolução nativa: boneco e barras à esquerda e três abas
	// (Personagem, Inventário, Feitiços). Clique seleciona; clique de novo (ou Enter) usa.
	class CharacterMenu
	{
	public:
		enum class Tab { Character = 0, Inventory = 1, Spells = 2 };

		void open(Game &game, Tab tab);
		Tab getTab() const { return this->tab; }
		OverlayResult handleInput(const InputEvent &event, Game &game, const MenuState &state);
		void render(kit::Painter &painter, const Game &game, const MenuState &state);

	private:
		void refreshDoll(Game &game);
		void layout(const kit::Painter &painter);
		OverlayResult activate(Game &game, const MenuState &state);
		OverlayResult dropSelected(Game &game);
		int rowCount(const Game &game) const;
		int rowAt(int x, int y) const;
		void select(int row, const Game &game);

		void renderCharacter(kit::Painter &painter, const Game &game, const MenuState &state);
		void renderInventory(kit::Painter &painter, const Game &game);
		void renderSpells(kit::Painter &painter, const Game &game, const MenuState &state);

		Tab tab = Tab::Character;
		PaperDoll doll;
		std::vector<uint32_t> dollPixels;
		int dollW = 0, dollH = 0;
		int selected = -1;
		int scroll = 0;
		std::string notice;

		// Layout do último quadro (para o mouse).
		int frameW = 1, frameH = 1, unitPx = 1;
		int mouseX = -1, mouseY = -1;
		Rect window, closeRect, content, listRect, primaryRect, secondaryRect;
		std::array<Rect, 3> tabRects{};
		int rowH = 1, visibleRows = 1;
		bool primaryEnabled = false, secondaryEnabled = false;
	};

	// Janela de saque: ouro e itens de baús, pilhas e corpos.
	class LootMenu
	{
	public:
		void open(Loot *source, std::string title);
		bool isEmpty() const { return this->loot == nullptr || (this->loot->gold == 0 && this->loot->items.empty()); }
		OverlayResult handleInput(const InputEvent &event, Game &game);
		void render(kit::Painter &painter, const Game &game);

	private:
		int rowAt(int x, int y) const;
		OverlayResult takeAll(Game &game);

		Loot *loot = nullptr;
		std::string title;
		std::string notice;
		int frameW = 1, frameH = 1, unitPx = 1;
		int mouseX = -1, mouseY = -1;
		Rect window, listRect, takeRect, takeAllRect, closeRect;
		int rowH = 1, visibleRows = 1;
	};

	// Subida de nível: vida ganha e pontos de bônus para distribuir entre os atributos (obrigatório).
	class LevelUpMenu
	{
	public:
		void open(int healthGained);
		OverlayResult handleInput(const InputEvent &event, Game &game);
		void render(kit::Painter &painter, const Game &game);

	private:
		void change(Game &game, int attribute, int delta);

		int healthGained = 0;
		int selected = 0;
		int frameW = 1, frameH = 1;
		int mouseX = -1, mouseY = -1;
		Rect window, doneRect;
		std::array<Rect, 8> minusRects{}, plusRects{};
	};

	// Salvar / carregar em 10 espaços (pausa e menu principal).
	class SaveLoadMenu
	{
	public:
		enum class Mode { Save, Load };
		struct Choice { bool close = false; int slot = -1; bool original = false; }; // original: save do Arena original

		void open(const Game &game, Mode mode);
		Mode getMode() const { return this->mode; }
		Choice handleInput(const InputEvent &event, const Game &game);
		void render(kit::Painter &painter, const Game &game);

	private:
		Choice choose(int slot);

		Mode mode = Mode::Load;
		std::array<Game::SaveSummary, Game::SaveSlots> slots{};
		// Carregar: alterna entre os saves do remake e os do Arena original (pasta do jogo).
		bool original = false;
		std::array<Game::SaveSummary, Game::SaveSlots> remakeSlots{}, originalSlots{};
		Rect sourceRect;
		int selected = 0;
		int confirm = -1; // espaço ocupado aguardando o segundo clique para sobrescrever
		int frameW = 1, frameH = 1;
		int mouseX = -1, mouseY = -1;
		Rect window, listRect, closeRect;
		int rowH = 1;
	};

	// Converte a posição do evento (fração da tela) para pixels do quadro.
	inline void eventToPixels(const InputEvent &event, int frameW, int frameH, int &x, int &y)
	{
		if (event.viewX < 0.0f) return;
		x = static_cast<int>(event.viewX * frameW);
		y = static_cast<int>(event.viewY * frameH);
	}
}
