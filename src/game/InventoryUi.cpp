// Regras de inventário e saque (nomes, pesos, equipar/usar, pegar) e o boneco vestido da ficha.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../render/Ui.h"
#include "InventoryUi.h"
#include "../world/SpriteOverrides.h"

namespace
{
	using namespace arena;

	std::string kilograms(float kg)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%.1f", kg);
		return buffer;
	}

	bool canWearArmor(const Game &game, const Item &item)
	{
		const int allowance = static_cast<int>(game.getData().getClass(std::max(0, game.getCharacter().classIndex)).armor);
		if (allowance == 3) return false;                    // nenhuma armadura (nem escudo)
		if (item.isShield()) return true;
		if (item.armorClass == ArmorClass::Plate) return allowance == 0;
		if (item.armorClass == ArmorClass::Chain) return allowance <= 1;
		return true;
	}
}

std::string arena::itemName(const Game &game, const Item &item)
{
	const Localization &text = game.getText();
	const ItemTables &tables = game.getData().getItems();
	const std::string material = text.getOr("Materials.Name" + std::to_string(item.material), tables.materialNames[static_cast<size_t>(item.material)]);
	switch (item.kind)
	{
	case ItemKind::Weapon:
		return text.getOr("Weapons.Name" + std::to_string(item.type), tables.weaponNames[static_cast<size_t>(item.type)]) + " (" + material + ")";
	case ItemKind::Armor:
	{
		std::string name = text.getOr("Armor.Piece" + std::to_string(item.type), tables.armorNames[static_cast<size_t>(item.type)]);
		if (!item.isShield()) name += " " + text.get("Armor.Class" + std::to_string(static_cast<int>(item.armorClass)));
		if (item.armorClass == ArmorClass::Plate || item.isShield()) name += " (" + material + ")";
		return name;
	}
	default:
		return text.getOr("Potions.Name" + std::to_string(item.type), tables.potionNames[static_cast<size_t>(item.type)]);
	}
}

std::string arena::itemDetails(const Game &game, const Item &item)
{
	const Localization &text = game.getText();
	const ItemTables &tables = game.getData().getItems();
	const std::string weight = kilograms(itemWeight(item, tables));
	switch (item.kind)
	{
	case ItemKind::Weapon:
		return ui::format(text.get("Items.WeaponDetails"), { std::to_string(weaponMinDamage(item, tables)), std::to_string(weaponMaxDamage(item, tables)), weight })
			+ (tables.weaponHands[static_cast<size_t>(item.type)] == 2 ? "  " + text.get("Items.TwoHanded") : std::string());
	case ItemKind::Armor:
		return ui::format(text.get("Items.ArmorDetails"), { std::to_string(armorRating(item, tables)), weight });
	default:
		return text.get("Items.PotionDetails");
	}
}

float arena::carriedWeight(const Game &game)
{
	float total = 0.0f;
	for (const Item &item : game.getCharacter().inventory) total += itemWeight(item, game.getData().getItems());
	return total;
}

float arena::maxCarryWeight(const Game &game)
{
	return static_cast<float>(game.getCharacter().attributes[0] * 2);
}

// ---------------------------------------------------------------------------

void arena::PaperDoll::load(Game &game)
{
	const CharacterDraft &c = game.getCharacter();
	const bool magic = game.getData().getClass(std::max(0, c.classIndex)).castsMagic;
	this->bodyName = std::string(c.male ? "CHARBK0" : "CHRBKF0") + std::to_string(c.raceIndex) + ".IMG";
	this->shirtName = c.male ? (magic ? "MRSHIRT.IMG" : "MSSHIRT.IMG") : (magic ? "FRSHIRT.IMG" : "FSSHIRT.IMG");
	this->pantsName = c.male ? "MPANTS.IMG" : "FPANTS.IMG";
	this->headsName = std::string(c.male ? "FACES1" : "FACESF1") + std::to_string(c.raceIndex) + ".CIF";
	this->equipmentName = c.male ? "0EQUIP.CIF" : "1EQUIP.CIF";
	this->body = game.loadImage(this->bodyName);
	this->shirt = game.loadImage(this->shirtName);
	this->pants = game.loadImage(this->pantsName);
	this->heads = game.loadFrames(this->headsName);
	this->equipment = game.loadFrames(this->equipmentName);
	this->shirtOffset = c.male ? (magic ? std::array<int, 2>{ 215, 35 } : std::array<int, 2>{ 186, 12 })
		: (magic ? std::array<int, 2>{ 220, 33 } : std::array<int, 2>{ 220, 35 });
	this->pantsOffset = c.male ? std::array<int, 2>{ 229, 82 } : std::array<int, 2>{ 212, 74 };
}

