// Conversão de .MIF/.INF em nível jogável. Semântica dos voxels baseada no
// MapGeneration do OpenTESArena (MIT).

#include <algorithm>
#include <cmath>

#include "Level.h"
#include "SpriteOverrides.h"

namespace
{
	constexpr float ArenaUnits = 128.0f;
	constexpr int DryChasm = 0xC;
	constexpr int WetChasm = 0xD;
	constexpr int LavaChasm = 0xE;

	// Tabelas de plataformas elevadas de interiores (ACD.EXE, RaisedPlatforms.BoxArrays).
	constexpr int RaisedHeights[8] = { 0, 12, 25, 37, 50, 62, 75, 87 };
	constexpr int RaisedThicknesses[16] = { 6, 13, 19, 25, 31, 38, 44, 50, 56, 63, 69, 75, 81, 88, 94, 100 };

	bool endsWith(const std::string &s, const char *suffix)
	{
		const size_t n = std::char_traits<char>::length(suffix);
		return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
	}
}

uint32_t arena::newTextureId()
{
	static uint32_t nextId = 0;
	return ++nextId;
}

arena::Texture arena::toTexture(const IndexedImage &image, const Palette &palette)
{
	Texture texture;
	texture.width = image.width;
	texture.height = image.height;
	texture.pixels.resize(image.pixels.size());
	for (size_t i = 0; i < image.pixels.size(); i++)
	{
		const uint8_t index = image.pixels[i];
		const Color &c = palette[index];
		const uint32_t alpha = (index == 0) ? 0u : 255u;
		texture.hasTransparency |= (index == 0);
		texture.pixels[i] = (alpha << 24) | (static_cast<uint32_t>(c.b) << 16) | (static_cast<uint32_t>(c.g) << 8) | c.r;
	}

	// Vidro das janelas (índice 113 da paleta): a cor muda com a hora na hora de desenhar
	// (preto de dia, alaranjado aceso à noite, como no original). Marcado com alfa 254.
	if (!texture.hasTransparency)
	{
		for (size_t i = 0; i < image.pixels.size(); i++)
			if (image.pixels[i] == WindowPaletteIndex) texture.pixels[i] = 0xFE145A6Eu;
	}

	enhanceTexture(texture);
	texture.id = newTextureId();
	return texture;
}

arena::Texture arena::toTexture(const IndexedImage &image, const Palette &palette, const std::string &source, int frame, int frameCount)
{
	if (auto texture = findSpriteOverride(source, frame, frameCount, image))
	{
		// No tamanho original ainda ganha a ampliação HD; em 2x/4x só o relevo.
		if (texture->scale == 1) enhanceTexture(*texture);
		else addRelief(*texture);
		texture->id = newTextureId();
		return std::move(*texture);
	}

	return toTexture(image, palette);
}

int arena::Level::addTexture(const AssetManager &assets, const Palette &palette, const InfVoxelTexture &source)
{
	const auto key = std::make_pair(source.filename, source.setIndex);
	if (const auto it = this->textureIds.find(key); it != this->textureIds.end())
	{
		return it->second;
	}

	IndexedImage image;
	int frame = 0, frameCount = 1;
	if (const auto data = assets.read(source.filename))
	{
		if (endsWith(source.filename, ".SET"))
		{
			const auto frames = loadSet(*data);
			frameCount = static_cast<int>(frames.size());
			frame = std::clamp(source.setIndex, 0, std::max(0, frameCount - 1));
			if (!frames.empty()) image = frames[static_cast<size_t>(frame)];
		}
		else if (auto img = loadImg(source.filename, *data))
		{
			image = std::move(*img);
		}
	}

	if (image.pixels.empty())
	{
		// Textura ausente: xadrez para ficar evidente sem quebrar o nível.
		image.width = image.height = 8;
		image.pixels.resize(64);
		for (int i = 0; i < 64; i++) image.pixels[static_cast<size_t>(i)] = static_cast<uint8_t>(((i / 8 + i) % 2) ? 15 : 1);
	}

	const int id = static_cast<int>(this->textures.size());
	this->textures.push_back(toTexture(image, palette, source.filename, frame, frameCount));
	this->textureIds[key] = id;
	return id;
}

