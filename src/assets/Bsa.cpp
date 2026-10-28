#include "Bsa.h"

namespace
{
	constexpr size_t IndexRecordSize = 18;
	constexpr size_t NameSize = 12;
}

bool arena::BsaArchive::open(const std::filesystem::path &path, std::string &outError)
{
	std::optional<ByteBuffer> file = readWholeFile(path);
	if (!file || file->size() < 2)
	{
		outError = "Não foi possível ler " + path.string();
		return false;
	}

	this->data = std::move(*file);
	this->entries.clear();
	this->indexByName.clear();

	const size_t count = readLE16(this->data.data());
	if (this->data.size() < 2 + count * IndexRecordSize)
	{
		outError = "Índice do BSA truncado.";
		return false;
	}

	const uint8_t *record = this->data.data() + this->data.size() - count * IndexRecordSize;
	uint32_t offset = 2;
	for (size_t i = 0; i < count; i++, record += IndexRecordSize)
	{
		Entry entry;
		const char *namePtr = reinterpret_cast<const char*>(record);
		size_t nameLen = 0;
		while (nameLen < NameSize && namePtr[nameLen] != '\0')
		{
			nameLen++;
		}

		entry.name = toUpperAscii(std::string(namePtr, nameLen));
		entry.offset = offset;
		entry.size = readLE32(record + 14);
		offset += entry.size;

		if (static_cast<size_t>(entry.offset) + entry.size > this->data.size())
		{
			outError = "Entrada fora dos limites: " + entry.name;
			return false;
		}

		// Alguns nomes aparecem repetidos; o primeiro vence, como no jogo original.
		this->indexByName.try_emplace(entry.name, this->entries.size());
		this->entries.push_back(std::move(entry));
	}

	return true;
}

bool arena::BsaArchive::contains(const std::string &name) const
{
	return this->indexByName.contains(toUpperAscii(name));
}

std::optional<arena::ByteBuffer> arena::BsaArchive::read(const std::string &name) const
{
	const auto it = this->indexByName.find(toUpperAscii(name));
	if (it == this->indexByName.end())
	{
		return std::nullopt;
	}

	const Entry &entry = this->entries[it->second];
	const auto begin = this->data.begin() + entry.offset;
	return ByteBuffer(begin, begin + entry.size);
}
