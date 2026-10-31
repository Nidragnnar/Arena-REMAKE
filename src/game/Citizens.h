#pragma once

#include <array>
#include <random>
#include <vector>

#include "../render3d/Renderer3D.h"
#include "../world/Level.h"
#include "ArenaData.h"

namespace arena
{
	// Moradores das cidades, como no original: animação de andar e parado pelo clima
	// (MLGEN0@T/D/W.CFA, FMGEN0@.CFA...), roupas com cores sorteadas e pele pela raça da
	// província. Andam pelas ruas nas quatro direções e param de vez em quando.
	class CitizenSystem
	{
	public:
		void init(const AssetManager &assets, const Palette &palette, const ArenaData &data, const Level &level, const CityLocation &city, std::mt19937 &rng);
		void update(double dt, const Level &level, float playerX, float playerZ, std::mt19937 &rng);
		void draw(Renderer3D &renderer, const Level &level, float eyeX, float eyeZ, float drawRadius) const;

		bool blocks(float x, float z, float radius) const;
		// Morador à frente (para conversar), ou -1.
		int findFacing(float px, float pz, float yaw, float reach) const;
		bool isMale(int index) const { return this->looks[static_cast<size_t>(this->citizens[static_cast<size_t>(index)].look)].male; }
		void faceTowards(int index, float x, float z);
		float angleOf(int index) const { return this->citizens[static_cast<size_t>(index)].angle; }
		size_t count() const { return this->citizens.size(); }
		std::pair<float, float> position(int index) const { return { this->citizens[static_cast<size_t>(index)].x, this->citizens[static_cast<size_t>(index)].z }; }
		bool empty() const { return this->citizens.empty(); }
		void remove(int index) { this->citizens.erase(this->citizens.begin() + index); }
		// À noite só uma parte dos moradores fica na rua.
		void setNight(bool value) { this->night = value; }

	private:
		struct Frame { int texture = -1; float width = 0, height = 0; };
		struct Look
		{
			bool male = true;
			std::array<std::vector<Frame>, 5> directions; // @1..@5 (6..8 espelham 4..2)
		};

		struct Citizen
		{
			float x = 0, z = 0;
			float angle = 0;
			int look = 0;
			bool walking = true;
			double stateTime = 0, timer = 0;
			double talkTimer = 0; // parado conversando com o jogador
			bool nightOwl = false; // continua na rua à noite (1 em cada 4)
		};

		bool walkable(const Level &level, int cx, int cz) const;
		bool hidden(const Citizen &c) const { return this->night && !c.nightOwl; }
		bool night = false;

		std::vector<Texture> textures;
		std::vector<Look> looks;
		std::vector<Citizen> citizens;
	};
}
