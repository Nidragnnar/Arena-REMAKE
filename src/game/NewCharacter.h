#pragma once

#include <array>
#include <string>
#include <vector>

#include "GameMenus.h"

namespace arena
{
	// Criação de personagem em resolução nativa, como um assistente em passos, seguindo as
	// regras do original: classe por 10 perguntas (entre as 40 do QUESTION.TXT) ou pela lista,
	// nome e gênero, província no mapa de Tamriel, atributos (base da raça + rolagem + bônus)
	// e rosto. Termina com o equipamento e o livro iniciais e entra na Prisão Imperial.
	class NewCharacterScreen : public Screen
	{
	public:
		explicit NewCharacterScreen(Game &game);

		void handleInput(const InputEvent &event) override;
		void render(Canvas &canvas) override { canvas.clear(0); }
		const Palette &getPalette() const override { return this->game.getDefaultPalette(); }
		bool usesWorldView() const override { return true; }
		bool nativeResolution() const override { return true; }
		void renderWorld(std::vector<uint32_t> &pixels, int width, int height) override;

	private:
		enum class Step { Method, Questions, ClassList, Identity, Province, Attributes, Portrait, Summary };

		struct Image { std::vector<uint32_t> pixels; int width = 0, height = 0; };
		struct Button { Rect rect; int id; bool enabled; };

		void goTo(Step step);
		bool canAdvance() const;
		void advance();
		void back();
		void answer(int choice);
		void rollAttributes();
		void changeAttribute(int index, int delta);
		void loadAppearance();
		void finish();
		// Original pela paleta, ou o PNG redesenhado de source (quadro frame de frameCount).
		Image convert(const IndexedImage &image, const Palette &palette, const std::string &source, int frame = 0, int frameCount = 1) const;

		// Desenho de cada passo dentro do pergaminho.
		void drawMethod(kit::Painter &p, const Rect &area);
		void drawQuestions(kit::Painter &p, const Rect &area);
		void drawClassList(kit::Painter &p, const Rect &area);
		void drawIdentity(kit::Painter &p, const Rect &area);
		void drawProvince(kit::Painter &p, const Rect &area);
		void drawAttributes(kit::Painter &p, const Rect &area);
		void drawPortrait(kit::Painter &p, const Rect &area);
		void drawSummary(kit::Painter &p, const Rect &area);
		void addButton(kit::Painter &p, const Rect &r, const std::string &label, int id, bool enabled = true, bool active = false);

		Step step = Step::Method;
		std::vector<Button> buttons;
		int frameW = 1, frameH = 1, unitPx = 1;
		int mouseX = -1, mouseY = -1;
		std::string notice;

		// Perguntas
		std::vector<int> questionOrder;
		int currentQuestion = 0;
		std::array<int, 3> categoryCounts{};
		int suggestedClass = -1;

		// Lista de classes, província e mapa
		std::vector<int> classOrder;
		Image map;
		Rect mapRect;
		int hoveredProvince = -1;

		// Aparência
		std::vector<Image> faces;
		PaperDoll doll;
		Image dollImage;
	};
}
