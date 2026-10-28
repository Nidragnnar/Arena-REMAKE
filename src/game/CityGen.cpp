#include <algorithm>
#include <cctype>
#include <cstdio>

#include "CityGen.h"

namespace
{
	// Tipos de quarteirão na planta da cidade (mesma numeração do original).
	enum BlockType { Empty, Reserved, Equipment, MagesGuild, NobleHouse, Temple, Tavern, Spacer, Houses };

	// Prefixo do arquivo e quantidade de variações, a partir de Equipment (EQBD1A.MIF...).
	constexpr const char *BlockCodes[] = { "EQ", "MG", "NB", "TP", "TV", "TS", "BS" };
	constexpr int BlockVariations[] = { 13, 11, 10, 12, 15, 11, 20 };
	constexpr const char *Rotations[] = { "A", "B", "C", "D" };

	BlockType randomBlockType(arena::ArenaRandom &random)
	{
		const int value = random.next();
		if (value <= 0x7333) return Houses;
		if (value <= 0xA666) return Tavern;
		if (value <= 0xCCCC) return Equipment;
		if (value <= 0xE666) return Temple;
		return NobleHouse;
	}

	std::string blockMifName(BlockType type, arena::ArenaRandom &random)
	{
		const int index = static_cast<int>(type) - 2;
		const char *rotation = Rotations[random.next() % 4];
		const int variation = std::max(random.next() % BlockVariations[index], 1);
		char name[32];
		std::snprintf(name, sizeof(name), "%sBD%d%s.MIF", BlockCodes[index], variation, rotation);
		return name;
	}
}

bool arena::generateCity(const AssetManager &assets, const CityGenTables &tables, const CityLocation &city, MifFile &outMif, std::string &outInf, std::string &outError)
{
	using Type = CityLocation::Type;
	if (city.type == Type::Dungeon) { outError = city.name + " é uma masmorra."; return false; }
	const bool cityState = city.type == Type::CityState;
	const int templateCount = city.coastal ? (cityState ? 3 : 2) : 5;
	const int templateId = city.globalId % templateCount;
	const int nameIndex = cityState ? (city.coastal ? 5 : 4) : city.type == Type::Town ? (city.coastal ? 1 : 0) : (city.coastal ? 3 : 2);
	const int startIndex = cityState ? (city.coastal ? 19 : 14) + templateId : city.type == Type::Town ? (city.coastal ? 5 : 0) + templateId : (city.coastal ? 12 : 7) + templateId;
	const int reservedIndex = (city.coastal ? 5 : 0) + templateId;
	const int cityDim = cityState ? 6 : city.type == Type::Town ? 5 : 4;
	if (nameIndex >= static_cast<int>(tables.templateNames.size()) || startIndex >= static_cast<int>(tables.startingPositions.size())
		|| reservedIndex >= static_cast<int>(tables.reservedBlocks.size()))
	{
		outError = "Tabelas de cidades incompletas.";
		return false;
	}

	// A Cidade Imperial é pronta (IMPERIAL.MIF), sem quarteirões sorteados.
	if (city.province == CenterProvinceID && city.localId == 0)
	{
		const auto data = assets.read("IMPERIAL.MIF");
		auto mif = data ? loadMif(*data, outError) : std::nullopt;
		if (!mif || mif->levels.empty()) { if (outError.empty()) outError = "IMPERIAL.MIF não encontrado."; return false; }
		outInf = "TCN.INF";
		mif->levels[0].name = city.name;
		mif->levels[0].info = outInf;
		outMif = std::move(*mif);
		return true;
	}

	// Molde da cidade ("city%d.mif" -> "CITY3.MIF").
	std::string templateName = tables.templateNames[static_cast<size_t>(nameIndex)];
	const size_t percent = templateName.find("%d");
	if (percent != std::string::npos) templateName.replace(percent, 2, std::to_string(templateId + 1));
	for (char &c : templateName) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	const auto templateData = assets.read(templateName);
	auto mif = templateData ? loadMif(*templateData, outError) : std::nullopt;
	if (!mif || mif->levels.empty())
	{
		if (outError.empty()) outError = templateName + " não encontrado.";
		return false;
	}

	// Planta: blocos reservados, um de cada serviço e o resto sorteado.
	ArenaRandom random(city.seed());
	const int citySize = cityDim * cityDim;
	std::vector<BlockType> plan(static_cast<size_t>(citySize), Empty);
	for (const uint8_t reserved : tables.reservedBlocks[static_cast<size_t>(reservedIndex)])
		if (reserved < plan.size()) plan[reserved] = Reserved;

	auto place = [&](BlockType type)
	{
		int index = 0;
		do { index = random.next() % citySize; } while (plan[static_cast<size_t>(index)] != Empty);
		plan[static_cast<size_t>(index)] = type;
	};

	for (BlockType type : { Equipment, MagesGuild, NobleHouse, Temple, Tavern, Spacer }) place(type);
	for (int remaining = static_cast<int>(std::count(plan.begin(), plan.end(), Empty)); remaining > 0; remaining--) place(randomBlockType(random));

	// Copia cada quarteirão para o molde, a partir do canto inicial, de 20 em 20 voxels.
	MifLevel &level = mif->levels[0];
	const auto [startX, startZ] = tables.startingPositions[static_cast<size_t>(startIndex)];
	for (int i = 0; i < citySize; i++)
	{
		const BlockType type = plan[static_cast<size_t>(i)];
		if (type == Reserved) continue;

		const std::string blockName = blockMifName(type, random);
		const auto blockData = assets.read(blockName);
		std::string blockError;
		const auto block = blockData ? loadMif(*blockData, blockError) : std::nullopt;
		if (!block || block->levels.empty())
		{
			std::printf("Quarteirão ausente: %s\n", blockName.c_str());
			continue;
		}

		const MifLevel &src = block->levels[0];
		const int xOffset = startX + (i % cityDim) * 20, zOffset = startZ + (i / cityDim) * 20;
		for (int z = 0; z < block->depth; z++)
		{
			for (int x = 0; x < block->width; x++)
			{
				const int dx = xOffset + x, dz = zOffset + z;
				if (dx < 0 || dz < 0 || dx >= mif->width || dz >= mif->depth) continue;
				const size_t from = static_cast<size_t>(x + z * block->width), to = static_cast<size_t>(dx + dz * mif->width);
				if (from < src.flor.size() && to < level.flor.size()) level.flor[to] = src.flor[from];
				if (from < src.map1.size() && to < level.map1.size()) level.map1[to] = src.map1[from];
				if (from < src.map2.size() && to < level.map2.size()) level.map2[to] = src.map2[from];
			}
		}
	}

	// .INF pelo clima (temperado/deserto/montanha), cidade, tempo limpo.
	const char climate = city.climate == CityLocation::Climate::Desert ? 'D' : city.climate == CityLocation::Climate::Mountain ? 'M' : 'T';
	outInf = std::string(1, climate) + "CN.INF";
	level.name = city.name;
	level.info = outInf;
	outMif = std::move(*mif);
	return true;
}
