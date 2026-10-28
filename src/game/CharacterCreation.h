#pragma once

#include <array>
#include <string>
#include <vector>

#include "Dialog.h"
#include "Game.h"

namespace arena
{
	// Base das telas que usam o quarto da criação (STARTGAM.MNU) como fundo.
	class CreationRoomScreen : public Screen
	{
	public:
		explicit CreationRoomScreen(Game &game);
		const Palette &getPalette() const override { return this->palette; }

	protected:
		void drawRoom(Canvas &canvas) const { canvas.blit(this->background, 0, 0, false); }
		// Botão de pergaminho centrado em (cx, cy), como no original (180x40).
		void drawParchmentButton(Canvas &canvas, const std::string &label, int cx, int cy, bool hovered, int w = 180, int h = 40) const;
		static Rect buttonRect(int cx, int cy, int w = 180, int h = 40) { return Rect{ cx - w / 2, cy - h / 2, w, h }; }

		IndexedImage background;
		Palette palette{};
		Dialog dialog;
	};

	// "Como desejas escolher tua classe?" — Gerar (perguntas) ou Escolher (lista).
	class ChooseClassCreationScreen : public CreationRoomScreen
	{
	public:
		explicit ChooseClassCreationScreen(Game &game) : CreationRoomScreen(game) { }
		void handleInput(const InputEvent &event) override;
		void render(Canvas &canvas) override;

	private:
		int hovered = -1;
	};

	// As 10 perguntas que determinam a classe.
	class ClassQuestionsScreen : public CreationRoomScreen
	{
	public:
		explicit ClassQuestionsScreen(Game &game);
		void handleInput(const InputEvent &event) override;
		void render(Canvas &canvas) override;

	private:
		static constexpr int QuestionCount = 10;

		void layoutQuestion();
		void answer(int choice);

		std::vector<int> questionOrder;
		int current = 0;
		std::array<int, 3> categoryCounts{}; // mago, ladrão, guerreiro
		int suggestedClass = -1;

		// Layout do texto da pergunta atual (em coordenadas do conteúdo, antes da rolagem).
		std::string questionText;
		std::array<std::string, 3> choiceTexts;
		std::array<int, 3> choiceTop{};
		std::array<int, 3> choiceBottom{};
		int contentHeight = 0;
		int scroll = 0;
		int hovered = -1;
	};

	// Lista das 18 classes com descrição.
	class ChooseClassScreen : public CreationRoomScreen
	{
	public:
		explicit ChooseClassScreen(Game &game);
		void handleInput(const InputEvent &event) override;
		void render(Canvas &canvas) override;

	private:
		static constexpr int VisibleRows = 6;
		int rowAt(int x, int y) const;
		std::string describe(int classIndex) const;

		IndexedImage popup;
		std::vector<int> order; // classes ordenadas pelo nome traduzido
		int scroll = 0;
		int hovered = -1;
	};

	class ChooseNameScreen : public CreationRoomScreen
	{
	public:
		explicit ChooseNameScreen(Game &game);
		void handleInput(const InputEvent &event) override;
		void update(double dt) override { this->blink += dt; }
		void render(Canvas &canvas) override;

	private:
		std::string name;
		double blink = 0.0;
	};

	class ChooseGenderScreen : public CreationRoomScreen
	{
	public:
		explicit ChooseGenderScreen(Game &game) : CreationRoomScreen(game) { }
		void handleInput(const InputEvent &event) override;
		void render(Canvas &canvas) override;

	private:
		void choose(bool male);
		int hovered = -1;
	};

	// Escolha da raça clicando na província do mapa de Tamriel.
	class ChooseRaceScreen : public Screen
	{
	public:
		explicit ChooseRaceScreen(Game &game);
		void handleInput(const InputEvent &event) override;
		void render(Canvas &canvas) override;
		const Palette &getPalette() const override { return this->palette; }

	private:
		enum class Stage { Intro, Map, Confirm, Confirmed1, Confirmed2, Confirmed3, Confirmed4 };
		void openStage(Stage stage);

		IndexedImage map;
		IndexedImage noExit;
		Palette palette{};
		Dialog dialog;
		Stage stage = Stage::Intro;
		int hoveredProvince = -1;
		int selectedProvince = -1;
	};

	// Ficha de atributos, distribuição de bônus e escolha do rosto.
	class ChooseAttributesScreen : public Screen
	{
	public:
		explicit ChooseAttributesScreen(Game &game);
		void handleInput(const InputEvent &event) override;
		void update(double) override { }
		void render(Canvas &canvas) override;
		const Palette &getPalette() const override { return this->palette; }

	private:
		enum class Stage { Intro, Editing, SaveReroll, Appearance, Portrait, Finished };

		void rollStats();
		void recalculate();
		void changeAttribute(int index, int delta);
		void finish();

		Palette palette{};
		IndexedImage sheet;
		IndexedImage body;
		IndexedImage shirt;
		IndexedImage pants;
		IndexedImage upDown;
		IndexedImage bonusImage;
		std::vector<IndexedImage> heads;
		std::array<int, 2> shirtOffset{};
		std::array<int, 2> pantsOffset{};

		Dialog dialog;
		Stage stage = Stage::Intro;
		int selected = 0;
	};
}
