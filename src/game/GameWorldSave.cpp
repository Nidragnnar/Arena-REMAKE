// Estado do mundo nos saves: posição, portas, trancas, chaves, gatilhos, itens recolhidos,
// criaturas e saques. Gravado junto da ficha ([World], [Creatures], [Loot]).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "GameWorldScreen.h"

namespace
{
	using namespace arena;

	std::vector<std::string> split(const std::string &s, char separator)
	{
		std::vector<std::string> parts;
		std::string current;
		for (char ch : s)
		{
			if (ch == separator) { parts.push_back(current); current.clear(); }
			else current.push_back(ch);
		}
		if (!current.empty()) parts.push_back(current);
		return parts;
	}

	std::string encodeItems(const std::vector<Item> &items)
	{
		std::string out;
		for (const Item &item : items)
		{
			if (!out.empty()) out += ';';
			out += std::to_string(static_cast<int>(item.kind)) + "." + std::to_string(item.type) + "." + std::to_string(static_cast<int>(item.armorClass))
				+ "." + std::to_string(item.material);
		}
		return out;
	}

	std::vector<Item> decodeItems(const std::string &text)
	{
		std::vector<Item> items;
		for (const std::string &entry : split(text, ';'))
		{
			int kind = 0, type = 0, armorClass = 0, material = 0;
			if (std::sscanf(entry.c_str(), "%d.%d.%d.%d", &kind, &type, &armorClass, &material) != 4) continue;
			Item item;
			item.kind = static_cast<ItemKind>(std::clamp(kind, 0, 2));
			item.type = type;
			item.armorClass = static_cast<ArmorClass>(std::clamp(armorClass, 0, 2));
			item.material = std::clamp(material, 0, MaterialCount - 1);
			items.push_back(item);
		}
		return items;
	}

	std::string number(float v)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%.3f", v);
		return buffer;
	}
}

std::string arena::GameWorldScreen::serializeWorld() const
{
	std::ostringstream out;
	out << "[World]\n# mapa, posição e estado do nível\nMap=" << this->mifName << "\nX=" << number(this->x) << "\nZ=" << number(this->z)
		<< "\nYaw=" << number(this->yaw) << "\nPitch=" << number(this->pitch) << "\nShield=" << this->shieldPoints
		<< "\nEscaped=" << (this->escaped ? 1 : 0) << "\nKeys=";
	bool first = true;
	for (int key : this->keys) { out << (first ? "" : ",") << key; first = false; }
	out << "\nTriggers=";
	first = true;
	for (size_t trigger : this->firedTriggers) { out << (first ? "" : ",") << trigger; first = false; }
	out << "\nLocks=";
	first = true;
	for (const auto &[pos, lock] : this->level.getLocks()) { out << (first ? "" : ";") << pos.first << ":" << pos.second << ":" << lock; first = false; }
	out << "\nDoors=";
	first = true;
	for (const auto &[dx, dz] : this->level.getOpenDoors()) { out << (first ? "" : ";") << dx << ":" << dz; first = false; }
	out << "\nRemoved=";
	first = true;
	for (const auto &[rx, rz, name] : this->removedSprites) { out << (first ? "" : ";") << number(rx) << ":" << number(rz) << ":" << name; first = false; }

	out << "\n\n[Creatures]\n# vida, x, z, morta\n";
	const auto &all = this->creatures.getCreatures();
	for (size_t i = 0; i < all.size(); i++)
	{
		const Creature &c = all[i];
		if (this->isWild()) break; // o ermo sorteia criaturas de novo a cada visita
		const bool dead = c.state == Creature::State::Dead || c.state == Creature::State::Dying;
		out << "C" << i << "=" << c.hp << "," << number(c.x) << "," << number(c.z) << "," << (dead ? 1 : 0) << "\n";
	}

	out << "\n[Loot]\n# baús/pilhas (célula) e corpos (criatura): ouro | itens tipo.id.classe.material\n";
	for (const auto &[cell, loot] : this->containerLoot)
	{
		out << "Box" << cell.first << "_" << cell.second << "=" << loot.gold << "|" << encodeItems(loot.items) << "\n";
	}
	for (const auto &[creature, loot] : this->corpseLoot)
	{
		out << "Corpse" << creature << "=" << loot.gold << "|" << encodeItems(loot.items) << "\n";
	}

	return out.str();
}

