#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include "../core/Bytes.h"

namespace arena
{
	// Textos do jogo. Ordem de busca:
	//   1. arquivo de idioma (data/lang/pt_BR.txt), chave "Secao.Chave";
	//   2. texto original em inglês lido do executável via data/exe/acd_strings.txt;
	//   3. a própria chave entre colchetes (facilita achar o que falta traduzir).
	class Localization
	{
	public:
		bool loadLanguage(const std::filesystem::path &path);
		bool loadExeStringMap(const std::filesystem::path &path, const ByteBuffer &exeImage);

		const std::string &get(const std::string &key) const;
		std::string getOr(const std::string &key, const std::string &fallback) const;
		bool has(const std::string &key) const { return this->translated.contains(key) || this->original.contains(key); }
		bool hasTranslation(const std::string &key) const { return this->translated.contains(key); }

	private:
		std::unordered_map<std::string, std::string> translated;
		std::unordered_map<std::string, std::string> original;
		mutable std::unordered_map<std::string, std::string> missing;
	};
}