void arena::Level::loadLiquidFrames(const AssetManager &assets, const Palette &palette, const char *filename, std::vector<int> &outFrames)
{
	// As texturas de fosso do .INF (SWATER/SLAVA.IMG) são só marcadores: o original desenha
	// água e lava com estas animações cruas de 5 quadros de 320x100.
	constexpr int FrameWidth = 320, FrameHeight = 100;
	const auto data = assets.read(filename);
	if (!data) return;

	const size_t frameSize = static_cast<size_t>(FrameWidth * FrameHeight);
	for (size_t offset = 0; offset + frameSize <= data->size(); offset += frameSize)
	{
		IndexedImage image;
		image.width = FrameWidth;
		image.height = FrameHeight;
		image.pixels.assign(data->begin() + static_cast<std::ptrdiff_t>(offset), data->begin() + static_cast<std::ptrdiff_t>(offset + frameSize));
		Texture texture = toTexture(image, palette);
		for (uint32_t &p : texture.pixels) p |= 0xFF000000u;
		texture.hasTransparency = false;
		outFrames.push_back(static_cast<int>(this->textures.size()));
		this->textures.push_back(std::move(texture));
	}
}

int arena::Level::addSpriteFrames(const AssetManager &assets, const Palette &palette, const std::string &filename,
	std::vector<int> &outFrames, int &outW, int &outH)
{
	const auto data = assets.read(filename);
	if (!data)
	{
		return 0;
	}

	std::vector<IndexedImage> frames;
	if (endsWith(filename, ".DFA")) frames = loadDfa(*data);
	else if (auto img = loadImg(filename, *data)) frames.push_back(std::move(*img));

	for (size_t i = 0; i < frames.size(); i++)
	{
		const auto key = std::make_pair(filename, static_cast<int>(i));
		auto it = this->textureIds.find(key);
		if (it == this->textureIds.end())
		{
			it = this->textureIds.emplace(key, static_cast<int>(this->textures.size())).first;
			this->textures.push_back(toTexture(frames[i], palette, filename, static_cast<int>(i), static_cast<int>(frames.size())));
		}

		outFrames.push_back(it->second);
	}

	if (!frames.empty())
	{
		outW = frames[0].width;
		outH = frames[0].height;
	}

	return static_cast<int>(frames.size());
}

bool arena::Level::load(const AssetManager &assets, const Palette &palette, const std::string &mifName, std::string &outError)
{
	const auto mifData = assets.read(mifName);
	const auto mif = mifData ? loadMif(*mifData, outError) : std::nullopt;
	if (!mif)
	{
		if (outError.empty()) outError = mifName + " não encontrado.";
		return false;
	}

	return this->build(assets, palette, *mif, false, outError);
}

