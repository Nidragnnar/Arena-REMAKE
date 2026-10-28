#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "../game/Game.h"
#include "../render/ImageOps.h"
#include "Hud.h"

namespace arena::kit
{
	// Paleta da interface: madeira escura, ferro, dourado e pergaminho.
	namespace color
	{
		constexpr uint32_t WoodDark = hud::rgb(30, 21, 14);
		constexpr uint32_t Wood = hud::rgb(54, 37, 24);
		constexpr uint32_t WoodLight = hud::rgb(84, 59, 38);
		constexpr uint32_t Gold = hud::rgb(201, 158, 82);
		constexpr uint32_t GoldBright = hud::rgb(246, 210, 128);
		constexpr uint32_t GoldDim = hud::rgb(128, 96, 50);
		constexpr uint32_t Ink = hud::rgb(58, 36, 18);
		constexpr uint32_t InkSoft = hud::rgb(104, 72, 40);
		constexpr uint32_t Text = hud::rgb(236, 224, 198);
		constexpr uint32_t TextDim = hud::rgb(168, 154, 128);
		constexpr uint32_t Health = hud::rgb(168, 34, 28);
		constexpr uint32_t Stamina = hud::rgb(52, 128, 50);
		constexpr uint32_t Magic = hud::rgb(52, 84, 186);
		constexpr uint32_t Danger = hud::rgb(214, 74, 52);
	}

	// Pintor da interface em resolução nativa sobre o quadro RGBA (HUD e janelas do jogo).
	// "u" é a unidade de escala (pixels por pixel das fontes do Arena): 2 em 720p, 3 em 1080p.
	class Painter
	{
	public:
		Painter(hud::Frame &frame, const Fonts &fonts);

		int unit() const { return this->scaleUnit; }
		hud::Frame &frame() { return this->target; }
		const Fonts &fonts() const { return this->fontSet; }

		// Moldura de madeira com filete dourado e cravos nos cantos; o miolo é madeira ou pergaminho.
		void woodPanel(const Rect &r, uint8_t alpha = 255);
		void parchmentPanel(const Rect &r);
		// Área afundada (listas, campos), escura ou em pergaminho mais escuro.
		void inset(const Rect &r, bool onParchment);
		void divider(int x, int y, int w, uint32_t tint = color::GoldDim);

		// Texto com as fontes do Arena ampliadas por "u" (scaleMul multiplica a escala).
		void text(const BitmapFont &font, std::string_view s, int x, int y, uint32_t tint, TextAlign align = TextAlign::Left,
			bool shadow = true, int scaleMul = 1);
		int textWidth(const BitmapFont &font, std::string_view s, int scaleMul = 1) const;
		int lineHeight(const BitmapFont &font, int scaleMul = 1) const;
		// Texto quebrado em linhas dentro da largura; devolve a altura usada.
		int paragraph(const BitmapFont &font, std::string_view s, const Rect &area, uint32_t tint, bool shadow = false);

		// Botão em relevo; destacado sob o mouse e afundado quando ativo (aba atual, item selecionado).
		void button(const Rect &r, std::string_view label, bool hovered, bool active, bool enabled = true);
		// Barra de recurso emoldurada com o valor escrito por cima.
		void bar(const Rect &r, int value, int maxValue, uint32_t fill, std::string_view label);
		// Faixa de seleção/destaque de uma linha de lista.
		void rowHighlight(const Rect &r, bool selected, bool hovered, bool onParchment);
		// Imagem RGBA (0xAABBGGRR) escalada para o retângulo (vizinho mais próximo).
		void image(const std::vector<uint32_t> &rgba, int width, int height, const Rect &dst);

		void fill(const Rect &r, uint32_t tint) { hud::fillRect(this->target, r.x, r.y, r.w, r.h, tint); }

	private:
		void border(const Rect &r, int thickness, uint32_t tint);

		hud::Frame &target;
		const Fonts &fontSet;
		int scaleUnit = 1;
	};

	// Ponto do mouse dentro de um retângulo (coordenadas do quadro).
	inline bool hit(const Rect &r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
}
