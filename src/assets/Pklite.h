#pragma once

#include <string>

#include "../core/Bytes.h"

namespace arena
{
	// Descompacta o executável DOS do Arena (A.EXE / ACD.EXE), comprimido com
	// PKLITE 1.x "extra compression". O resultado é a imagem de carga do programa
	// (sem cabeçalho MZ e sem relocações aplicadas), de onde o motor lê tabelas e
	// textos originais do jogo.
	//
	// Algoritmo baseado no ExeUnpacker do OpenTESArena (MIT) e em
	// pklite_specification.md.
	bool unpackPklite(const ByteBuffer &exe, ByteBuffer &outImage, std::string &outError);
}