bool arena::Level::build(const AssetManager &assets, const Palette &palette, const MifFile &mifFile, bool isExterior, std::string &outError)
{
	const MifFile *mif = &mifFile;
	this->exterior = isExterior;
	const MifLevel &level = mif->levels[static_cast<size_t>(std::clamp(mif->startingLevel, 0, static_cast<int>(mif->levels.size()) - 1))];
	const auto infData = assets.read(level.info);
	const auto inf = infData ? loadInf(*infData, outError) : std::nullopt;
	if (!inf)
	{
		if (outError.empty()) outError = level.info + " não encontrado.";
		return false;
	}

	this->name = level.name;
	this->infName = toUpperAscii(level.info.substr(0, level.info.find('.')));
	this->texts = inf->texts;
	this->sounds = inf->sounds;
	for (const MifLevel::Trigger &t : level.triggers)
	{
		this->triggers.push_back({ t.y, t.x, t.textIndex, t.soundIndex }); // (coluna, linha) -> (X = linha, Z = coluna)
	}

	for (const MifLevel::Lock &lock : level.locks)
	{
		this->locks[{ lock.y, lock.x }] = lock.level;
	}
	this->width = mif->depth;  // X do mundo = linhas
	this->depth = mif->width;  // Z do mundo = colunas
	this->wallHeight = static_cast<float>(inf->ceilingHeight) / ArenaUnits;
	this->cells.assign(static_cast<size_t>(this->width * this->depth), Cell{});

	auto texture = [&](int infIndex)
	{
		if (inf->voxelTextures.empty()) return -1;
		const int clamped = (infIndex < 0 || infIndex >= static_cast<int>(inf->voxelTextures.size())) ? 0 : infIndex;
		return this->addTexture(assets, palette, inf->voxelTextures[static_cast<size_t>(clamped)]);
	};

	// *MENU pelo índice da textura (portas de prédios e saídas).
	auto menuOf = [&inf](int textureIndex)
	{
		for (const auto &[id, index] : inf->menus) if (index == textureIndex) return id;
		return -1;
	};

	this->ceilingTexture = texture(inf->ceilingTexture.value_or(0));
	this->loadLiquidFrames(assets, palette, "WATERANI.RCI", this->waterFrames);
	this->loadLiquidFrames(assets, palette, "LAVAANI.RCI", this->lavaFrames);

	const auto &[startCol, startRow] = mif->startPoints[0];
	this->startX = startRow / ArenaUnits;
	this->startZ = startCol / ArenaUnits;

	for (int row = 0; row < mif->depth; row++)
	{
		for (int col = 0; col < mif->width; col++)
		{
			const size_t src = static_cast<size_t>(col + row * mif->width);
			Cell &c = this->cells[static_cast<size_t>(row + col * this->width)];

			const uint16_t flor = level.flor.empty() ? 0 : level.flor[src];
			const int floorId = flor >> 8;
			if (floorId == DryChasm || floorId == WetChasm || floorId == LavaChasm)
			{
				c.chasm = true;
				c.chasmKind = (floorId == DryChasm) ? ChasmKind::Dry : (floorId == WetChasm) ? ChasmKind::Wet : ChasmKind::Lava;
				const std::optional<int> chasmIndex = (floorId == DryChasm) ? inf->dryChasm : (floorId == WetChasm) ? inf->wetChasm : inf->lavaChasm;
				c.chasmTexture = texture(chasmIndex.value_or(0));
				c.floorTexture = c.chasmTexture;
			}
			else
			{
				c.floorTexture = texture(floorId);
			}

			// MAP2 nos exteriores: andares de cima (1 a 4) com a textura própria.
			const uint16_t m2 = (this->exterior && !level.map2.empty()) ? level.map2[src] : 0;
			if (m2 != 0)
			{
				c.upperTexture = texture((m2 & 0x007F) - 1);
				c.upperStories = ((m2 & 0x0080) != 0) ? 2 : ((m2 & 0x8000) != 0) ? 3 : 1; // mesma ordem de testes do original
			}

			const uint16_t m1 = level.map1.empty() ? 0 : level.map1[src];
			if (m1 == 0)
			{
				continue;
			}

			const int nibble = (m1 & 0xF000) >> 12;
			if ((m1 & 0x8000) == 0)
			{
				const int msb = (m1 >> 8) & 0xFF;
				const int lsb = m1 & 0xFF;
				if (msb == lsb)
				{
					c.type = CellType::Wall;
					c.wallTexture = texture(msb - 1);
					c.isExit = inf->levelUp.has_value() && (msb - 1) == *inf->levelUp;
					c.isLevelDown = inf->levelDown.has_value() && (msb - 1) == *inf->levelDown;
					c.menu = menuOf(msb - 1);
				}
				else
				{
					c.type = CellType::Raised;
					const auto side = inf->boxSides.find(m1 & 0x000F);
					const auto cap = inf->boxCaps.find((m1 & 0x00F0) >> 4);
					c.wallTexture = texture(side != inf->boxSides.end() ? side->second : 0);
					c.capTexture = texture(cap != inf->boxCaps.end() ? cap->second : 0);
					const int heightIndex = msb & 0x07;
					const int thicknessIndex = (msb & 0x78) >> 3;
					int size = RaisedThicknesses[thicknessIndex];
					if (inf->boxScale) size = (size * *inf->boxScale) / 256;
					c.raisedBottom = RaisedHeights[heightIndex] / ArenaUnits;
					c.raisedTop = c.raisedBottom + size / ArenaUnits;
				}
			}
			else if (nibble == 0x8)
			{
				// Sprite (FLAT) no centro do voxel.
				const int flatIndex = m1 & 0x00FF;
				if (flatIndex >= static_cast<int>(inf->flats.size())) continue;
				const InfFlat &flat = inf->flats[static_cast<size_t>(flatIndex)];
				if (flat.itemId && *flat.itemId >= 32 && *flat.itemId <= 54)
				{
					// Criatura: animações .CFA e regras vêm do executável.
					this->creatureSpawns.push_back({ row + 0.5f, col + 0.5f, *flat.itemId - 32 });
					continue;
				}

				Sprite sprite;
				int w = 0, h = 0;
				if (this->addSpriteFrames(assets, palette, flat.filename, sprite.frames, w, h) == 0) continue;
				if (this->exterior && flat.lightRadius > 0 && flat.filename.size() > 4)
				{
					// Lampiões das ruas: a versão apagada (mesmo nome + "DA.IMG") aparece de dia.
					int dw = 0, dh = 0;
					this->addSpriteFrames(assets, palette, flat.filename.substr(0, flat.filename.size() - 4) + "DA.IMG", sprite.dayFrames, dw, dh);
				}
				// Dimensões estáticas: pixels * modificador / 128 (escala média/grande do .INF).
				const float modifier = flat.largeScale ? 3.0f * 128.0f / 100.0f : flat.mediumScale ? 1.5f * 128.0f / 100.0f : 1.0f;
				sprite.width = w * modifier / ArenaUnits;
				sprite.height = h * modifier / ArenaUnits;
				sprite.x = row + 0.5f;
				sprite.z = col + 0.5f;
				sprite.yBottom = -flat.yOffset / ArenaUnits;
				sprite.lightRadius = static_cast<float>(flat.lightRadius);
				sprite.collider = flat.collider;
				sprite.name = flat.filename;
				sprite.itemId = flat.itemId.value_or(-1);
				this->sprites.push_back(std::move(sprite));
			}
			else if (nibble == 0x9)
			{
				c.type = CellType::TransparentWall;
				c.wallTexture = texture((m1 & 0x00FF) - 1);
				c.collider = (m1 & 0x0100) == 0;
			}
			else if (nibble == 0xA)
			{
				c.type = CellType::Edge;
				c.wallTexture = texture(std::max(0, (m1 & 0x003F) - 1));
				c.menu = menuOf((m1 & 0x003F) - 1);
				c.collider = (m1 & 0x0100) != 0;
				// Orientação 0/4/8/C = -X/+Z/+X/-Z (já no sistema do mundo).
				const int orientation = (m1 & 0x00C0) >> 4;
				c.facing = (orientation == 0x0) ? Facing::NegX : (orientation == 0x4) ? Facing::PosZ : (orientation == 0x8) ? Facing::PosX : Facing::NegZ;
			}
			else if (nibble == 0xB)
			{
				c.type = CellType::Door;
				c.wallTexture = texture((m1 & 0x003F) - 1);
				this->doors[{ row, col }] = DoorState{};
			}
			else if (nibble == 0xD)
			{
				c.type = CellType::Diagonal;
				c.wallTexture = texture((m1 & 0x00FF) - 1);
				c.diagonalRight = (m1 & 0x0100) == 0;
			}
		}
	}

	return true;
}

