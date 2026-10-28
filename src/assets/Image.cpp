#include <algorithm>
#include <utility>

#include "Compression.h"
#include "Image.h"

namespace
{
	constexpr size_t ImgHeaderSize = 12;
	constexpr uint16_t FlagEmbeddedPalette = 0x0100;

	// .IMG sem cabeçalho, com dimensões fixas no executável original.
	constexpr std::pair<const char*, std::pair<int, int>> RawImgDimensions[] =
	{
		{ "ARENARW.IMG", { 16, 16 } }, { "CITY.IMG", { 16, 11 } }, { "DITHER.IMG", { 8, 100 } },
		{ "DITHER2.IMG", { 8, 100 } }, { "DUNGEON.IMG", { 14, 8 } }, { "DZTTAV.IMG", { 32, 34 } },
		{ "NOCAMP.IMG", { 25, 19 } }, { "NOSPELL.IMG", { 25, 19 } }, { "P1.IMG", { 320, 53 } },
		{ "POPTALK.IMG", { 320, 77 } }, { "S2.IMG", { 320, 36 } }, { "SLIDER.IMG", { 289, 7 } },
		{ "TOWN.IMG", { 9, 10 } }, { "UPDOWN.IMG", { 8, 16 } }, { "VILLAGE.IMG", { 8, 8 } }
	};

	arena::Palette readVgaPalette(const uint8_t *p)
	{
		// Paletas embutidas usam o DAC VGA de 6 bits (0..63).
		arena::Palette palette;
		for (size_t i = 0; i < palette.size(); i++, p += 3)
		{
			palette[i].r = static_cast<uint8_t>(std::min<int>(p[0], 63) * 255 / 63);
			palette[i].g = static_cast<uint8_t>(std::min<int>(p[1], 63) * 255 / 63);
			palette[i].b = static_cast<uint8_t>(std::min<int>(p[2], 63) * 255 / 63);
			palette[i].a = 255;
		}

		palette[0].a = 0;
		return palette;
	}
}

std::optional<arena::Palette> arena::loadColPalette(const ByteBuffer &data)
{
	if (data.size() < 8 + 768 || readLE32(data.data()) != 776 || readLE32(data.data() + 4) != 0xB123)
	{
		return std::nullopt;
	}

	Palette palette;
	const uint8_t *p = data.data() + 8;
	for (size_t i = 0; i < palette.size(); i++, p += 3)
	{
		palette[i] = Color{ p[0], p[1], p[2], 255 };
	}

	palette[0].a = 0; // índice 0 é transparente
	return palette;
}

std::optional<arena::IndexedImage> arena::loadImg(const std::string &name, const ByteBuffer &data)
{
	IndexedImage image;
	const std::string upperName = toUpperAscii(name);

	for (const auto &[rawName, dims] : RawImgDimensions)
	{
		if (upperName == rawName)
		{
			image.width = dims.first;
			image.height = dims.second;
			image.pixels.assign(static_cast<size_t>(image.width * image.height), 0);
			std::copy_n(data.begin(), std::min(data.size(), image.pixels.size()), image.pixels.begin());
			return image;
		}
	}

	// Texturas de parede: 64x64 sem cabeçalho.
	if (data.size() == 4096)
	{
		image.width = 64;
		image.height = 64;
		image.pixels = data;
		return image;
	}

	if (data.size() < ImgHeaderSize)
	{
		return std::nullopt;
	}

	const uint8_t *p = data.data();
	image.offsetX = readLE16(p);
	image.offsetY = readLE16(p + 2);
	image.width = readLE16(p + 4);
	image.height = readLE16(p + 6);
	const uint16_t flags = readLE16(p + 8);
	const size_t length = readLE16(p + 10);

	if (image.width <= 0 || image.height <= 0 || ImgHeaderSize + length > data.size())
	{
		return std::nullopt;
	}

	image.pixels.assign(static_cast<size_t>(image.width * image.height), 0);
	const uint8_t *body = p + ImgHeaderSize;
	const uint8_t *bodyEnd = body + length;

	switch (flags & 0x00FF)
	{
	case 0x00:
		std::copy_n(body, std::min(length, image.pixels.size()), image.pixels.begin());
		break;
	case 0x04:
		compression::decodeType04(body, bodyEnd, image.pixels);
		break;
	case 0x08:
		// Tipo 8 tem 2 bytes com o tamanho descomprimido antes dos dados.
		compression::decodeType08(body + 2, bodyEnd, image.pixels);
		break;
	default:
		return std::nullopt;
	}

	if ((flags & FlagEmbeddedPalette) != 0 && ImgHeaderSize + length + 768 <= data.size())
	{
		image.palette = readVgaPalette(bodyEnd);
	}

	return image;
}

