#include "AssetManager.h"
#include "Pklite.h"

void arena::AssetManager::indexDirectory(const std::filesystem::path &dir, std::unordered_map<std::string, std::filesystem::path> &index)
{
	std::error_code ec;
	if (!std::filesystem::is_directory(dir, ec))
	{
		return;
	}

	for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
	{
		if (entry.is_regular_file())
		{
			index[toUpperAscii(entry.path().filename().string())] = entry.path();
		}
	}
}

bool arena::AssetManager::init(const std::filesystem::path &arenaDir, const std::filesystem::path &modDir, std::string &outError)
{
	this->indexDirectory(arenaDir, this->looseFiles);
	this->indexDirectory(modDir, this->modFiles);

	const auto bsaIt = this->looseFiles.find("GLOBAL.BSA");
	if (bsaIt == this->looseFiles.end())
	{
		outError = "GLOBAL.BSA não encontrado em " + arenaDir.string();
		return false;
	}

	if (!this->globalBsa.open(bsaIt->second, outError))
	{
		return false;
	}

	// A versão CD usa ACD.EXE; a versão em disquete, A.EXE.
	for (const char *exeName : { "ACD.EXE", "A.EXE" })
	{
		const auto exeIt = this->looseFiles.find(exeName);
		if (exeIt == this->looseFiles.end())
		{
			continue;
		}

		const auto exe = readWholeFile(exeIt->second);
		if (exe && unpackPklite(*exe, this->exeImage, outError))
		{
			return true;
		}
	}

	outError = "Executável do Arena (ACD.EXE) não encontrado ou inválido. " + outError;
	return false;
}

std::optional<arena::ByteBuffer> arena::AssetManager::read(const std::string &name) const
{
	const std::string key = toUpperAscii(name);
	if (const auto it = this->modFiles.find(key); it != this->modFiles.end())
	{
		return readWholeFile(it->second);
	}

	if (const auto it = this->looseFiles.find(key); it != this->looseFiles.end())
	{
		return readWholeFile(it->second);
	}

	return this->globalBsa.read(key);
}

bool arena::AssetManager::exists(const std::string &name) const
{
	const std::string key = toUpperAscii(name);
	return this->modFiles.contains(key) || this->looseFiles.contains(key) || this->globalBsa.contains(key);
}
