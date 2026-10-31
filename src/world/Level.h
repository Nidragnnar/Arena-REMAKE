#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../assets/AssetManager.h"
#include "../assets/Image.h"
#include "InfFile.h"
#include "MifFile.h"

namespace arena
{
	// Textura RGBA (paleta já aplicada; alfa 0 = transparente).
	// Índice da paleta usado pelo vidro das janelas (cor dinâmica; ver Renderer3D::setWindowLight).
	constexpr uint8_t WindowPaletteIndex = 113;

	struct Texture
	{
		int width = 0;
		int height = 0;
		std::vector<uint32_t> pixels; // 0xAABBGGRR
		bool hasTransparency = false;
		int scale = 1;               // ampliação em relação à imagem original (estilo HD)
		uint32_t id = 0;             // identificador único (cache de texturas na GPU)
		std::vector<int8_t> normals; // relevo no espaço da face (x, y, z por texel); vazio = plano
	};

	// Estilo das texturas, aplicado por toTexture a tudo que for carregado depois.
	struct TextureStyle
	{
		bool hd = true;     // ampliação bicúbica com grão de detalhe (filtro suave no renderizador)
		bool relief = true; // mapa de relevo em paredes, pisos e tetos
	};

	void setTextureStyle(const TextureStyle &style);
	const TextureStyle &getTextureStyle();
	void enhanceTexture(Texture &texture);
	void addRelief(Texture &texture); // relevo sem ampliar (texturas já em alta resolução)

	enum class CellType : uint8_t
	{
		Empty,
		Wall,            // bloco sólido
		Door,            // porta (abre levantando)
		TransparentWall, // arco/grade com textura dos dois lados
		Edge,            // painel fino num dos lados do voxel (cercas, placas)
		Diagonal,        // parede diagonal
		Raised           // plataforma elevada
	};

	enum class Facing : uint8_t { NegX, PosX, NegZ, PosZ };
	enum class ChasmKind : uint8_t { None, Dry, Wet, Lava };

	struct Cell
	{
		CellType type = CellType::Empty;
		int wallTexture = -1;   // índice no vetor de texturas do nível
		int capTexture = -1;    // topo da plataforma
		int floorTexture = -1;
		bool chasm = false;     // água/lava/fosso
		int chasmTexture = -1;
		ChasmKind chasmKind = ChasmKind::None;
		bool collider = true;
		Facing facing = Facing::NegX;
		bool diagonalRight = true;
		float raisedBottom = 0.0f;
		float raisedTop = 0.0f;
		bool isExit = false;    // *LEVELUP (portão de saída)
		int menu = -1;          // *MENU: entrada de prédio (cidade) ou saída (interior)
		bool isLevelDown = false; // *LEVELDOWN (escada para o andar de baixo)
		int upperTexture = -1;  // MAP2 (exteriores): andares de cima da parede
		int upperStories = 0;   // quantos andares acima do térreo
	};

	struct Sprite
	{
		float x = 0, z = 0;      // centro em coordenadas do mundo
		float yBottom = 0;
		float width = 0, height = 0;
		std::vector<int> frames; // índices de textura
		std::vector<int> dayFrames; // lampiões: versão apagada de dia (NLAMP1.DFA -> NLAMP1DA.IMG)
		float lightRadius = 0;   // em voxels (0 = não ilumina)
		bool collider = false;
		std::string name;
		int itemId = -1;         // *ITEM do .INF (2 corpo, 3 equipamento, 4..6 tesouro, 7 baú)
	};

	// Marcador de criatura no MAP1 (FLAT com *ITEM 32..54 no .INF).
	struct CreatureSpawn
	{
		float x = 0, z = 0;
		int creatureIndex = 0; // índice nas tabelas de criaturas do ACD.EXE
	};

	// Gatilho do MIF: ao entrar na célula, mostra um texto do .INF e/ou toca um som.
	struct Trigger
	{
		int x = 0, z = 0;       // célula no sistema do mundo
		int textIndex = -1;
		int soundIndex = -1;
	};

	// Nível pronto para o jogo. Coordenadas como no OpenTESArena: X do mundo = linha
	// do mapa original (sul), Z = coluna (oeste), Y para cima, 1 unidade = 1 voxel.
	class Level
	{
	public:
		bool load(const AssetManager &assets, const Palette &palette, const std::string &mifName, std::string &outError);
		// Monta a partir de um .MIF já carregado (cidades geradas). exterior = céu aberto, com MAP2.
		bool build(const AssetManager &assets, const Palette &palette, const MifFile &mif, bool exterior, std::string &outError);
		bool isExterior() const { return this->exterior; }

		int getWidth() const { return this->width; }   // células em X (linhas do .MIF)
		int getDepth() const { return this->depth; }   // células em Z (colunas do .MIF)
		float getWallHeight() const { return this->wallHeight; }
		int getCeilingTexture() const { return this->ceilingTexture; }

		const Cell &cell(int x, int z) const;
		bool inBounds(int x, int z) const { return x >= 0 && z >= 0 && x < this->width && z < this->depth; }

