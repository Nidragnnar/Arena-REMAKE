#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../core/Bytes.h"

namespace arena
{
	// GLOBAL.BSA: arquivo-contêiner do Arena. Formato:
	//   uint16 quantidade, depois os dados de cada entrada em sequência,
	//   e no final um índice de N registros de 18 bytes
	//   (nome DOS de 12 bytes, uint16 flag de compressão, uint32 tamanho).
	class BsaArchive
	{
	public:
		struct Entry
		{
			std::string name;
			uint32_t offset = 0;
			uint32_t size = 0;
		};

		bool open(const std::filesystem::path &path, std::string &outError);

		const std::vector<Entry> &getEntries() const { return this->entries; }
		bool contains(const std::string &name) const;
		std::optional<ByteBuffer> read(const std::string &name) const;

	private:
		ByteBuffer data;
		std::vector<Entry> entries;
		std::unordered_map<std::string, size_t> indexByName;
	};
}
