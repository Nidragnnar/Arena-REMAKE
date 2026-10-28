#include "Game.h"
#include "MainQuest.h"

int arena::quest::targetProvince(const CharacterDraft &character)
{
	return character.staffPieces < PieceCount ? StaffProvinces[character.staffPieces] : -1;
}

bool arena::quest::dungeonKnown(const CharacterDraft &character, const CityLocation &dungeon)
{
	if (!dungeon.mainQuest()) return true;
	for (int i = 0; i < character.staffPieces && i < PieceCount; i++)
		if (StaffProvinces[i] == dungeon.province) return true; // já percorridas
	if (dungeon.province != targetProvince(character)) return false;
	return dungeon.localId == ClueDungeon ? character.questStep >= 2 : character.questStep >= 3;
}

bool arena::quest::dreamDue(const CharacterDraft &character)
{
	if (character.questStep != 0 || character.staffPieces > PieceCount) return false;
	return character.staffPieces > 0 || character.level >= 2;
}

std::string arena::quest::premadeMif(const CityLocation &dungeon)
{
	return std::to_string(dungeon.dungeonSeed()).substr(0, 8) + ".MIF";
}
