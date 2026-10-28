#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace arena::compression
{
	// LZSS com janela de 4 KB (texturas .IMG/.CIF "tipo 4").
	void decodeType04(const uint8_t *src, const uint8_t *srcEnd, std::span<uint8_t> out);

	// LZHUF (Okumura: LZSS + Huffman adaptativo) — .IMG/.CIF "tipo 8" e voxels dos .MIF.
	void decodeType08(const uint8_t *src, const uint8_t *srcEnd, std::span<uint8_t> out);

	// RLE simples por bytes.
	void decodeRLE(const uint8_t *src, const uint8_t *srcEnd, std::span<uint8_t> out);
}
