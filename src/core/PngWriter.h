#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "Bytes.h"

namespace arena
{
	// Gravador PNG mínimo (RGBA, deflate sem compressão) para exportar assets e
	// capturas de tela sem dependências externas.
	inline bool writePngRGBA(const std::filesystem::path &path, int width, int height, const uint8_t *rgba)
	{
		static uint32_t crcTable[256];
		static bool crcReady = false;
		if (!crcReady)
		{
			for (uint32_t n = 0; n < 256; n++)
			{
				uint32_t c = n;
				for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
				crcTable[n] = c;
			}

			crcReady = true;
		}

		std::vector<uint8_t> out = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
		auto put32 = [](std::vector<uint8_t> &v, uint32_t x)
		{
			v.push_back(static_cast<uint8_t>(x >> 24)); v.push_back(static_cast<uint8_t>(x >> 16));
			v.push_back(static_cast<uint8_t>(x >> 8)); v.push_back(static_cast<uint8_t>(x));
		};
		auto chunk = [&](const char *type, const std::vector<uint8_t> &payload)
		{
			put32(out, static_cast<uint32_t>(payload.size()));
			const size_t start = out.size();
			out.insert(out.end(), type, type + 4);
			out.insert(out.end(), payload.begin(), payload.end());
			uint32_t crc = 0xFFFFFFFFu;
			for (size_t i = start; i < out.size(); i++) crc = crcTable[(crc ^ out[i]) & 0xFF] ^ (crc >> 8);
			put32(out, crc ^ 0xFFFFFFFFu);
		};

		std::vector<uint8_t> ihdr;
		put32(ihdr, static_cast<uint32_t>(width));
		put32(ihdr, static_cast<uint32_t>(height));
		ihdr.insert(ihdr.end(), { 8, 6, 0, 0, 0 });
		chunk("IHDR", ihdr);

		std::vector<uint8_t> raw;
		const size_t stride = static_cast<size_t>(width) * 4;
		for (int y = 0; y < height; y++)
		{
			raw.push_back(0);
			raw.insert(raw.end(), rgba + y * stride, rgba + (y + 1) * stride);
		}

		std::vector<uint8_t> z = { 0x78, 0x01 };
		uint32_t a = 1, b = 0;
		for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
		for (size_t pos = 0; pos < raw.size() || pos == 0; )
		{
			const size_t len = std::min<size_t>(65535, raw.size() - pos);
			const bool last = pos + len >= raw.size();
			z.push_back(last ? 1 : 0);
			z.push_back(static_cast<uint8_t>(len)); z.push_back(static_cast<uint8_t>(len >> 8));
			z.push_back(static_cast<uint8_t>(~len)); z.push_back(static_cast<uint8_t>(~len >> 8));
			z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + len);
			pos += len;
			if (last) break;
		}

		put32(z, (b << 16) | a);
		chunk("IDAT", z);
		chunk("IEND", {});
		return writeWholeFile(path, out.data(), out.size());
	}
}