std::vector<arena::IndexedImage> arena::loadSet(const ByteBuffer &data)
{
	constexpr size_t ChunkSize = 64 * 64;
	std::vector<IndexedImage> frames;
	for (size_t offset = 0; offset < data.size(); offset += ChunkSize)
	{
		IndexedImage frame;
		frame.width = 64;
		frame.height = 64;
		frame.pixels.assign(ChunkSize, 0);
		// Um .SET (TBS2.SET) tem um byte a menos; completa com zero.
		std::copy_n(data.begin() + offset, std::min(ChunkSize, data.size() - offset), frame.pixels.begin());
		frames.push_back(std::move(frame));
	}

	return frames;
}

std::vector<arena::IndexedImage> arena::loadDfa(const ByteBuffer &data)
{
	std::vector<IndexedImage> frames;
	if (data.size() < 12)
	{
		return frames;
	}

	const int count = readLE16(data.data());
	const int width = readLE16(data.data() + 6);
	const int height = readLE16(data.data() + 8);
	const size_t firstLength = readLE16(data.data() + 10);
	if (count <= 0 || width <= 0 || height <= 0)
	{
		return frames;
	}

	IndexedImage first;
	first.width = width;
	first.height = height;
	first.pixels.assign(static_cast<size_t>(width * height), 0);
	compression::decodeRLE(data.data() + 12, data.data() + std::min(data.size(), 12 + firstLength), first.pixels);
	frames.push_back(first);

	size_t offset = 12 + firstLength;
	for (int f = 1; f < count && offset + 4 <= data.size(); f++)
	{
		IndexedImage frame = first;
		const int chunkCount = readLE16(data.data() + offset + 2);
		offset += 4;
		for (int c = 0; c < chunkCount && offset + 4 <= data.size(); c++)
		{
			const size_t updateOffset = readLE16(data.data() + offset);
			const size_t updateCount = readLE16(data.data() + offset + 2);
			offset += 4;
			for (size_t i = 0; i < updateCount && offset < data.size(); i++, offset++)
			{
				if (updateOffset + i < frame.pixels.size()) frame.pixels[updateOffset + i] = data[offset];
			}
		}

		frames.push_back(std::move(frame));
	}

	return frames;
}