void arena::GameWorldScreen::restoreWorld(const std::vector<std::pair<std::string, std::string>> &entries)
{
	for (const auto &[key, value] : entries)
	{
		if (key == "World.X") this->x = static_cast<float>(std::atof(value.c_str()));
		else if (key == "World.Z") this->z = static_cast<float>(std::atof(value.c_str()));
		else if (key == "World.Yaw") this->yaw = static_cast<float>(std::atof(value.c_str()));
		else if (key == "World.Pitch") this->pitch = static_cast<float>(std::atof(value.c_str()));
		else if (key == "World.Shield") this->shieldPoints = std::atoi(value.c_str());
		else if (key == "World.Escaped") this->escaped = std::atoi(value.c_str()) != 0;
		else if (key == "World.Keys") { this->keys.clear(); for (const std::string &k : split(value, ',')) this->keys.insert(std::atoi(k.c_str())); }
		else if (key == "World.Triggers") { this->firedTriggers.clear(); for (const std::string &t : split(value, ',')) this->firedTriggers.insert(static_cast<size_t>(std::atoll(t.c_str()))); }
		else if (key == "World.Locks")
		{
			std::map<std::pair<int, int>, int> locks;
			for (const std::string &entry : split(value, ';'))
			{
				int lx = 0, lz = 0, lock = 0;
				if (std::sscanf(entry.c_str(), "%d:%d:%d", &lx, &lz, &lock) == 3) locks[{ lx, lz }] = lock;
			}
			this->level.setLocks(std::move(locks));
		}
		else if (key == "World.Doors")
		{
			for (const std::string &entry : split(value, ';'))
			{
				int dx = 0, dz = 0;
				if (std::sscanf(entry.c_str(), "%d:%d", &dx, &dz) == 2) this->level.setDoorOpen(dx, dz, true);
			}
		}
		else if (key == "World.Removed")
		{
			for (const std::string &entry : split(value, ';'))
			{
				const std::vector<std::string> parts = split(entry, ':');
				if (parts.size() < 3) continue;
				const float rx = static_cast<float>(std::atof(parts[0].c_str())), rz = static_cast<float>(std::atof(parts[1].c_str()));
				const auto &sprites = this->level.getSprites();
				for (size_t i = 0; i < sprites.size(); i++)
				{
					if (sprites[i].name == parts[2] && std::fabs(sprites[i].x - rx) < 0.01f && std::fabs(sprites[i].z - rz) < 0.01f)
					{
						this->level.removeSprite(i);
						break;
					}
				}
				this->removedSprites.emplace_back(rx, rz, parts[2]);
			}
		}
		else if (key.rfind("Creatures.C", 0) == 0)
		{
			int hp = 0, dead = 0;
			float cx = 0, cz = 0;
			if (std::sscanf(value.c_str(), "%d,%f,%f,%d", &hp, &cx, &cz, &dead) == 4) this->creatures.restore(std::atoi(key.c_str() + 11), hp, cx, cz, dead != 0);
		}
		else if (key.rfind("Loot.Box", 0) == 0 || key.rfind("Loot.Corpse", 0) == 0)
		{
			const size_t bar = value.find('|');
			Loot loot;
			loot.gold = std::atoi(value.c_str());
			if (bar != std::string::npos) loot.items = decodeItems(value.substr(bar + 1));
			if (key.rfind("Loot.Box", 0) == 0)
			{
				int bx = 0, bz = 0;
				if (std::sscanf(key.c_str() + 8, "%d_%d", &bx, &bz) == 2) this->containerLoot[{ bx, bz }] = loot;
			}
			else
			{
				this->corpseLoot[std::atoi(key.c_str() + 11)] = loot;
			}
		}
	}

	this->lastCellX = static_cast<int>(std::floor(this->x));
	this->lastCellZ = static_cast<int>(std::floor(this->z));
	this->eyeOffset = this->level.floorHeightAt(this->lastCellX, this->lastCellZ);
}

bool arena::GameWorldScreen::saveToSlot(int slot)
{
	CharacterDraft &character = this->game.getCharacter();
	character.health = this->health;
	character.experience = this->experience;
	character.spellPoints = this->spellPoints;
	const std::string location = this->currentLocation();
	const bool ok = this->game.saveCharacter(this->game.getSlotPath(slot), this->serializeWorld(), location);
	this->showMessage(this->game.getText().get(ok ? "Saves.Saved" : "Saves.Failed"), 2.5);
	return ok;
}
