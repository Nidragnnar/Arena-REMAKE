#pragma once

#include <cstdint>

namespace arena
{
	// Gerador aleatório do Arena: semente * 7143469 a cada sorteio, devolvendo os bits 16..31.
	class ArenaRandom
	{
	public:
		explicit ArenaRandom(uint32_t seed) : value(seed) {}
		int next() { this->value *= 7143469u; return static_cast<int>((this->value >> 16) & 0xFFFF); }
		uint32_t state() const { return this->value; }

	private:
		uint32_t value;
	};
}
