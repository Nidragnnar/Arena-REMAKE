#include <algorithm>
#include <map>

#include "../world/InfFile.h"
#include "DungeonGen.h"

namespace
{
	constexpr int ChunkDim = 32;

	int levelChangeCoord(int chunk) { return 10 + chunk * ChunkDim; }
}

uint32_t arena::wildDungeonSeed(const ArenaData &data, int province, int wildBlockX, int wildBlockY)
{
	const auto [globalX, globalY] = data.provinceOrigin(province);
	const uint32_t provinceSeed = ((static_cast<uint32_t>(globalX) << 16) + static_cast<uint32_t>(globalY)) * static_cast<uint32_t>(province);
	return provinceSeed + static_cast<uint32_t>(((wildBlockY << 6) + wildBlockX) & 0xFFFF);
}

bool arena::generateDungeon(const AssetManager &assets, uint32_t seed, int widthChunks, int depthChunks, bool artifact, MifFile &outMif, std::string &outError)
{
	const auto data = assets.read("RANDOM1.MIF");
	const auto blocks = data ? loadMif(*data, outError) : std::nullopt;
	if (!blocks || blocks->levels.size() < 32)
	{
		if (outError.empty()) outError = "RANDOM1.MIF não encontrado.";
		return false;
	}

	const std::string infName = blocks->levels[0].info;
	const auto infData = assets.read(infName);
	const auto inf = infData ? loadInf(*infData, outError) : std::nullopt;
	if (!inf || !inf->levelUp)
	{
		if (outError.empty()) outError = infName + " sem *LEVELUP.";
		return false;
	}

	// Quantos andares e onde ficam as escadas (bloco sorteado, diferente do andar anterior).
	ArenaRandom random(seed);
	const int levelCount = artifact ? 4 : 1 + (random.next() % 2);
	const uint32_t seed2 = random.state(); // cada andar usa (estado + andar) como semente
	std::vector<std::pair<int, int>> transitions;
	auto nextBlock = [&]() { const int y = random.next() % depthChunks; const int x = random.next() % widthChunks; return std::pair<int, int>{ x, y }; };
	transitions.push_back(nextBlock());
	for (int i = 1; i < levelCount; i++)
	{
		std::pair<int, int> block;
		do { block = nextBlock(); } while (block == transitions.back());
		transitions.push_back(block);
	}

	const int width = widthChunks * ChunkDim, depth = depthChunks * ChunkDim;
	MifFile mif;
	mif.width = width;
	mif.depth = depth;
	for (int i = 0; i < levelCount; i++)
	{
		ArenaRandom levelRandom(seed2 + static_cast<uint32_t>(i));
		MifLevel level;
		level.info = infName;
		level.name = "level" + std::to_string(i + 1);
		level.flor.assign(static_cast<size_t>(width) * depth, 0);
		level.map1.assign(level.flor.size(), 0);

		const int tileSet = levelRandom.next() % 4;
		for (int row = 0; row < depthChunks; row++)
		{
			for (int column = 0; column < widthChunks; column++)
			{
				const MifLevel &block = blocks->levels[static_cast<size_t>(tileSet * 8 + levelRandom.next() % 8)];
				for (int z = 0; z < ChunkDim; z++)
					for (int x = 0; x < ChunkDim; x++)
					{
						const size_t from = static_cast<size_t>(z * blocks->width + x);
						const size_t to = static_cast<size_t>((row * ChunkDim + z) * width + column * ChunkDim + x);
						if (from < block.flor.size()) level.flor[to] = block.flor[from];
						if (from < block.map1.size()) level.map1[to] = block.map1[from];
					}

				for (const MifLevel::Lock &lock : block.locks) level.locks.push_back({ column * ChunkDim + lock.x, row * ChunkDim + lock.y, lock.level });
				for (const MifLevel::Trigger &t : block.triggers) level.triggers.push_back({ column * ChunkDim + t.x, row * ChunkDim + t.y, t.textIndex, t.soundIndex });
			}
		}

		// Borda: a parede mais comum da masmorra.
		std::map<uint16_t, int> walls;
		for (const uint16_t v : level.map1) if (v != 0 && (v & 0x8000) == 0 && (v >> 8) == (v & 0xFF)) walls[v]++;
		uint16_t border = 0x0101;
		int most = 0;
		for (const auto &[v, n] : walls) if (n > most && (!inf->levelUp || (v & 0xFF) - 1 != *inf->levelUp)) { most = n; border = v; }
		for (int x = 0; x < width; x++) { level.map1[static_cast<size_t>(x)] = border; level.map1[static_cast<size_t>((depth - 1) * width + x)] = border; }
		for (int z = 0; z < depth; z++) { level.map1[static_cast<size_t>(z * width)] = border; level.map1[static_cast<size_t>(z * width + width - 1)] = border; }

		// Escadas: subida neste andar e descida para o próximo.
		auto place = [&](std::pair<int, int> block, int textureIndex)
		{
			const uint16_t v = static_cast<uint16_t>(((textureIndex + 1) << 8) | (textureIndex + 1));
			level.map1[static_cast<size_t>(levelChangeCoord(block.second) * width + levelChangeCoord(block.first))] = v;
		};
		place(transitions[static_cast<size_t>(i)], *inf->levelUp);
		if (i + 1 < levelCount && inf->levelDown) place(transitions[static_cast<size_t>(i + 1)], *inf->levelDown);

		mif.levels.push_back(std::move(level));
	}

	outMif = std::move(mif);
	return true;
}
