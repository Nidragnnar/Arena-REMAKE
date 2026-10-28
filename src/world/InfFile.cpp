// Formato .INF baseado na pesquisa do OpenTESArena (MIT) e do BSATool.

#include <array>
#include <cctype>
#include <cstdlib>
#include <sstream>

#include "../core/KeyValueFile.h"
#include "InfFile.h"

namespace
{
	std::vector<std::string> splitWhitespace(const std::string &line)
	{
		std::vector<std::string> tokens;
		std::istringstream stream(line);
		std::string token;
		while (stream >> token) tokens.push_back(token);
		return tokens;
	}

	std::string upper(std::string s)
	{
		for (char &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		return s;
	}

	void decrypt(arena::ByteBuffer &data)
	{
		constexpr std::array<uint8_t, 8> Keys = { 0xEA, 0x7B, 0x4E, 0xBD, 0x19, 0xC9, 0x38, 0x99 };
		uint8_t count = 0;
		for (size_t i = 0; i < data.size(); i++)
		{
			data[i] ^= static_cast<uint8_t>(count + Keys[i % Keys.size()]);
			count++;
		}
	}
}

std::optional<arena::InfFile> arena::loadInf(ByteBuffer data, std::string &outError)
{
	// Arquivos soltos são texto puro; os do GLOBAL.BSA começam cifrados.
	size_t firstChar = 0;
	while (firstChar < data.size() && std::isspace(data[firstChar])) firstChar++;
	if (firstChar >= data.size())
	{
		outError = "Arquivo .INF vazio.";
		return std::nullopt;
	}

	if (data[firstChar] != '@')
	{
		decrypt(data);
	}

	InfFile inf;
	enum class Section { None, Floors, Walls, Flats, Sound, Text, Other } section = Section::None;
	int currentText = -1;

	// Marcadores (*BOXCAP, *CEILING...) acumulados até a próxima linha de textura.
	std::vector<std::vector<std::string>> pendingTags;
	std::optional<int> pendingItem;

	std::istringstream stream(std::string(data.begin(), data.end()));
	std::string rawLine;
	while (std::getline(stream, rawLine))
	{
		const std::string line = trimWhitespace(rawLine);
		if (line.empty())
		{
			continue;
		}

		if (section == Section::Text && line[0] != '@')
		{
			if (line[0] == '*')
			{
				const std::vector<std::string> tokens = splitWhitespace(line);
				currentText = (upper(tokens[0]) == "*TEXT" && tokens.size() >= 2) ? std::atoi(tokens[1].c_str()) : -1;
				if (currentText >= 0) inf.texts[currentText] = InfText{};
				continue;
			}

			if (currentText < 0) continue;
			InfText &entry = inf.texts[currentText];
			std::string content = line;
			if (entry.text.empty() && !entry.keyId && content[0] == '+')
			{
				entry.keyId = std::atoi(content.c_str() + 1);
				continue;
			}

			if (entry.text.empty() && content[0] == '^') entry.riddle = true;
			if (entry.text.empty() && content[0] == '~')
			{
				entry.once = true;
				content.erase(0, 1);
			}

			if (!entry.text.empty()) entry.text += ' ';
			entry.text += content;
			continue;
		}

		if (line[0] == '@')
		{
			const std::string name = upper(splitWhitespace(line)[0]);
			section = (name == "@FLOORS") ? Section::Floors : (name == "@WALLS") ? Section::Walls
				: (name == "@FLATS") ? Section::Flats : (name == "@SOUND") ? Section::Sound
				: (name == "@TEXT") ? Section::Text : Section::Other;
			currentText = -1;
			pendingTags.clear();
			pendingItem.reset();
			continue;
		}

		if (section == Section::Floors || section == Section::Walls)
		{
			if (line[0] == '*')
			{
				std::vector<std::string> tokens = splitWhitespace(line);
				tokens[0] = upper(tokens[0].substr(1));
				if (tokens[0] == "CEILING")
				{
					if (tokens.size() >= 2) inf.ceilingHeight = std::atoi(tokens[1].c_str());
					if (tokens.size() >= 3) inf.boxScale = std::atoi(tokens[2].c_str());
				}

				pendingTags.push_back(std::move(tokens));
				continue;
			}

			// Linha de textura: "NOME.IMG" ou "NOME.SET  #n" (n imagens 64x64).
			const size_t hash = line.find('#');
			const std::string name = upper(trimWhitespace(line.substr(0, hash)));
			const int setCount = (hash != std::string::npos) ? std::max(1, std::atoi(line.c_str() + hash + 1)) : 0;
			const int index = static_cast<int>(inf.voxelTextures.size());
			if (setCount == 0)
			{
				inf.voxelTextures.push_back({ name, -1 });
			}
			else
			{
				for (int i = 0; i < setCount; i++) inf.voxelTextures.push_back({ name, i });
			}

			for (const auto &tag : pendingTags)
			{
				const int id = (tag.size() >= 2) ? std::atoi(tag[1].c_str()) : 0;
				if (tag[0] == "BOXCAP") inf.boxCaps[id] = index;
				else if (tag[0] == "BOXSIDE") inf.boxSides[id] = index;
				else if (tag[0] == "CEILING") inf.ceilingTexture = index;
				else if (tag[0] == "DRYCHASM") inf.dryChasm = index;
				else if (tag[0] == "WETCHASM") inf.wetChasm = index;
				else if (tag[0] == "LAVACHASM") inf.lavaChasm = index;
				else if (tag[0] == "LEVELUP") inf.levelUp = index;
				else if (tag[0] == "LEVELDOWN") inf.levelDown = index;
				else if (tag[0] == "MENU") inf.menus[id] = index;
			}

			pendingTags.clear();
		}
		else if (section == Section::Sound)
		{
			const std::vector<std::string> tokens = splitWhitespace(line);
			if (tokens.size() >= 2) inf.sounds[std::atoi(tokens[1].c_str())] = upper(tokens[0]);
		}
		else if (section == Section::Flats)
		{
			std::vector<std::string> tokens = splitWhitespace(line);
			if (line[0] == '*')
			{
				if (tokens.size() >= 2 && upper(tokens[0]) == "*ITEM") pendingItem = std::atoi(tokens[1].c_str());
				continue;
			}

			InfFlat flat;
			flat.filename = upper(tokens[0][0] == '-' ? tokens[0].substr(1) : tokens[0]);
			flat.itemId = pendingItem;
			pendingItem.reset();
			for (size_t i = 1; i < tokens.size(); i++)
			{
				const size_t colon = tokens[i].find(':');
				if (colon == std::string::npos) continue;
				const char type = static_cast<char>(std::toupper(static_cast<unsigned char>(tokens[i][0])));
				const int value = std::atoi(tokens[i].c_str() + colon + 1);
				if (type == 'F')
				{
					flat.collider = (value & 1) != 0;
					flat.largeScale = (value & 4) != 0;
					flat.transparent = (value & 16) != 0;
					flat.ceiling = (value & 32) != 0;
					flat.mediumScale = (value & 64) != 0;
				}
				else if (type == 'S') flat.lightRadius = value;
				else if (type == 'Y') flat.yOffset = value;
			}

			inf.flats.push_back(std::move(flat));
		}
	}

	if (inf.voxelTextures.empty())
	{
		outError = "Nenhuma textura no .INF.";
		return std::nullopt;
	}

	return inf;
}
