#pragma once

#include <filesystem>
#include <string>

namespace arena
{
	class Game;

	// Saves do Arena original (pasta do jogo): NAMES.DAT com o nome dos 10 espaços e SAVEENGN.0x com o
	// personagem. O começo do SAVEENGN vem embaralhado (XOR com ror do tamanho, como no OpenTESArena); a
	// ficha (NPCData, 1054 bytes) tem posições conhecidas, os dados do jogo (PlayerData) só em parte.
	namespace original
	{
		struct Summary { bool exists = false; std::string name; int level = 1; };
		Summary summary(const std::filesystem::path &arenaDir, int slot); // slot 1..10

		// Importa nome, raça, classe, sexo, rosto, nível, experiência, atributos, vida, magia, ouro,
		// feitiços e a data; começa diante do portão da cidade onde o personagem estava (ou da natal).
		// Itens e o progresso das missões não são importados (formato ainda não decifrado).
		// outMap = mapa de partida ("WILD:província:local").
		bool import(Game &game, const std::filesystem::path &arenaDir, int slot, std::string &outMap, std::string &outError);
	}
}
