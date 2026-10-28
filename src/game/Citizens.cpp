#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Citizens.h"

namespace
{
	constexpr float Pi = 3.14159265f;
	constexpr float TwoPi = 2.0f * Pi;
	constexpr float ArenaUnits = 128.0f;
	constexpr float CitizenRadius = 0.25f;
	constexpr float WalkSpeed = 1.15f; // voxels por segundo

	constexpr int WalkFrames[] = { 0, 1, 2, 3, 4, 5 };
	constexpr int IdleFrames[] = { 6, 7, 8 };
	constexpr double WalkFps = 12.0, IdleFps = 3.0;

	// Cores das roupas (blocos de 10 cores da paleta) e da pele, do ACD.EXE (CitizenColorBase /
	// CitizenSkinColors), e o bloco de pele de cada raça (-1 = sem troca).
	constexpr uint8_t ColorBase[16] = { 32, 42, 52, 64, 74, 84, 100, 116, 128, 138, 148, 160, 170, 180, 202, 212 };
	constexpr uint8_t SkinColors[10] = { 224, 226, 227, 229, 231, 232, 234, 235, 236, 237 };
	constexpr int RaceSkinOffsets[9] = { -1, 148, -1, 52, 192, -1, -1, 116, 148 };

	// Troca de cores do morador pela semente (mesmo algoritmo do original).
	std::array<uint8_t, 256> citizenColors(int raceIndex, uint16_t seed)
	{
		std::array<uint8_t, 256> map{};
		for (int i = 0; i < 256; i++) map[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
		uint16_t value = seed & 0x7FFF;
		for (const uint8_t dest : ColorBase)
		{
			const bool flag = (value & 0x8000) != 0;
			value = static_cast<uint16_t>((value << 1) | (value >> 15));
			if (!flag) continue;
			const uint8_t block = value & 0xF;
			if (dest == 128 && block == 11) continue; // sem cabelo verde
			for (int j = 0; j < 10; j++) map[static_cast<size_t>(dest + j)] = static_cast<uint8_t>(ColorBase[block] + j);
		}

		const int offset = RaceSkinOffsets[std::clamp(raceIndex, 0, 8)];
		if (offset >= 0)
			for (int i = 0; i < 10; i++) map[SkinColors[i]] = static_cast<uint8_t>(offset + i);
		return map;
	}
}

bool arena::CitizenSystem::walkable(const Level &level, int cx, int cz) const
{
	if (!level.inBounds(cx, cz) || level.blocksMovement(cx, cz)) return false;
	const Cell &c = level.cell(cx, cz);
	return !c.chasm && c.type == CellType::Empty && c.upperStories == 0;
}

void arena::CitizenSystem::init(const AssetManager &assets, const Palette &palette, const ArenaData &, const Level &level, const CityLocation &city, std::mt19937 &rng)
{
	this->textures.clear();
	this->looks.clear();
	this->citizens.clear();

	// Arquivos pelo clima (ordem das tabelas do executável).
	const char *male = city.climate == CityLocation::Climate::Mountain ? "MLGEN0@W.CFA" : city.climate == CityLocation::Climate::Desert ? "MLGEN0@D.CFA" : "MLGEN0@T.CFA";
	const char *female = city.climate == CityLocation::Climate::Mountain ? "FMGENW0@.CFA" : city.climate == CityLocation::Climate::Desert ? "FMGEND0@.CFA" : "FMGEN0@.CFA";
	constexpr int Variants = 3; // roupas diferentes por gênero
	for (int gender = 0; gender < 2; gender++)
	{
		const std::string pattern = gender == 0 ? male : female;
		for (int variant = 0; variant < Variants; variant++)
		{
			// Cores fixas por cidade (as mesmas a cada visita).
			const auto colors = citizenColors(city.province, static_cast<uint16_t>(city.seed() * 7143469u + static_cast<uint32_t>(gender * Variants + variant) * 40503u));
			Look look;
			look.male = gender == 0;
			bool ok = true;
			for (int dir = 1; dir <= 5 && ok; dir++)
			{
				std::string name = pattern;
				name[name.find('@')] = static_cast<char>('0' + dir);
				const auto data = assets.read(name);
				if (!data) { ok = false; break; }
				const std::vector<IndexedImage> images = loadCfa(*data);
				for (size_t i = 0; i < images.size(); i++)
				{
					IndexedImage image = images[i];
					for (uint8_t &p : image.pixels) p = colors[p];
					Frame frame;
					frame.texture = static_cast<int>(this->textures.size());
					frame.width = image.width / ArenaUnits;
					frame.height = (image.height * 200.0f / 256.0f) / ArenaUnits;
					this->textures.push_back(toTexture(image, palette, name, static_cast<int>(i), static_cast<int>(images.size())));
					look.directions[static_cast<size_t>(dir - 1)].push_back(frame);
				}
				ok = !look.directions[static_cast<size_t>(dir - 1)].empty();
			}

			if (ok) this->looks.push_back(std::move(look));
		}
	}

	if (this->looks.empty()) return;

	// Espalha os moradores pelas ruas (mais nas cidades-estado).
	const int count = city.type == CityLocation::Type::CityState ? 110 : city.type == CityLocation::Type::Town ? 75 : 45;
	for (int tries = 0; tries < count * 60 && static_cast<int>(this->citizens.size()) < count; tries++)
	{
		const int cx = static_cast<int>(rng() % static_cast<uint32_t>(level.getWidth()));
		const int cz = static_cast<int>(rng() % static_cast<uint32_t>(level.getDepth()));
		if (!this->walkable(level, cx, cz)) continue;
		Citizen citizen;
		citizen.x = cx + 0.5f;
		citizen.z = cz + 0.5f;
		citizen.angle = static_cast<float>(rng() % 4u) * (Pi / 2.0f);
		citizen.look = static_cast<int>(rng() % this->looks.size());
		citizen.walking = (rng() % 4u) != 0;
		citizen.nightOwl = (rng() % 4u) == 0;
		citizen.timer = 2.0 + (rng() % 600u) / 100.0;
		this->citizens.push_back(citizen);
	}
}

void arena::CitizenSystem::update(double dt, const Level &level, float playerX, float playerZ, std::mt19937 &rng)
{
	const float step = static_cast<float>(dt) * WalkSpeed;
	for (Citizen &c : this->citizens)
	{
		if (this->hidden(c)) continue;
		c.stateTime += dt;
		c.timer -= dt;
		if (c.talkTimer > 0.0)
		{
			// Depois da conversa, volta a andar nas direções da rua.
			c.talkTimer -= dt;
			if (c.talkTimer <= 0.0) c.angle = std::round(c.angle / (Pi / 2.0f)) * (Pi / 2.0f);
			continue;
		}

		if (c.timer <= 0.0)
		{
			// Alterna entre andar e parar; ao voltar a andar, às vezes muda de rua.
			c.walking = !c.walking || (rng() % 3u) != 0;
			if (c.walking && (rng() % 2u) == 0) c.angle = static_cast<float>(rng() % 4u) * (Pi / 2.0f);
			c.timer = c.walking ? 3.0 + (rng() % 800u) / 100.0 : 1.5 + (rng() % 300u) / 100.0;
			c.stateTime = 0.0;
		}

		if (!c.walking) continue;

		const float dx = std::cos(c.angle), dz = std::sin(c.angle);
		const float nx = c.x + dx * step, nz = c.z + dz * step;
		const float ahead = CitizenRadius + 0.05f;
		const int cx = static_cast<int>(std::floor(nx + dx * ahead)), cz = static_cast<int>(std::floor(nz + dz * ahead));
		const float px = playerX - nx, pz = playerZ - nz;
		const bool playerAhead = px * px + pz * pz < 0.7f * 0.7f && px * dx + pz * dz > 0.0f;
		if (!this->walkable(level, cx, cz) || playerAhead)
		{
			// Rua bloqueada: vira para uma direção livre (ou espera, se for o jogador).
			if (playerAhead) { c.walking = false; c.timer = 1.0; c.stateTime = 0.0; continue; }
			const int start = static_cast<int>(rng() % 4u);
			for (int k = 0; k < 4; k++)
			{
				const float angle = static_cast<float>((start + k) % 4) * (Pi / 2.0f);
				const int tx = static_cast<int>(std::floor(c.x)) + static_cast<int>(std::lround(std::cos(angle)));
				const int tz = static_cast<int>(std::floor(c.z)) + static_cast<int>(std::lround(std::sin(angle)));
				if (this->walkable(level, tx, tz)) { c.angle = angle; break; }
			}

			// Mantém no centro da faixa para não raspar nas paredes.
			c.x = std::floor(c.x) + 0.5f;
			c.z = std::floor(c.z) + 0.5f;
			continue;
		}

		c.x = nx;
		c.z = nz;
		// Puxa para o meio da célula na direção transversal.
		if (std::fabs(dx) > 0.5f) c.z += (std::floor(c.z) + 0.5f - c.z) * std::min(1.0f, step * 2.0f);
		else c.x += (std::floor(c.x) + 0.5f - c.x) * std::min(1.0f, step * 2.0f);
	}
}

void arena::CitizenSystem::draw(Renderer3D &renderer, const Level &level, float eyeX, float eyeZ, float drawRadius) const
{
	const Vec3 right = renderer.getRight();
	const Vec3 forward = renderer.getForward();
	for (const Citizen &c : this->citizens)
	{
		if (this->hidden(c)) continue;
		const float ox = c.x - eyeX, oz = c.z - eyeZ;
		if (ox * ox + oz * oz > drawRadius * drawRadius || ox * forward.x + oz * forward.z < -0.5f) continue;

		// Direção da animação relativa à câmera (1 = de frente; 6..8 espelham 4..2).
		const float diffAngle = std::atan2(eyeZ - c.z, eyeX - c.x);
		float relative = std::fmod(TwoPi + (diffAngle - c.angle) + TwoPi / 16.0f, TwoPi);
		int direction = std::clamp(static_cast<int>(relative / (TwoPi / 8.0f)), 0, 7) + 1;
		bool mirrored = false;
		if (direction >= 6) { direction = 10 - direction; mirrored = true; }

		const Look &look = this->looks[static_cast<size_t>(c.look)];
		const auto &frames = look.directions[static_cast<size_t>(direction - 1)];
		const bool walking = c.walking && c.talkTimer <= 0.0;
		const int *indices = walking ? WalkFrames : IdleFrames;
		const size_t count = walking ? std::size(WalkFrames) : std::size(IdleFrames);
		const int index = indices[static_cast<size_t>(c.stateTime * (walking ? WalkFps : IdleFps)) % count];
		// Só há quadros parados de frente: nas outras direções, o primeiro quadro.
		const Frame &frame = frames[index < static_cast<int>(frames.size()) ? static_cast<size_t>(index) : 0];

		const float yb = level.floorHeightAt(static_cast<int>(std::floor(c.x)), static_cast<int>(std::floor(c.z)));
		const float yt = yb + frame.height;
		const float hw = frame.width * 0.5f;
		const Vec3 v[4] =
		{
			{ c.x - right.x * hw, yt, c.z - right.z * hw },
			{ c.x + right.x * hw, yt, c.z + right.z * hw },
			{ c.x + right.x * hw, yb, c.z + right.z * hw },
			{ c.x - right.x * hw, yb, c.z - right.z * hw }
		};
		const float u0 = mirrored ? 0.999f : 0.0f, u1 = mirrored ? 0.0f : 0.999f;
		const float uv[4][2] = { { u0, 0 }, { u1, 0 }, { u1, 0.999f }, { u0, 0.999f } };
		Surface figure;
		figure.kind = Surface::Kind::Figure;
		renderer.drawQuad(v, uv, this->textures[static_cast<size_t>(frame.texture)], true, figure);
	}
}

bool arena::CitizenSystem::blocks(float x, float z, float radius) const
{
	for (const Citizen &c : this->citizens)
	{
		if (this->hidden(c)) continue;
		const float dx = x - c.x, dz = z - c.z;
		if (dx * dx + dz * dz < (radius + CitizenRadius) * (radius + CitizenRadius)) return true;
	}

	return false;
}

int arena::CitizenSystem::findFacing(float px, float pz, float yaw, float reach) const
{
	const float fx = std::cos(yaw), fz = std::sin(yaw);
	int best = -1;
	float bestAlong = reach;
	for (size_t i = 0; i < this->citizens.size(); i++)
	{
		if (this->hidden(this->citizens[i])) continue;
		const float dx = this->citizens[i].x - px, dz = this->citizens[i].z - pz;
		const float along = dx * fx + dz * fz;
		const float side = std::fabs(dx * fz - dz * fx);
		if (along > 0.0f && along < bestAlong && side < 0.5f) { best = static_cast<int>(i); bestAlong = along; }
	}

	return best;
}

void arena::CitizenSystem::faceTowards(int index, float x, float z)
{
	Citizen &c = this->citizens[static_cast<size_t>(index)];
	c.angle = std::atan2(z - c.z, x - c.x);
	c.talkTimer = 4.0;
	c.stateTime = 0.0;
}
