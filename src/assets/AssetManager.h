#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

#include "Bsa.h"

namespace arena
{
	// Sistema de arquivos virtual: procura primeiro na pasta de mods (permite
	// substituir qualquer asset original), depois nos arquivos soltos da pasta do
	// jogo e por fim dentro do GLOBAL.BSA. Nomes são comparados sem diferenciar
	// maiúsculas/minúsculas, como no DOS.
	class AssetManager
	{
	public:
		bool init(const std::filesystem::path &arenaDir, const std::filesystem::path &modDir, std::string &outError);

		std::optional<ByteBuffer> read(const std::string &name) const;
		bool exists(const std::string &name) const;

		// Imagem descompactada do executável (textos e tabelas do jogo original).
		const ByteBuffer &getExeImage() const { return this->exeImage; }

	private:
		void indexDirectory(const std::filesystem::path &dir, std::unordered_map<std::string, std::filesystem::path> &index);

		std::unordered_map<std::string, std::filesystem::path> modFiles;
		std::unordered_map<std::string, std::filesystem::path> looseFiles;
		BsaArchive globalBsa;
		ByteBuffer exeImage;
	};
}
