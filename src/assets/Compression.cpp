// Algoritmos portados do OpenTESArena (MIT), Compression.h.

#include <algorithm>
#include <array>
#include <numeric>

#include "Compression.h"

void arena::compression::decodeType04(const uint8_t *src, const uint8_t *srcEnd, std::span<uint8_t> out)
{
	std::array<uint8_t, 4096> history;
	history.fill(0x20);
	int historyPos = 0;
	size_t dst = 0;

	int bitCount = 0;
	int mask = 0;
	while (src != srcEnd && dst < out.size())
	{
		if (bitCount == 0)
		{
			bitCount = 8;
			mask = *(src++);
		}
		else
		{
			mask >>= 1;
		}

		if ((mask & 1) != 0)
		{
			if (src == srcEnd)
			{
				break;
			}

			history[historyPos++ & 0x0FFF] = *src;
			out[dst++] = *(src++);
		}
		else
		{
			if (srcEnd - src < 2)
			{
				break;
			}

			const uint8_t byte1 = *(src++);
			const uint8_t byte2 = *(src++);
			const int toCopy = (byte2 & 0x0F) + 3;
			int copyPos = (((byte2 & 0xF0) << 4) | byte1) + 18;
			for (int i = 0; i < toCopy && dst < out.size(); i++)
			{
				const uint8_t value = history[copyPos++ & 0x0FFF];
				history[historyPos++ & 0x0FFF] = value;
				out[dst++] = value;
			}
		}

		bitCount--;
	}

	std::fill(out.begin() + dst, out.end(), uint8_t(0));
}

namespace
{
	constexpr int CharCount = 314; // 256 literais + 58 comprimentos
	constexpr int TableSize = CharCount * 2 - 1; // 627
	constexpr int Root = TableSize - 1;

	constexpr std::array<uint8_t, 256> HighOffsetBits = []()
	{
		std::array<uint8_t, 256> table{};
		int i = 0;
		auto fill = [&](int count, int value) { for (int j = 0; j < count; j++) table[i++] = static_cast<uint8_t>(value); };
		fill(32, 0);
		for (int v = 1; v <= 3; v++) fill(16, v);
		for (int v = 4; v <= 11; v++) fill(8, v);
		for (int v = 12; v <= 23; v++) fill(4, v);
		for (int v = 24; v <= 47; v++) fill(2, v);
		for (int v = 48; v <= 63; v++) fill(1, v);
		return table;
	}();

	constexpr std::array<uint8_t, 256> LowOffsetBitCount = []()
	{
		std::array<uint8_t, 256> table{};
		int i = 0;
		auto fill = [&](int count, int value) { for (int j = 0; j < count; j++) table[i++] = static_cast<uint8_t>(value); };
		fill(32, 3);
		fill(48, 4);
		fill(64, 5);
		fill(48, 6);
		fill(48, 7);
		fill(16, 8);
		return table;
	}();

	class BitReader
	{
	public:
		BitReader(const uint8_t *src, const uint8_t *srcEnd) : src(src), srcEnd(srcEnd) { }

		int bit()
		{
			this->refill();
			const int value = (this->bitmask >> 15) & 1;
			this->bitmask = static_cast<uint16_t>(this->bitmask << 1);
			this->validBits--;
			return value;
		}

		int byte()
		{
			this->refill();
			const int value = this->bitmask >> 8;
			this->bitmask = static_cast<uint16_t>(this->bitmask << 8);
			this->validBits -= 8;
			return value;
		}

	private:
		void refill()
		{
			while (this->validBits < 9)
			{
				if (this->src != this->srcEnd)
				{
					this->bitmask = static_cast<uint16_t>(this->bitmask | (*(this->src++) << (8 - this->validBits)));
				}

				this->validBits += 8;
			}
		}

		const uint8_t *src;
		const uint8_t *srcEnd;
		uint16_t bitmask = 0;
		int validBits = 0;
	};
}

