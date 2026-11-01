#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../core/Bytes.h"

namespace arena
{
	// Textura de voxel: um .IMG ou uma das imagens 64x64 de um .SET.
	struct InfVoxelTexture
	{
		std::string filename;
		int setIndex = -1;
	};

	// Objeto decorativo/item (seção @FLATS).
	struct InfFlat
	{
		std::string filename;
		std::optional<int> itemId;
		int yOffset = 0;        // unidades do Arena (negativo = mais alto)
		int lightRadius = 0;    // "S:n" — alcance da luz em voxels
		bool collider = false;
		bool largeScale = false;
		bool mediumScale = false;
		bool transparent = false;
		bool ceiling = false;
	};

	// Texto de gatilho (@TEXT): descrição, chave ("+N") ou enigma ("^").
	struct InfText
	{
		std::string text;
		bool once = false;          // "~": mostrado só uma vez
		std::optional<int> keyId;   // "+N": o gatilho entrega a chave N
		// Enigma ("^A B"): pergunta, respostas aceitas (":"), texto do acerto ("`CORRECT") e do erro ("`WRONG").
		// A = trava aberta pelo acerto (255 = a porta do próprio enigma); B = castigo do erro (0 = nenhum).
		bool riddle = false;
		int riddleLock = 0, riddlePenalty = 0;
		std::vector<std::string> answers;
		std::string correct, wrong;
	};

	// Arquivo .INF: texturas e propriedades de um mapa. Os .INF dentro do GLOBAL.BSA
	// são cifrados (XOR com chave de 8 bytes + contador).
	struct InfFile
	{
		std::vector<InfVoxelTexture> voxelTextures;
		std::map<int, int> boxCaps;   // *BOXCAP n  -> índice de textura
		std::map<int, int> boxSides;  // *BOXSIDE n -> índice de textura
		int ceilingHeight = 100;      // *CEILING (unidades do Arena; 128 = 1 voxel)
		std::optional<int> boxScale;
		std::optional<int> ceilingTexture;
		std::optional<int> dryChasm, wetChasm, lavaChasm;
		std::optional<int> levelUp, levelDown;
		std::map<int, int> menus; // *MENU id -> índice da textura (entradas de prédios e saídas de interiores)
		std::vector<InfFlat> flats;
		std::map<int, std::string> sounds; // @SOUND: id -> arquivo .VOC
		std::map<int, InfText> texts;      // @TEXT: *TEXT n
	};

	std::optional<InfFile> loadInf(ByteBuffer data, std::string &outError);
}