const arena::Cell &arena::Level::cell(int x, int z) const
{
	static const Cell Solid = []() { Cell c; c.type = CellType::Wall; return c; }();
	return this->inBounds(x, z) ? this->cells[static_cast<size_t>(x + z * this->width)] : Solid;
}

float arena::Level::doorOpenness(int x, int z) const
{
	const auto it = this->doors.find({ x, z });
	return it != this->doors.end() ? it->second.open : 0.0f;
}

void arena::Level::toggleDoor(int x, int z)
{
	const auto it = this->doors.find({ x, z });
	if (it != this->doors.end())
	{
		it->second.target = (it->second.target > 0.5f) ? 0.0f : 1.0f;
	}
}

void arena::Level::update(double dt)
{
	constexpr float DoorSpeed = 1.6f; // aberturas por segundo
	for (auto &[pos, door] : this->doors)
	{
		const float step = static_cast<float>(dt) * DoorSpeed;
		if (door.open < door.target) door.open = std::min(door.target, door.open + step);
		else if (door.open > door.target) door.open = std::max(door.target, door.open - step);
	}
}

bool arena::Level::blocksMovement(int x, int z) const
{
	const Cell &c = this->cell(x, z);
	switch (c.type)
	{
	case CellType::Wall:
	case CellType::Diagonal: return true;
	case CellType::Door: return this->doorOpenness(x, z) < 0.9f;
	case CellType::TransparentWall:
	case CellType::Edge: return c.collider;
	case CellType::Raised: return c.raisedBottom > 0.25f || c.raisedTop > 0.45f; // degraus baixos podem ser subidos
	default: return false;
	}
}

float arena::Level::floorHeightAt(int x, int z) const
{
	const Cell &c = this->cell(x, z);
	if (c.type == CellType::Raised && c.raisedBottom <= 0.25f && c.raisedTop <= 0.45f) return c.raisedTop;
	if (c.chasm) return -0.25f;
	return 0.0f;
}