std::vector<arena::IndexedImage> arena::loadCfa(const ByteBuffer &data)
{
	// Formato adaptado do OpenTESArena/WinArena.
	std::vector<IndexedImage> frames;
	if (data.size() < 76)
	{
		return frames;
	}

	const int width = readLE16(data.data());
	const int height = readLE16(data.data() + 2);
	const int packedWidth = readLE16(data.data() + 4);
	const int offsetX = readLE16(data.data() + 6);
	const int offsetY = readLE16(data.data() + 8);
	const int bitsPerPixel = data[10];
	const int frameCount = data[11];
	const size_t headerSize = readLE16(data.data() + 12);
	const uint8_t *lookup = data.data() + 76;
	if (width <= 0 || height <= 0 || frameCount <= 0 || bitsPerPixel < 1 || bitsPerPixel > 8 || headerSize >= data.size())
	{
		return frames;
	}

	std::vector<uint8_t> packed(static_cast<size_t>(packedWidth * height * frameCount) + 16, 0);
	compression::decodeRLE(data.data() + headerSize, data.data() + data.size(), std::span<uint8_t>(packed.data(), packed.size() - 16));

	// Lê o i-ésimo valor de 'bits' bits (MSB primeiro) da linha empacotada.
	auto unpack = [](const uint8_t *line, int index, int bits)
	{
		const int bitPos = index * bits;
		int value = 0;
		for (int b = 0; b < bits; b++)
		{
			const int bit = bitPos + b;
			value = (value << 1) | ((line[bit >> 3] >> (7 - (bit & 7))) & 1);
		}

		return value;
	};

	size_t offset = 0;
	for (int f = 0; f < frameCount; f++)
	{
		IndexedImage frame;
		frame.width = width;
		frame.height = height;
		frame.offsetX = offsetX;
		frame.offsetY = offsetY;
		frame.pixels.assign(static_cast<size_t>(width * height), 0);
		for (int y = 0; y < height; y++, offset += static_cast<size_t>(packedWidth))
		{
			const uint8_t *line = packed.data() + offset;
			for (int x = 0; x < width; x++)
			{
				frame.pixels[static_cast<size_t>(x + y * width)] = (bitsPerPixel == 8) ? line[x] : lookup[unpack(line, x, bitsPerPixel)];
			}
		}

		frames.push_back(std::move(frame));
	}

	return frames;
}

std::vector<arena::IndexedImage> arena::loadCif(const std::string &name, const ByteBuffer &data)
{
	// Peças de moldura sem cabeçalho: 9 quadros de tamanho fixo.
	constexpr std::pair<const char*, std::pair<int, int>> RawCifDimensions[] =
	{
		{ "BRASS.CIF", { 8, 8 } }, { "BRASS2.CIF", { 8, 8 } }, { "MARBLE.CIF", { 3, 3 } },
		{ "MARBLE2.CIF", { 3, 3 } }, { "PARCH.CIF", { 20, 20 } }, { "SCROLL.CIF", { 20, 20 } }
	};

	std::vector<IndexedImage> frames;
	const std::string upperName = toUpperAscii(name);
	for (const auto &[rawName, dims] : RawCifDimensions)
	{
		if (upperName != rawName)
		{
			continue;
		}

		const size_t frameSize = static_cast<size_t>(dims.first * dims.second);
		for (size_t offset = 0; offset + frameSize <= data.size() && frames.size() < 9; offset += frameSize)
		{
			IndexedImage frame;
			frame.width = dims.first;
			frame.height = dims.second;
			frame.pixels.assign(data.begin() + offset, data.begin() + offset + frameSize);
			frames.push_back(std::move(frame));
		}

		return frames;
	}

	size_t offset = 0;
	while (offset + ImgHeaderSize <= data.size())
	{
		const uint8_t *header = data.data() + offset;
		IndexedImage frame;
		frame.offsetX = readLE16(header);
		frame.offsetY = readLE16(header + 2);
		frame.width = readLE16(header + 4);
		frame.height = readLE16(header + 6);
		const uint16_t flags = readLE16(header + 8);
		const size_t length = readLE16(header + 10);
		const uint8_t *body = header + ImgHeaderSize;
		const uint8_t *bodyEnd = std::min(body + length, data.data() + data.size());
		if (frame.width <= 0 || frame.height <= 0)
		{
			break;
		}

		frame.pixels.assign(static_cast<size_t>(frame.width * frame.height), 0);
		switch (flags & 0x00FF)
		{
		case 0x00: std::copy_n(body, std::min<size_t>(bodyEnd - body, frame.pixels.size()), frame.pixels.begin()); break;
		case 0x02: compression::decodeRLE(body, bodyEnd, frame.pixels); break;
		case 0x04: compression::decodeType04(body, bodyEnd, frame.pixels); break;
		case 0x08: compression::decodeType08(body + 2, bodyEnd, frame.pixels); break;
		default: return frames;
		}

		frames.push_back(std::move(frame));
		offset += ImgHeaderSize + length;
	}

	return frames;
}
