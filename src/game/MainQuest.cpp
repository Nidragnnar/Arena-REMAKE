#include <algorithm>

#include "../render/Ui.h"
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
	return dungeon.localId == ClueDungeon ? character.questStep >= 2 : character.questStep >= 4;
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

std::string arena::quest::objective(const CharacterDraft &character, const ArenaData &data, const Localization &text, bool inPrison)
{
	if (inPrison) return text.get("Quest.ObjPrison");
	if (character.staffPieces >= PieceCount)
	{
		static const char *const Final[] = { "Quest.ObjDream", "Quest.ObjPalace", "Quest.ObjPalaceDown", "Quest.ObjJewel", "Quest.ObjDone" };
		return text.get(Final[std::clamp(character.questStep, 0, 4)]);
	}

	if (character.questStep == 0) return text.get(character.staffPieces == 0 && character.level < 2 ? "Quest.ObjLevel" : "Quest.ObjDream");
	const int province = targetProvince(character);
	const CityLocation *piece = data.getCity(province, PieceDungeon), *clue = data.getCity(province, ClueDungeon);
	const std::string provinceName = text.get("Provinces.Name" + std::to_string(province));
	const std::string pieceName = piece ? piece->name : std::string(), clueName = clue ? clue->name : std::string();
	if (character.questStep == 1) return ui::format(text.get("Quest.ObjRumor"), { pieceName, provinceName });
	if (character.questStep == 2) return ui::format(text.get("Quest.ObjClue"), { clueName, provinceName, text.get("Quest.Item" + std::to_string(character.staffPieces)) });
	if (character.questStep == 3)
	{
		const CityLocation *giver = nullptr;
		for (const CityLocation &c : data.getCities()) if (c.globalId == character.questGiver) giver = &c;
		return ui::format(text.get("Quest.ObjReturn"), { text.get("Quest.Item" + std::to_string(character.staffPieces)), giver ? giver->name : std::string(), provinceName });
	}
	return ui::format(text.get("Quest.ObjPiece"), { pieceName, provinceName });
}

void arena::quest::note(CharacterDraft &character, std::string text, std::string detail)
{
	character.journal.push_back({ character.gameMinutes, std::move(detail), std::move(text) });
}
