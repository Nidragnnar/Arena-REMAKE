// Formato .MIF baseado na pesquisa do OpenTESArena (MIT).

#include <cstring>

#include "../assets/Compression.h"
#include "MifFile.h"

namespace
{
	// Camadas FLOR/MAP1/MAP2: tamanho comprimido, tamanho descomprimido e dados tipo 8.
	size_t decodeLayer(const uint8_t *tag, const uint8_t *fileEnd, int width, int depth, std::vector<uint16_t> &out)
	{
		const uint16_t compressedSize = arena::readLE16(tag + 4);
		const uint16_t uncompressedSize = arena::readLE16(tag + 6);
		std::vector<uint8_t> decoded(std::max<size_t>(uncompressedSize, static_cast<size_t>(width * depth * 2)), 0);
		const uint8_t *begin = tag + 8;
		const uint8_t *end = std::min(tag + 6 + compressedSize, fileEnd);
		if (begin < end)
		{
			arena::compression::decodeType08(begin, end, decoded);
		}

		out.resize(static_cast<size_t>(width * depth));
		for (size_t i = 0; i < out.size(); i++)
		{
			out[i] = arena::readLE16(decoded.data() + i * 2);
		}

		return static_cast<size_t>(compressedSize) + 6;
	}

	std::string readCString(const uint8_t *p, size_t maxLen)
	{
		size_t len = 0;
		while (len < maxLen && p[len] != 0) len++;
		return std::string(reinterpret_cast<const char*>(p), len);
	}
}

std::optional<arena::MifFile> arena::loadMif(const ByteBuffer &data, std::string &outError)
{
	if (data.size() < 6 + 27 || std::memcmp(data.data(), "MHDR", 4) != 0)
	{
		outError = "Arquivo .MIF inválido (sem MHDR).";
		return std::nullopt;
	}

	MifFile mif;
	const uint8_t *p = data.data();
	const uint8_t *fileEnd = p + data.size();
	const uint16_t headerSize = readLE16(p + 4);
	const uint8_t *header = p + 6;
	for (size_t i = 0; i < 4; i++)
	{
		mif.startPoints[i] = { readLE16(header + 2 + i * 2), readLE16(header + 10 + i * 2) };
	}

	mif.startingLevel = header[18];
	mif.width = readLE16(header + 21);
	mif.depth = readLE16(header + 23);
	if (mif.width <= 0 || mif.depth <= 0 || mif.width > 1024 || mif.depth > 1024)
	{
		outError = "Dimensões inválidas no .MIF.";
		return std::nullopt;
	}

	const uint8_t *level = p + 6 + headerSize;
	while (level + 6 <= fileEnd && std::memcmp(level, "LEVL", 4) == 0)
	{
		MifLevel out;
		const uint16_t levelSize = readLE16(level + 4);
		const uint8_t *tag = level + 6;
		const uint8_t *levelEnd = std::min(tag + levelSize, fileEnd);
		while (tag + 6 <= levelEnd)
		{
			const std::string name(reinterpret_cast<const char*>(tag), 4);
			const uint16_t size = readLE16(tag + 4);
			const uint8_t *body = tag + 6;
			size_t advance = static_cast<size_t>(size) + 6;

			if (name == "FLOR") advance = decodeLayer(tag, fileEnd, mif.width, mif.depth, out.flor);
			else if (name == "MAP1") advance = decodeLayer(tag, fileEnd, mif.width, mif.depth, out.map1);
			else if (name == "MAP2") advance = decodeLayer(tag, fileEnd, mif.width, mif.depth, out.map2);
			else if (name == "NAME") out.name = readCString(body, size);
			else if (name == "INFO") out.info = readCString(body, size);
			else if (name == "NUMF") out.numf = body[0];
			else if (name == "LOCK")
			{
				for (size_t i = 0; i + 3 <= size; i += 3) out.locks.push_back({ body[i], body[i + 1], body[i + 2] });
			}
			else if (name == "TRIG")
			{
				for (size_t i = 0; i + 4 <= size; i += 4)
					out.triggers.push_back({ body[i], body[i + 1], static_cast<int8_t>(body[i + 2]), static_cast<int8_t>(body[i + 3]) });
			}
			else if (name != "FLAT" && name != "INNS" && name != "LOOT" && name != "STOR" && name != "TARG")
			{
				break; // tag desconhecida: arquivo corrompido
			}

			tag += advance;
		}

		mif.levels.push_back(std::move(out));
		// O tamanho do LEVL do WILD.MIF é inconsistente; segue a posição real das tags.
		level = std::max(tag, levelEnd);
	}

	if (mif.levels.empty())
	{
		outError = "Nenhum andar (LEVL) no .MIF.";
		return std::nullopt;
	}

	return mif;
}