void arena::compression::decodeType08(const uint8_t *src, const uint8_t *srcEnd, std::span<uint8_t> out)
{
	std::array<uint8_t, 4096> history;
	history.fill(0x20);
	uint16_t historyPos = 0;

	// Árvore de Huffman adaptativa: freq[] ordenado, son[] filhos, parent[] mapeia
	// nós (e folhas, deslocadas por TableSize) para a posição do pai.
	std::array<uint16_t, TableSize + CharCount> parent{};
	std::array<uint16_t, TableSize> son{};
	std::array<uint16_t, TableSize + 1> freq{};

	for (int i = 0; i < CharCount; i++)
	{
		freq[i] = 1;
		son[i] = static_cast<uint16_t>(i + TableSize);
		parent[i + TableSize] = static_cast<uint16_t>(i);
	}

	for (int i = 0, j = CharCount; j <= Root; i += 2, j++)
	{
		freq[j] = static_cast<uint16_t>(freq[i] + freq[i + 1]);
		son[j] = static_cast<uint16_t>(i);
		parent[i] = parent[i + 1] = static_cast<uint16_t>(j);
	}

	freq[TableSize] = 0xFFFF;
	parent[Root] = 0;

	auto update = [&](int c)
	{
		c = parent[c + TableSize];
		do
		{
			const uint16_t k = ++freq[c];
			int l = c + 1;
			if (k > freq[l])
			{
				while (k > freq[++l]) { }
				l--;
				freq[c] = freq[l];
				freq[l] = k;

				const int i = son[c];
				parent[i] = static_cast<uint16_t>(l);
				if (i < TableSize) parent[i + 1] = static_cast<uint16_t>(l);

				const int j = son[l];
				son[l] = static_cast<uint16_t>(i);
				parent[j] = static_cast<uint16_t>(c);
				if (j < TableSize) parent[j + 1] = static_cast<uint16_t>(c);
				son[c] = static_cast<uint16_t>(j);

				c = l;
			}

			c = parent[c];
		} while (c != 0);
	};

	BitReader reader(src, srcEnd);
	size_t dst = 0;
	while (dst < out.size())
	{
		int node = son[Root];
		while (node < TableSize)
		{
			node = son[node + reader.bit()];
		}

		const int code = node - TableSize;
		update(code);

		if (code < 256)
		{
			history[historyPos++ & 0x0FFF] = static_cast<uint8_t>(code);
			out[dst++] = static_cast<uint8_t>(code);
			continue;
		}

		const int tableIndex = reader.byte();
		const int offsetHigh = HighOffsetBits[tableIndex] << 6;
		int offsetLow = tableIndex;
		for (int i = 0; i < LowOffsetBitCount[tableIndex] - 2; i++)
		{
			offsetLow = (offsetLow << 1) | reader.bit();
		}

		uint16_t copyPos = static_cast<uint16_t>(historyPos - (offsetHigh | (offsetLow & 0x3F)) - 1);
		const int toCopy = code - 256 + 3;
		for (int i = 0; i < toCopy && dst < out.size(); i++)
		{
			const uint8_t value = history[copyPos++ & 0x0FFF];
			history[historyPos++ & 0x0FFF] = value;
			out[dst++] = value;
		}
	}
}

void arena::compression::decodeRLE(const uint8_t *src, const uint8_t *srcEnd, std::span<uint8_t> out)
{
	size_t dst = 0;
	while (src < srcEnd && dst < out.size())
	{
		const uint8_t sample = *(src++);
		if ((sample & 0x80) != 0)
		{
			if (src == srcEnd) break;
			const uint8_t value = *(src++);
			for (int i = 0; i < sample - 0x7F && dst < out.size(); i++)
			{
				out[dst++] = value;
			}
		}
		else
		{
			for (int i = 0; i < sample + 1 && src < srcEnd && dst < out.size(); i++)
			{
				out[dst++] = *(src++);
			}
		}
	}
}
