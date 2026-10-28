#include <cstdlib>

#include "../core/KeyValueFile.h"
#include "Localization.h"

namespace
{
	std::string unescape(const std::string &s)
	{
		std::string out;
		for (size_t i = 0; i < s.size(); i++)
		{
			if (s[i] == '\\' && i + 1 < s.size())
			{
				const char next = s[++i];
				out.push_back(next == 'n' ? '\n' : next == 't' ? '\t' : next);
			}
			else
			{
				out.push_back(s[i]);
			}
		}

		return out;
	}
}

bool arena::Localization::loadLanguage(const std::filesystem::path &path)
{
	return parseKeyValueFile(path, [this](const std::string &key, const std::string &value)
	{
		this->translated[key] = unescape(value);
	});
}

bool arena::Localization::loadExeStringMap(const std::filesystem::path &path, const ByteBuffer &exeImage)
{
	return parseKeyValueFile(path, [this, &exeImage](const std::string &key, const std::string &value)
	{
		// Valor: "0xOFFSET" (string terminada em zero) ou "0xOFFSET,TAMANHO".
		char *end = nullptr;
		const unsigned long offset = std::strtoul(value.c_str(), &end, 16);
		const int fields = (end == value.c_str()) ? 0 : (*end == ',') ? 2 : 1;
		const unsigned long length = (fields == 2) ? std::strtoul(end + 1, nullptr, 10) : 0;
		if (fields < 1 || offset >= exeImage.size())
		{
			return;
		}

		std::string text;
		for (size_t i = offset; i < exeImage.size(); i++)
		{
			const char c = static_cast<char>(exeImage[i]);
			if ((fields == 2 && text.size() >= length) || (fields == 1 && c == '\0'))
			{
				break;
			}

			// O jogo usa '\r' como quebra de linha.
			text.push_back(c == '\r' ? '\n' : c);
		}

		this->original[key] = std::move(text);
	});
}

std::string arena::Localization::getOr(const std::string &key, const std::string &fallback) const
{
	return this->has(key) ? this->get(key) : fallback;
}

const std::string &arena::Localization::get(const std::string &key) const
{
	if (const auto it = this->translated.find(key); it != this->translated.end())
	{
		return it->second;
	}

	if (const auto it = this->original.find(key); it != this->original.end())
	{
		return it->second;
	}

	return this->missing.try_emplace(key, "[" + key + "]").first->second;
}
