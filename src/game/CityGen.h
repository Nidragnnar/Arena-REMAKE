#pragma once

#include <string>

#include "../world/MifFile.h"
#include "ArenaData.h"
#include "ArenaRandom.h"

namespace arena
{
	// Monta a cidade como o original: molde (CITY/TOWN/VILLAGE[W]n.MIF) + quarteirões de 20x20
	// sorteados pela semente da cidade (lojas, guilda, templo, taverna, casas nobres e casas).
	// outInf recebe o .INF do clima (TCN/DCN/MCN.INF, tempo limpo).
	bool generateCity(const AssetManager &assets, const CityGenTables &tables, const CityLocation &city, MifFile &outMif, std::string &outInf, std::string &outError);
}