std::vector<arena::PaperDoll::Part> arena::PaperDoll::parts(const Game &game) const
{
	const CharacterDraft &c = game.getCharacter();
	std::vector<Part> out;
	out.push_back({ &this->body, this->bodyName, 0, 1, Canvas::Width - this->body.width, 0 });
	out.push_back({ &this->pants, this->pantsName, 0, 1, this->pantsOffset[0], this->pantsOffset[1] });
	out.push_back({ &this->shirt, this->shirtName, 0, 1, this->shirtOffset[0], this->shirtOffset[1] });

	// Índices no 0EQUIP/1EQUIP.CIF: 0..17 armas, 18 + (0 placas | 11 malha | 18 couro) + peça; escudos 25..28.
	auto frameFor = [](const Item &item) -> int
	{
		if (item.kind == ItemKind::Weapon) return item.type;
		if (item.isShield()) return 18 + item.type;
		const int classOffset = (item.armorClass == ArmorClass::Plate) ? 0 : (item.armorClass == ArmorClass::Chain) ? 11 : 18;
		return 18 + classOffset + item.type;
	};

	const int equipmentCount = static_cast<int>(this->equipment.size());
	auto addFrame = [&](int index)
	{
		if (index < 0 || index >= equipmentCount) return;
		const IndexedImage &frame = this->equipment[static_cast<size_t>(index)];
		out.push_back({ &frame, this->equipmentName, index, equipmentCount, frame.offsetX, frame.offsetY });
	};

	// Ordem: pernas, pés, peito, ombros, mãos, cabeça (rosto + elmo), escudo, arma.
	constexpr int PieceOrder[] = { 2, 6, 0, 3, 4, 1 };
	for (int piece : PieceOrder)
		for (const Item &item : c.inventory)
			if (item.equipped && item.kind == ItemKind::Armor && item.type == piece) addFrame(frameFor(item));

	if (!this->heads.empty())
	{
		const int index = c.portraitIndex % static_cast<int>(this->heads.size());
		const IndexedImage &head = this->heads[static_cast<size_t>(index)];
		out.push_back({ &head, this->headsName, index, static_cast<int>(this->heads.size()), head.offsetX, head.offsetY });
	}

	for (const Item &item : c.inventory)
		if (item.equipped && item.kind == ItemKind::Armor && item.type == 5) addFrame(frameFor(item));
	for (const Item &item : c.inventory)
		if (item.equipped && item.isShield()) addFrame(frameFor(item));
	for (const Item &item : c.inventory)
		if (item.equipped && item.kind == ItemKind::Weapon) addFrame(frameFor(item));
	return out;
}

void arena::PaperDoll::draw(Canvas &canvas, const Game &game) const
{
	for (const Part &part : this->parts(game)) canvas.blit(*part.image, part.x, part.y);
}

void arena::PaperDoll::compose(const Game &game, std::vector<uint32_t> &pixels, int &width, int &height) const
{
	// Cada peça em RGBA (PNG redesenhado ou original com a paleta da ficha).
	const Palette palette = game.loadPalette("CHARSHT.COL").value_or(game.getDefaultPalette());
	const std::vector<Part> list = this->parts(game);
	std::vector<RgbaImage> images;
	int scale = 1;
	for (const Part &part : list)
	{
		images.push_back(loadRgba(*part.image, palette, part.source, part.frame, part.frameCount));
		scale = std::max(scale, images.back().scale);
	}

	// Monta no 320x200 ampliado pela maior escala; peças menores são ampliadas por vizinho.
	const int W = Canvas::Width * scale, H = Canvas::Height * scale;
	std::vector<uint32_t> canvas(static_cast<size_t>(W) * H, 0u);
	for (size_t k = 0; k < list.size(); k++)
	{
		const RgbaImage &img = images[k];
		const int w = list[k].image->width * scale, h = list[k].image->height * scale;
		for (int y = 0; y < h; y++)
		{
			const int ty = list[k].y * scale + y;
			if (ty < 0 || ty >= H) continue;
			const int sy = y * img.scale / scale;
			for (int x = 0; x < w; x++)
			{
				const int tx = list[k].x * scale + x;
				if (tx < 0 || tx >= W) continue;
				const uint32_t p = img.pixels[static_cast<size_t>(sy) * img.width + x * img.scale / scale];
				const uint32_t a = p >> 24;
				uint32_t &d = canvas[static_cast<size_t>(ty) * W + tx];
				if (a == 255) d = p;
				else if (a != 0)
				{
					// Borda suave do PNG sobre o que já foi desenhado.
					auto mix = [&](int shift) { return ((((p >> shift) & 0xFF) * a + ((d >> shift) & 0xFF) * (255 - a)) / 255) << shift; };
					d = 0xFF000000u | mix(16) | mix(8) | mix(0);
				}
			}
		}
	}

	// Recorta só as colunas com desenho (alguns fundos têm uma faixa vazia à esquerda).
	int x0 = (Canvas::Width - std::clamp(this->body.width, 1, Canvas::Width)) * scale;
	auto columnUsed = [&](int x) { for (int y = 0; y < H; y++) if ((canvas[static_cast<size_t>(y) * W + x] >> 24) != 0) return true; return false; };
	while (x0 < W - 1 && !columnUsed(x0)) x0++;
	width = W - x0;
	height = H;
	pixels.resize(static_cast<size_t>(width) * height);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < width; x++)
			pixels[static_cast<size_t>(y) * width + x] = canvas[static_cast<size_t>(y) * W + x0 + x] | 0xFF000000u;
}


