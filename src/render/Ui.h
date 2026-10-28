#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "Canvas.h"
#include "ImageOps.h"

namespace arena::ui
{
	int textWidth(const BitmapFont &font, std::string_view utf8);

	// Minúsculas sem acentos, para ordenar nomes em português.
	std::string sortKey(std::string_view utf8);

	// Largura da linha mais larga e altura total de um texto com '\n'.
	void measureBlock(const BitmapFont &font, std::string_view utf8, int lineSpacing, int &outWidth, int &outHeight);

	// Quebra o texto em linhas de até maxWidth pixels, preservando '\n' existentes.
	std::string wrapText(const BitmapFont &font, std::string_view utf8, int maxWidth);

	// Substitui cada "%s" em ordem pelos argumentos.
	std::string format(std::string text, std::initializer_list<std::string> args);

	// Caixa de pergaminho montada com as 9 peças do PARCH.CIF (cantos, bordas, miolo),
	// como as janelas da criação de personagem original. Índice 0 = transparente.
	IndexedImage makeParchment(const std::vector<IndexedImage> &tiles, int width, int height);

	// Caixa escura com borda chanfrada (janelas sobre a ficha de personagem).
	void drawDarkBox(Canvas &canvas, const Palette &palette, const Rect &rect);
}