		const std::vector<Texture> &getTextures() const { return this->textures; }
		// Quadros da animação de água/lava (WATERANI/LAVAANI.RCI); vazio se o arquivo não existir.
		const std::vector<int> &getLiquidFrames(ChasmKind kind) const { return kind == ChasmKind::Lava ? this->lavaFrames : this->waterFrames; }
		const std::vector<Sprite> &getSprites() const { return this->sprites; }
		const std::vector<CreatureSpawn> &getCreatureSpawns() const { return this->creatureSpawns; }
		void addCreatureSpawn(const CreatureSpawn &spawn) { this->creatureSpawns.push_back(spawn); }
		const std::vector<Trigger> &getTriggers() const { return this->triggers; }
		const InfText *getText(int index) const
		{
			const auto it = this->texts.find(index);
			return it != this->texts.end() ? &it->second : nullptr;
		}
		std::string getSoundFile(int index) const
		{
			const auto it = this->sounds.find(index);
			return it != this->sounds.end() ? it->second : std::string();
		}
		const std::string &getInfName() const { return this->infName; }

		// Travas (LOCK do MIF): nível > 0 enquanto trancada.
		int lockLevel(int x, int z) const
		{
			const auto it = this->locks.find({ x, z });
			return it != this->locks.end() ? it->second : 0;
		}
		void unlock(int x, int z) { this->locks.erase({ x, z }); }
		void lock(int x, int z, int value) { this->locks[{ x, z }] = std::max(1, value); }
		// Feitiços que mudam o mapa (Passwall abre a parede, Earth Wall ergue uma); não ficam no save.
		void setWall(int x, int z, bool wall, int texture)
		{
			if (!this->inBounds(x, z)) return;
			Cell &c = this->cells[static_cast<size_t>(x + z * this->width)];
			c.type = wall ? CellType::Wall : CellType::Empty;
			if (wall) c.wallTexture = texture;
		}
		const std::map<std::pair<int, int>, int> &getLocks() const { return this->locks; }
		void setLocks(std::map<std::pair<int, int>, int> values) { this->locks = std::move(values); }
		// Portas abertas (para salvar) e abertura imediata ao carregar.
		std::vector<std::pair<int, int>> getOpenDoors() const
		{
			std::vector<std::pair<int, int>> open;
			for (const auto &[pos, door] : this->doors) if (door.target > 0.5f) open.push_back(pos);
			return open;
		}
		void setDoorOpen(int x, int z, bool open)
		{
			const auto it = this->doors.find({ x, z });
			if (it != this->doors.end()) it->second.open = it->second.target = open ? 1.0f : 0.0f;
		}
		// Sprite criado pelo jogo (alvos das missões secundárias), em tamanho original; false sem a imagem.
		bool addFlat(const AssetManager &assets, const Palette &palette, const std::string &filename, float x, float z, int itemId);
		void removeSprite(size_t index) { if (index < this->sprites.size()) this->sprites.erase(this->sprites.begin() + static_cast<std::ptrdiff_t>(index)); }
		float getStartX() const { return this->startX; }
		float getStartZ() const { return this->startZ; }
		const std::string &getName() const { return this->name; }

		// Portas: 0 = fechada, 1 = aberta.
		float doorOpenness(int x, int z) const;
		void toggleDoor(int x, int z);
		void update(double dt);

		// Colisão em XZ (o jogador anda sobre o chão ou plataformas baixas).
		bool blocksMovement(int x, int z) const;
		float floorHeightAt(int x, int z) const;

	private:
		int addTexture(const AssetManager &assets, const Palette &palette, const InfVoxelTexture &source);
		void loadLiquidFrames(const AssetManager &assets, const Palette &palette, const char *filename, std::vector<int> &outFrames);
		int addSpriteFrames(const AssetManager &assets, const Palette &palette, const std::string &filename, std::vector<int> &outFrames, int &outW, int &outH);

		int width = 0;
		int depth = 0;
		float wallHeight = 100.0f / 128.0f;
		bool exterior = false;
		int ceilingTexture = -1;
		float startX = 0, startZ = 0;
		std::string name;
		std::vector<Cell> cells;
		std::vector<Texture> textures;
		std::map<std::pair<std::string, int>, int> textureIds;
		std::vector<int> waterFrames, lavaFrames;
		std::vector<Sprite> sprites;
		std::vector<CreatureSpawn> creatureSpawns;
		std::vector<Trigger> triggers;
		std::map<std::pair<int, int>, int> locks;
		std::map<int, InfText> texts;
		std::map<int, std::string> sounds;
		std::string infName;

		struct DoorState { float open = 0.0f; float target = 0.0f; };
		std::map<std::pair<int, int>, DoorState> doors;
	};

	Texture toTexture(const IndexedImage &image, const Palette &palette);
	// Igual, mas usa o PNG redesenhado de data/mods quando existir (ver SpriteOverrides.h).
	Texture toTexture(const IndexedImage &image, const Palette &palette, const std::string &source, int frame, int frameCount);
	uint32_t newTextureId(); // para texturas criadas por código (cache da GPU)
}