// ---------------------------------------------------------------------------
// Regras

arena::OverlayResult arena::useInventoryItem(Game &game, int index, int currentHealth, int currentSpellPoints, std::string &notice)
{
	OverlayResult result;
	CharacterDraft &c = game.getCharacter();
	if (index < 0 || index >= static_cast<int>(c.inventory.size())) return result;

	const Localization &text = game.getText();
	const ItemTables &tables = game.getData().getItems();
	Item &item = c.inventory[static_cast<size_t>(index)];
	result.inventoryChanged = true;

	if (item.kind == ItemKind::Potion)
	{
		// Poção de Cura recupera metade da vida; Cura Verdadeira, tudo; Restaurar Magia, metade da magia.
		if (item.type == 2 || item.type == 8)
		{
			const int heal = (item.type == 8) ? c.maxHealth : std::max(1, c.maxHealth / 2);
			result.healthDelta = std::min(heal, c.maxHealth - currentHealth);
			result.message = ui::format(text.get("Items.PotionHealed"), { std::to_string(result.healthDelta) });
		}
		else if (item.type == 3 && c.maxSpellPoints > 0)
		{
			result.spellPointsDelta = std::min(std::max(1, c.maxSpellPoints / 2), c.maxSpellPoints - currentSpellPoints);
			result.message = ui::format(text.get("Items.PotionRestoredMagic"), { std::to_string(result.spellPointsDelta) });
		}
		else
		{
			result.message = text.get("Items.PotionNoEffect");
		}

		c.inventory.erase(c.inventory.begin() + index);
		game.playSound("CLICKS.VOC", 0.5f);
		return result;
	}

	if (item.equipped)
	{
		item.equipped = false;
		return result;
	}

	if (item.kind == ItemKind::Weapon && item.type >= 16)
	{
		result.inventoryChanged = false;
		notice = text.get("Items.BowsNotSupported");
		return result;
	}

	if (item.kind == ItemKind::Armor && !canWearArmor(game, item))
	{
		result.inventoryChanged = false;
		notice = text.get("Items.ClassForbidden");
		return result;
	}

	// Um item por parte do corpo; arma de duas mãos não combina com escudo.
	const bool twoHanded = item.kind == ItemKind::Weapon && tables.weaponHands[static_cast<size_t>(item.type)] == 2;
	for (Item &other : c.inventory)
	{
		if (&other == &item || !other.equipped) continue;
		const bool sameSlot = (item.kind == ItemKind::Weapon && other.kind == ItemKind::Weapon)
			|| (item.isShield() && other.isShield())
			|| (item.kind == ItemKind::Armor && !item.isShield() && !other.isShield() && other.kind == ItemKind::Armor && bodyPartOf(other) == bodyPartOf(item));
		const bool conflictTwoHanded = (twoHanded && other.isShield())
			|| (item.isShield() && other.kind == ItemKind::Weapon && tables.weaponHands[static_cast<size_t>(other.type)] == 2);
		if (sameSlot || conflictTwoHanded) other.equipped = false;
	}

	item.equipped = true;
	game.playSound("CLANK.VOC", 0.4f);
	return result;
}

arena::OverlayResult arena::takeLoot(Game &game, Loot &loot, int row, std::string &notice)
{
	OverlayResult result;
	CharacterDraft &c = game.getCharacter();
	const int goldRows = (loot.gold > 0) ? 1 : 0;
	if (row < goldRows)
	{
		c.gold += loot.gold;
		result.message = ui::format(game.getText().get("Loot.TookGold"), { std::to_string(loot.gold) });
		loot.gold = 0;
		result.inventoryChanged = true;
		game.playSound("CLICKS.VOC", 0.6f);
		return result;
	}

	const int itemIndex = row - goldRows;
	if (itemIndex < 0 || itemIndex >= static_cast<int>(loot.items.size())) return result;
	Item item = loot.items[static_cast<size_t>(itemIndex)];
	if (carriedWeight(game) + itemWeight(item, game.getData().getItems()) > maxCarryWeight(game))
	{
		notice = game.getText().get("Loot.TooHeavy");
		return result;
	}

	item.equipped = false;
	c.inventory.push_back(item);
	loot.items.erase(loot.items.begin() + itemIndex);
	result.inventoryChanged = true;
	result.message = ui::format(game.getText().get("Loot.TookItem"), { itemName(game, item) });
	game.playSound("CLANK.VOC", 0.5f);
	return result;
}

std::string arena::kilogramsText(float kg)
{
	return kilograms(kg);
}
