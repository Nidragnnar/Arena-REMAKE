#pragma once

#include <string>
#include <vector>

#include "../core/Bytes.h"

namespace arena
{
	// Animação FLIC (.FLC/.CEL do Arena: cutscenes). Decodifica um quadro por vez, em ordem,
	// porque os quadros seguintes são diferenças do anterior; a cena mais longa (WALK) tem 390.
	class FlcPlayer
	{
	public:
		bool open(ByteBuffer data, std::string &outError);
		bool nextFrame(); // decodifica o próximo quadro; false depois do último
		void rewind();

		int width() const { return this->w; }
		int height() const { return this->h; }
		int frameCount() const { return this->count; }
		int frameIndex() const { return this->decoded - 1; }
		int frameDelayMs() const { return this->delayMs; }
		const std::vector<uint32_t> &rgba() const { return this->pixels; } // 0xAABBGGRR

	private:
		void decodeChunk(size_t offset, uint16_t type);

		ByteBuffer data;
		int w = 0, h = 0, count = 0, delayMs = 70, decoded = 0;
		size_t cursor = 128;
		std::vector<uint8_t> indices;
		std::vector<uint8_t> palette = std::vector<uint8_t>(768, 0);
		std::vector<uint32_t> pixels;
	};
}
