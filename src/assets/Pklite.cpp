#include <array>
#include <string_view>

#include "Pklite.h"

namespace
{
	// Códigos de prefixo como strings de bits ('0'/'1'), na ordem em que são lidos.
	struct PrefixCode
	{
		std::string_view bits;
		int value;
	};

	// Quantidade de bytes a copiar (seção 4.3.1). Valor -1 = caso especial "011100".
	constexpr std::array<PrefixCode, 24> CopyCountCodes =
	{ {
		{ "10", 2 }, { "11", 3 }, { "000", 4 }, { "0010", 5 }, { "0011", 6 }, { "0100", 7 },
		{ "01010", 8 }, { "01011", 9 }, { "01100", 10 }, { "011010", 11 }, { "011011", 12 },
		{ "011100", -1 },
		{ "0111010", 13 }, { "0111011", 14 }, { "0111100", 15 }, { "01111010", 16 },
		{ "01111011", 17 }, { "01111100", 18 }, { "011111010", 19 }, { "011111011", 20 },
		{ "011111100", 21 }, { "011111101", 22 }, { "011111110", 23 }, { "011111111", 24 }
	} };

	// Byte mais significativo do deslocamento (seção 4.3.2).
	constexpr std::array<PrefixCode, 32> OffsetHighCodes =
	{ {
		{ "1", 0 }, { "0000", 1 }, { "0001", 2 }, { "00100", 3 }, { "00101", 4 }, { "00110", 5 },
		{ "00111", 6 }, { "010000", 7 }, { "010001", 8 }, { "010010", 9 }, { "010011", 10 },
		{ "010100", 11 }, { "010101", 12 }, { "010110", 13 }, { "0101110", 14 }, { "0101111", 15 },
		{ "0110000", 16 }, { "0110001", 17 }, { "0110010", 18 }, { "0110011", 19 }, { "0110100", 20 },
		{ "0110101", 21 }, { "0110110", 22 }, { "0110111", 23 }, { "0111000", 24 }, { "0111001", 25 },
		{ "0111010", 26 }, { "0111011", 27 }, { "0111100", 28 }, { "0111101", 29 }, { "0111110", 30 },
		{ "0111111", 31 }
	} };

	// Início dos dados comprimidos no A.EXE/ACD.EXE (após o stub do PKLITE).
	constexpr size_t CompressedDataOffset = 752;

	class BitStream
	{
	public:
		BitStream(const uint8_t *data, size_t size) : data(data), size(size)
		{
			this->word = this->nextWord();
		}

		bool nextBit()
		{
			const bool bit = (this->word & (1u << this->bitsRead)) != 0;
			this->bitsRead++;
			if (this->bitsRead == 16)
			{
				this->bitsRead = 0;
				this->word = this->nextWord();
			}

			return bit;
		}

		uint8_t nextByte()
		{
			if (this->pos >= this->size)
			{
				this->overrun = true;
				return 0;
			}

			return this->data[this->pos++];
		}

		int getBitsRead() const { return this->bitsRead; }
		bool hasOverrun() const { return this->overrun; }

	private:
		uint16_t nextWord()
		{
			const uint8_t lo = this->nextByte();
			const uint8_t hi = this->nextByte();
			return static_cast<uint16_t>(lo | (hi << 8));
		}

		const uint8_t *data;
		size_t size;
		size_t pos = 0;
		uint16_t word = 0;
		int bitsRead = 0;
		bool overrun = false;
	};

	template<size_t N>
	bool decodePrefix(BitStream &stream, const std::array<PrefixCode, N> &codes, int &outValue, std::string &outBits)
	{
		outBits.clear();
		while (outBits.size() < 9)
		{
			outBits.push_back(stream.nextBit() ? '1' : '0');
			for (const PrefixCode &code : codes)
			{
				if (code.bits == outBits)
				{
					outValue = code.value;
					return true;
				}
			}
		}

		return false;
	}
}

bool arena::unpackPklite(const ByteBuffer &exe, ByteBuffer &outImage, std::string &outError)
{
	if (exe.size() < CompressedDataOffset + 16 || exe[0] != 'M' || exe[1] != 'Z')
	{
		outError = "Arquivo não é um executável MZ válido.";
		return false;
	}

	// Os últimos 8 bytes guardam segmento:deslocamento do fim da imagem; antes deles,
	// a palavra 0xFFFF marca o fim do fluxo comprimido.
	const uint8_t *end = exe.data() + exe.size() - 8;
	if (readLE16(end - 2) != 0xFFFF)
	{
		outError = "Assinatura PKLITE não encontrada (palavra final != 0xFFFF).";
		return false;
	}

	const size_t imageSize = static_cast<size_t>(readLE16(end)) * 16 + readLE16(end + 2);
	outImage.assign(imageSize, 0);

	BitStream stream(exe.data() + CompressedDataOffset, exe.size() - CompressedDataOffset);
	size_t outPos = 0;
	std::string bits;

	while (true)
	{
		if (stream.hasOverrun())
		{
			outError = "Fluxo comprimido terminou inesperadamente.";
			return false;
		}

		if (!stream.nextBit())
		{
			// Literal: byte cifrado com XOR pela quantidade de bits restantes na palavra.
			const uint8_t key = static_cast<uint8_t>(16 - stream.getBitsRead());
			const uint8_t value = stream.nextByte() ^ key;
			if (outPos >= outImage.size())
			{
				outError = "Saída excedeu o tamanho declarado da imagem.";
				return false;
			}

			outImage[outPos++] = value;
			continue;
		}

		int copyCount = 0;
		if (!decodePrefix(stream, CopyCountCodes, copyCount, bits))
		{
			outError = "Código de tamanho inválido.";
			return false;
		}

		if (copyCount == -1)
		{
			const uint8_t extended = stream.nextByte();
			if (extended == 0xFE)
			{
				continue;
			}
			else if (extended == 0xFF)
			{
				break;
			}

			copyCount = extended + 25;
		}

		int offsetHigh = 0;
		if (copyCount != 2 && !decodePrefix(stream, OffsetHighCodes, offsetHigh, bits))
		{
			outError = "Código de deslocamento inválido.";
			return false;
		}

		const size_t offset = static_cast<size_t>(stream.nextByte() | (offsetHigh << 8));
		if (offset > outPos || outPos + copyCount > outImage.size())
		{
			outError = "Referência de cópia fora dos limites.";
			return false;
		}

		for (int i = 0; i < copyCount; i++, outPos++)
		{
			outImage[outPos] = outImage[outPos - offset];
		}
	}

	return true;
}
