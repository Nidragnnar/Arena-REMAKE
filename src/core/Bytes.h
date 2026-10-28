#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace arena
{
	using ByteBuffer = std::vector<uint8_t>;

	inline uint16_t readLE16(const uint8_t *p)
	{
		return static_cast<uint16_t>(p[0] | (p[1] << 8));
	}

	inline uint32_t readLE32(const uint8_t *p)
	{
		return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
			(static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
	}

	inline std::optional<ByteBuffer> readWholeFile(const std::filesystem::path &path)
	{
		std::ifstream stream(path, std::ios::binary | std::ios::ate);
		if (!stream)
		{
			return std::nullopt;
		}

		const std::streamsize size = stream.tellg();
		stream.seekg(0);
		ByteBuffer data(static_cast<size_t>(size));
		if (size > 0 && !stream.read(reinterpret_cast<char*>(data.data()), size))
		{
			return std::nullopt;
		}

		return data;
	}

	inline bool writeWholeFile(const std::filesystem::path &path, const uint8_t *data, size_t size)
	{
		std::ofstream stream(path, std::ios::binary);
		stream.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
		return static_cast<bool>(stream);
	}

	// Nomes de arquivo do Arena são DOS 8.3 em maiúsculas; normalizamos para comparar.
	inline std::string toUpperAscii(std::string s)
	{
		for (char &c : s)
		{
			if (c >= 'a' && c <= 'z')
			{
				c = static_cast<char>(c - 'a' + 'A');
			}
		}

		return s;
	}
}
