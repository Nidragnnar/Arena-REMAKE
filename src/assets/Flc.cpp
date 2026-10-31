#include <algorithm>

#include "Flc.h"

namespace
{
	uint16_t u16(const arena::ByteBuffer &b, size_t o) { return o + 1 < b.size() ? static_cast<uint16_t>(b[o] | (b[o + 1] << 8)) : 0; }
	uint32_t u32(const arena::ByteBuffer &b, size_t o) { return o + 3 < b.size() ? static_cast<uint32_t>(u16(b, o) | (u16(b, o + 2) << 16)) : 0; }
	uint8_t u8(const arena::ByteBuffer &b, size_t o) { return o < b.size() ? b[o] : 0; }
	int s8(const arena::ByteBuffer &b, size_t o) { return static_cast<int8_t>(u8(b, o)); }
}

bool arena::FlcPlayer::open(ByteBuffer bytes, std::string &outError)
{
	this->data = std::move(bytes);
	const uint16_t type = u16(this->data, 4);
	if (this->data.size() < 128 || (type != 0xAF12 && type != 0xAF11))
	{
		outError = "Arquivo FLIC inválido.";
		return false;
	}

	this->count = u16(this->data, 6);
	this->w = u16(this->data, 8);
	this->h = u16(this->data, 10);
	const uint32_t speed = u32(this->data, 16);
	this->delayMs = std::max(1, static_cast<int>(type == 0xAF11 ? speed * 1000 / 70 : speed)); // FLI: 1/70 s
	this->rewind();
	return this->w > 0 && this->h > 0 && this->count > 0;
}

void arena::FlcPlayer::rewind()
{
	this->cursor = 128;
	this->decoded = 0;
	this->indices.assign(static_cast<size_t>(this->w) * this->h, 0);
	this->pixels.assign(this->indices.size(), 0xFF000000u);
}

bool arena::FlcPlayer::nextFrame()
{
	while (this->decoded < this->count && this->cursor + 6 <= this->data.size())
	{
		const size_t at = this->cursor;
		const uint32_t size = u32(this->data, at);
		if (size < 6) break;
		this->cursor += size;
		if (u16(this->data, at + 4) != 0xF1FA) continue; // prefixo e outros blocos

		const int chunks = u16(this->data, at + 6);
		size_t c = at + 16;
		for (int k = 0; k < chunks && c + 6 <= this->data.size(); k++)
		{
			this->decodeChunk(c + 6, u16(this->data, c + 4));
			c += std::max<uint32_t>(6, u32(this->data, c));
		}

		for (size_t i = 0; i < this->indices.size(); i++)
		{
			const uint8_t *p = &this->palette[static_cast<size_t>(this->indices[i]) * 3];
			this->pixels[i] = 0xFF000000u | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[0];
		}
		this->decoded++;
		return true;
	}

	return false;
}

void arena::FlcPlayer::decodeChunk(size_t o, uint16_t type)
{
	const ByteBuffer &b = this->data;
	const int width = this->w, height = this->h;
	auto put = [&](int x, int y, uint8_t v) { if (x >= 0 && x < width && y >= 0 && y < height) this->indices[static_cast<size_t>(y) * width + x] = v; };

	if (type == 4 || type == 11)
	{
		// Paleta: pacotes (pular, quantidade) de cores RGB; a de 64 níveis vai a 0..255.
		const int scale = type == 11 ? 4 : 1;
		int packets = u16(b, o), index = 0;
		o += 2;
		while (packets-- > 0)
		{
			index += u8(b, o++);
			int n = u8(b, o++);
			if (n == 0) n = 256;
			for (int i = 0; i < n && index < 256; i++, index++)
				for (int k = 0; k < 3; k++) this->palette[static_cast<size_t>(index) * 3 + k] = static_cast<uint8_t>(std::min(255, u8(b, o++) * scale));
		}
	}
	else if (type == 15)
	{
		// BYTE_RUN: quadro inteiro comprimido por repetições, linha a linha.
		for (int y = 0; y < height; y++)
		{
			o++; // contagem de pacotes (ignorada)
			for (int x = 0; x < width;)
			{
				const int n = s8(b, o++);
				if (n < 0) for (int i = 0; i < -n; i++) put(x++, y, u8(b, o++));
				else { const uint8_t v = u8(b, o++); for (int i = 0; i < n; i++) put(x++, y, v); }
				if (o >= b.size()) return;
			}
		}
	}
	else if (type == 7)
	{
		// DELTA_FLC: linhas alteradas, em palavras de 2 pixels.
		int lines = u16(b, o), y = 0;
		o += 2;
		while (lines > 0 && o + 1 < b.size())
		{
			uint16_t word = u16(b, o);
			o += 2;
			if ((word & 0xC000) == 0xC000) { y += 0x10000 - word; continue; }
			if ((word & 0xC000) == 0x8000)
			{
				put(width - 1, y, static_cast<uint8_t>(word & 0xFF));
				word = u16(b, o);
				o += 2;
				if (word == 0) { y++; lines--; continue; }
			}

			int x = 0;
			for (int p = 0; p < word; p++)
			{
				x += u8(b, o++);
				const int n = s8(b, o++);
				if (n > 0) for (int i = 0; i < n * 2; i++) put(x++, y, u8(b, o++));
				else
				{
					const uint8_t a = u8(b, o), c = u8(b, o + 1);
					o += 2;
					for (int i = 0; i < -n; i++) { put(x++, y, a); put(x++, y, c); }
				}
			}
			y++;
			lines--;
		}
	}
	else if (type == 12)
	{
		// DELTA_FLI: linhas alteradas, em bytes.
		int y = u16(b, o);
		const int lines = u16(b, o + 2);
		o += 4;
		for (int l = 0; l < lines && o < b.size(); l++, y++)
		{
			int packets = u8(b, o++), x = 0;
			while (packets-- > 0)
			{
				x += u8(b, o++);
				const int n = s8(b, o++);
				if (n > 0) for (int i = 0; i < n; i++) put(x++, y, u8(b, o++));
				else { const uint8_t v = u8(b, o++); for (int i = 0; i < -n; i++) put(x++, y, v); }
			}
		}
	}
	else if (type == 13) std::fill(this->indices.begin(), this->indices.end(), uint8_t{ 0 });
	else if (type == 16)
	{
		for (size_t i = 0; i < this->indices.size() && o + i < b.size(); i++) this->indices[i] = b[o + i];
	}
}
