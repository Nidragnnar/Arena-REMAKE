#pragma once

#include <filesystem>
#include <fstream>
#include <string>

namespace arena
{
	inline std::string trimWhitespace(const std::string &s)
	{
		const size_t begin = s.find_first_not_of(" \t\r\n");
		if (begin == std::string::npos) return {};
		const size_t end = s.find_last_not_of(" \t\r\n");
		return s.substr(begin, end - begin + 1);
	}

	// Lê um arquivo "Chave=Valor" com seções [Secao] e comentários "#", chamando
	// onEntry("Secao.Chave", valor) para cada linha. Aceita BOM UTF-8.
	template<typename Fn>
	bool parseKeyValueFile(const std::filesystem::path &path, Fn &&onEntry)
	{
		std::ifstream file(path);
		if (!file)
		{
			return false;
		}

		std::string section;
		std::string line;
		bool first = true;
		while (std::getline(file, line))
		{
			if (first && line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
			{
				line.erase(0, 3);
			}

			first = false;
			const std::string trimmed = trimWhitespace(line);
			if (trimmed.empty() || trimmed[0] == '#')
			{
				continue;
			}

			if (trimmed.front() == '[' && trimmed.back() == ']')
			{
				section = trimmed.substr(1, trimmed.size() - 2);
				continue;
			}

			const size_t eq = trimmed.find('=');
			if (eq == std::string::npos)
			{
				continue;
			}

			const std::string key = trimWhitespace(trimmed.substr(0, eq));
			onEntry(section.empty() ? key : section + "." + key, trimWhitespace(trimmed.substr(eq + 1)));
		}

		return true;
	}
}
