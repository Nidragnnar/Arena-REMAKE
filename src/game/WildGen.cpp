#include <algorithm>
#include <cstdio>
#include <map>

#include "CityGen.h"
#include "WildGen.h"

namespace
{
	// .RMD: uint16 tamanho em palavras (0 = cru) e as 3 camadas (FLOR, MAP1, MAP2) de 64x64
	// palavras, comprimidas em RLE de palavras.
	struct RmdChunk { std::vector<uint16_t> flor, map1, map2; };

	bool loadRmd(const arena::ByteBuffer &data, RmdChunk &out)
	{
		constexpr size_t Words = arena::WildChunk * arena::WildChunk;
		if (data.size() < 2) return false;
		std::vector<uint16_t> words;
		const size_t declared = data[0] | (data[1] << 8);
		if (declared == 0)
		{
			if (data.size() != Words * 2 * 3) return false;
			words.resize(Words * 3);
			for (size_t i = 0; i < words.size(); i++) words[i] = static_cast<uint16_t>(data[i * 2] | (data[i * 2 + 1] << 8));
		}
		else
		{
			// Amostra positiva: N palavras literais; negativa: repete a próxima palavra -N vezes.
			size_t at = 2;
			while (words.size() < declared && at + 2 <= data.size())
			{
				const int16_t sample = static_cast<int16_t>(data[at] | (data[at + 1] << 8));
				at += 2;
				if (sample > 0)
				{
					for (int j = 0; j < sample && at + 2 <= data.size(); j++, at += 2) words.push_back(static_cast<uint16_t>(data[at] | (data[at + 1] << 8)));
				}
				else
				{
					if (at + 2 > data.size()) break;
					const uint16_t value = static_cast<uint16_t>(data[at] | (data[at + 1] << 8));
					at += 2;
					words.insert(words.end(), static_cast<size_t>(-sample), value);
				}
			}
			words.resize(Words * 3, 0);
		}

		out.flor.assign(words.begin(), words.begin() + Words);
		out.map1.assign(words.begin() + Words, words.begin() + 2 * Words);
		out.map2.assign(words.begin() + 2 * Words, words.begin() + 3 * Words);
		return true;
	}
}

bool arena::generateWilderness(const AssetManager &assets, const ArenaData &data, const CityLocation &city, char weatherLetter, MifFile &outMif, std::string &outError)
{
	// Semente: as 4 primeiras letras do nome da cidade (little-endian).
	uint32_t seed = 0;
	for (size_t i = 0; i < 4 && i < city.name.size(); i++) seed |= static_cast<uint32_t>(static_cast<uint8_t>(city.name[i])) << (8 * i);

	// Sorteia os 64x64 blocos na mesma ordem do original (para dar o mesmo ermo).
	const auto &lists = data.getCityGen().wildBlocks;
	ArenaRandom random(seed);
	std::vector<uint8_t> indices(64 * 64, 0);
	for (uint8_t &index : indices)
	{
		constexpr int Normal = 0x6666, Village = 0x4000, Dungeon = 0x2666, Tavern = 0x1999;
		int value = random.next();
		size_t list = 4;
		if (value < Normal) list = 0;
		else if ((value -= Normal) < Village) list = 1;
		else if ((value -= Village) < Dungeon) list = 2;
		else if ((value -= Dungeon) < Tavern) list = 3;
		const auto &blocks = lists[list];
		index = blocks.empty() ? 5 : blocks[static_cast<size_t>((random.next() & 0xFF) % static_cast<int>(blocks.size()))];
	}

	// Janela de blocos em volta da cidade.
	constexpr int Size = WildWindow * WildChunk;
	MifFile mif;
	mif.width = mif.depth = Size;
	MifLevel level;
	level.flor.assign(static_cast<size_t>(Size) * Size, 0);
	level.map1.assign(level.flor.size(), 0);
	level.map2.assign(level.flor.size(), 0);
	std::map<int, RmdChunk> cache;
	for (int cy = 0; cy < WildWindow; cy++)
	{
		for (int cx = 0; cx < WildWindow; cx++)
		{
			const int wx = WildWindowFirst + cx, wy = WildWindowFirst + cy;
			const bool cityBlock = wx >= WildCityChunk && wx <= WildCityChunk + 1 && wy >= WildCityChunk && wy <= WildCityChunk + 1;
			if (cityBlock) continue;
			const int id = indices[static_cast<size_t>(wy * 64 + wx)];
			auto it = cache.find(id);
			if (it == cache.end())
			{
				char name[16];
				std::snprintf(name, sizeof(name), "WILD%03d.RMD", id);
				RmdChunk chunk;
				const auto file = assets.read(name);
				if (!file || !loadRmd(*file, chunk)) { std::printf("Bloco do ermo ausente: %s\n", name); continue; }
				it = cache.emplace(id, std::move(chunk)).first;
			}

			for (int z = 0; z < WildChunk; z++)
				for (int x = 0; x < WildChunk; x++)
				{
					const size_t from = static_cast<size_t>(z * WildChunk + x);
					const size_t to = static_cast<size_t>((cy * WildChunk + z) * Size + cx * WildChunk + x);
					level.flor[to] = it->second.flor[from];
					level.map1[to] = it->second.map1[from];
					level.map2[to] = it->second.map2[from];
				}
		}
	}

	// Cidade no centro: só as muralhas (e portões) ficam, com as texturas do ermo.
	MifFile cityMif;
	std::string cityInf;
	if (generateCity(assets, data.getCityGen(), city, cityMif, cityInf, outError) && !cityMif.levels.empty())
	{
		const MifLevel &src = cityMif.levels[0];
		const std::vector<uint16_t> grass = level.flor; // chão do ermo por baixo
		for (int z = 0; z < 2 * WildChunk; z++)
			for (int x = 0; x < 2 * WildChunk; x++)
			{
				const size_t to = static_cast<size_t>((WildCityOffset + z) * Size + WildCityOffset + x);
				level.flor[to] = 0;
				level.map1[to] = level.map2[to] = 0;
				if (x >= cityMif.width || z >= cityMif.depth) continue;
				const size_t from = static_cast<size_t>(z * cityMif.width + x);
				level.flor[to] = src.flor.empty() ? 0 : src.flor[from];
				uint16_t m1 = src.map1.empty() ? 0 : src.map1[from];
				uint16_t m2 = src.map2.empty() ? 0 : src.map2[from];
				if ((m1 & 0x8000) != 0 || (m1 != 0x2F2F && m1 != 0x2D2D && m1 != 0x2E2E)) { m1 = 0; m2 = 0; }
				else if (m1 == 0x2F2F) { m1 = 0x3030; m2 = static_cast<uint16_t>(0x3030 | (m2 & 0x8080)); }
				else if (m1 == 0x2D2D) { m1 = 0x2F2F; m2 = static_cast<uint16_t>(0x3030 | (m2 & 0x8080)); }
				else { m2 = static_cast<uint16_t>(0x3030 | (m2 & 0x8080)); }
				level.map1[to] = m1;
				level.map2[to] = m2;
			}
	}

	const char climate = city.climate == CityLocation::Climate::Desert ? 'D' : city.climate == CityLocation::Climate::Mountain ? 'M' : 'T';
	if (climate == 'D' && weatherLetter == 'S') weatherLetter = 'N';
	level.info = std::string(1, climate) + "W" + weatherLetter + ".INF";
	level.name = city.name;
	mif.levels.push_back(std::move(level));
	outMif = std::move(mif);
	return true;
}
