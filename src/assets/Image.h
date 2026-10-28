#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "../core/Bytes.h"

namespace arena
{
	struct Color
	{
		uint8_t r = 0, g = 0, b = 0, a = 255;
	};

	using Palette = std::array<Color, 256>;

	// Imagem indexada de 8 bits (formato nativo do Arena).
	struct IndexedImage
	{
		int width = 0;
		int height = 0;
		int offsetX = 0;
		int offsetY = 0;
		std::vector<uint8_t> pixels;
		std::optional<Palette> palette; // presente se o .IMG tiver paleta embutida

		uint8_t at(int x, int y) const { return this->pixels[static_cast<size_t>(x + y * this->width)]; }
	};

	// .COL: 8 bytes de cabeçalho (tamanho 776, versão 0xB123) + 256 cores RGB de 8 bits.
	std::optional<Palette> loadColPalette(const ByteBuffer &data);

	// .IMG: cabeçalho de 12 bytes (offsets, dimensões, flags, tamanho), pixels
	// possivelmente comprimidos (tipo 4/8) e paleta VGA opcional de 6 bits.
	std::optional<IndexedImage> loadImg(const std::string &name, const ByteBuffer &data);

	// .CIF: sequência de imagens (rostos, ícones, peças de moldura). Cada quadro
	// tem o mesmo cabeçalho de 12 bytes do .IMG; alguns arquivos são brutos.
	std::vector<IndexedImage> loadCif(const std::string &name, const ByteBuffer &data);

	// .SET: várias texturas 64x64 brutas concatenadas.
	std::vector<IndexedImage> loadSet(const ByteBuffer &data);

	// .DFA: animação; primeiro quadro em RLE e os demais como atualizações parciais.
	std::vector<IndexedImage> loadDfa(const ByteBuffer &data);

	// .CFA: animação de criatura/personagem. Todos os quadros num bloco RLE, com
	// pixels empacotados em 1..8 bits e convertidos por uma tabela de cores.
	std::vector<IndexedImage> loadCfa(const ByteBuffer &data);
}
