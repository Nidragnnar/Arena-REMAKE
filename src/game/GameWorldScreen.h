#pragma once

#include <array>
#include <map>
#include <set>
#include <tuple>
#include <string>

#include "../render3d/Renderer3D.h"
#include "../world/Level.h"
#include "Creatures.h"
#include "Game.h"
#include "InventoryUi.h"
#include "GameMenus.h"
#include "ServiceMenu.h"
#include "Citizens.h"
#include "DialogueMenu.h"
#include "TravelMenu.h"
#include "SpellbookUi.h"

namespace arena
{
	// Exploração em primeira pessoa de um nível (começa na Prisão Imperial, START.MIF).
	class GameWorldScreen : public Screen
	{
	public:
		GameWorldScreen(Game &game, const std::string &mifName);

		void handleInput(const InputEvent &event) override;
		void update(double dt) override;
		void render(Canvas &canvas) override;
		const Palette &getPalette() const override { return this->palette; }

		bool usesWorldView() const override { return true; }
		void renderWorld(std::vector<uint32_t> &pixels, int width, int height) override;
		bool debugCommand(const std::string &name, const std::string &arg) override;
		bool wantsRelativeMouse() const override { return this->loaded && !this->paused && this->health > 0 && this->overlay == Overlay::None && !this->chapterOpen && this->visionText.empty() && this->gateTimer <= 0.0; }

	private:
		// Arma em primeira pessoa (CIF do original) e seus golpes.
		struct WeaponFrame { int texture; int offsetX, offsetY; };
		struct Weapon
		{
			std::string name;
			int minDamage = 1, maxDamage = 2;
			std::vector<WeaponFrame> frames;
			std::vector<Texture> textures;
			int idleFrame = 32;
			bool fists = false;
		};

		void setupWeapon();
		void startSwing();
		void resolveSwing();
		void handleCombatEvents(const std::vector<CombatEvent> &events);
		void drawWeapon(std::vector<uint32_t> &pixels, int width, int viewHeight);
		std::string creatureRef(int creature, bool capitalized) const;
		void checkTriggers(int cellX, int cellZ);
		bool tryOpenLoot();
		void applyOverlayResult(const OverlayResult &result);
		void persist();
		PlayerCombatStats combatStats() const;
		void drawOverlay(std::vector<uint32_t> &pixels, int width, int height);
		void openCharacterMenu(CharacterMenu::Tab tab);
		void checkLevelUp();

		// Saves (GameWorldSave.cpp)
		std::string serializeWorld() const;
		void restoreWorld(const std::vector<std::pair<std::string, std::string>> &entries);
		bool saveToSlot(int slot);
		void removeSpriteAt(size_t index);
		MenuState menuState() const;
		std::vector<std::string> describeEffects() const;
		void screenButtonAction(char action);
		void drawChapterEnd(kit::Painter &painter, int width, int height);
		void drawSky();
		std::string currentLocation() const;
		static constexpr double GateDuration = 2.2;
		void playAt(const std::string &sound, float worldX, float worldZ, float volume = 1.0f);
		void rest();
		void movePlayer(float dx, float dz);
		bool collides(float x, float z) const;
		void interact();
		void showMessage(const std::string &text, double seconds = 4.0);
		void drawLevel();
		void drawHud(std::vector<uint32_t> &pixels, int width, int height);
		std::string compassDirection() const;

		// Magia (GameWorldSpells.cpp)
		void loadSpellArt();
		void castReadySpell();
		void releaseSpell();
		void applySpellToSelf(const Spell &spell);
		void applySpellToCreature(const Spell &spell, int creature);
		void spellImpact(const Spell &spell, float px, float py, float pz, int directHit);
		bool openDoorAhead(const SpellEffect &effect);
		void rewardKill(int creature, int maxHp);
		void updateMagic(double dt);
		void addMagicLights(std::vector<PointLight> &lights) const;
		void drawMagic();
		void drawCasting(std::vector<uint32_t> &pixels, int width, int viewHeight);
		int fortifyBonus(int attribute) const;
		bool hasPlayerEffect(SpellEffectType type) const;

		Palette palette{};
		Level level;
		Renderer3D renderer;
		bool loaded = false;
		std::string loadError;

		// Jogador
		float x = 0, z = 0;
		float yaw = 0, pitch = 0;
		float eyeOffset = 0.0f;  // suaviza subida em degraus/água
		double time = 0.0;
		double bob = 0.0;
		std::set<char> heldKeys;
		bool holdLeft = false, holdRight = false, holdUp = false, holdDown = false, holdShift = false;

		std::string message;
		double messageTimer = 0.0;
		double helpTimer = 12.0;
		bool paused = false;
		bool showMinimap = true;

		// Combate
		CreatureSystem creatures;
		CitizenSystem citizens; // moradores (só nas cidades)
		bool tryTalkToCitizen();
		Weapon weapon;
		int health = 1;
		int experience = 0;
		double hurtTimer = 0.0;
		bool swinging = false;
		bool swingResolved = false;
		double swingTime = 0.0;
		std::vector<int> swingFrames;
		float recentMouseX = 0.0f;  // movimento horizontal recente (escolhe o golpe)
		int swingCycle = 0;

		// Gatilhos, chaves e sons de passos
		std::set<int> keys;
		std::set<size_t> firedTriggers;
		int lastCellX = -1, lastCellZ = -1;
		float stepDistance = 0.0f;
		bool leftFoot = false;

		// Inventário e saque (janelas sobre o mundo; o jogo pausa enquanto abertas)
		enum class Overlay { None, Character, Loot, LevelUp, SaveLoad, Service, Dialogue, Travel };
		Overlay overlay = Overlay::None;
		CharacterMenu characterMenu;
		LootMenu lootMenu;
		LevelUpMenu levelUpMenu;
		SaveLoadMenu saveLoadMenu;
		ServiceMenu serviceMenu;
		DialogueMenu dialogueMenu;
		TravelMenu travelMenu;
		bool isWild() const { return this->mifName.rfind("WILD:", 0) == 0; }
		// Cidade do mapa atual (cidade, ermo em volta dela ou prédio dentro dela).
		const CityLocation *currentCity() const;
		bool useCityGate(int cellX, int cellZ);
		// Masmorras: "DNG:semente:largura:profundidade:artefato:andar" ou "MIFL:ARQ.MIF:andar".
		bool isDungeon() const { return this->mifName.rfind("IN:DNG:", 0) == 0 || this->mifName.rfind("IN:MIFL:", 0) == 0; }
		bool loadMap(const std::string &name);
		void changeDungeonLevel(int delta);
		void travelToDungeon(const CityLocation &target, int days);
		void enterDungeon(const std::string &dungeon, const std::string &back, const std::string &name);
		void spawnBeside(bool levelUp);
		int dungeonLevel = 0;
		double wildSpawnTimer = 2.0;
		void updateWildCreatures(double dt);
		bool nightMusic = false;      // música da noite tocando (exteriores)
		float skyDaylight = -1.0f;    // luz do dia usada no degradê atual do céu
		void updateDayNight(bool force);
		// Tempo da cidade: sorteado por cidade e por dia (0 limpo, 1 nublado, 2 chuva, 3 neve).
		enum Weather { Clear, Overcast, Rain, Snow };
		Weather weather() const;
		static Weather weatherFor(const CityLocation *city, double gameMinutes);
		void drawWeather(std::vector<uint32_t> &pixels, int width, int height);
		int skyWeather = -1;
		std::string placeDirections(float fromX, float fromZ, int menu, const std::string &label) const;
		bool tryTalkToClerk();

		// Botões das telas de pausa e de morte (desenhados no HUD) e o mouse em pixels do quadro.
		std::vector<std::pair<Rect, char>> screenButtons;
		int uiMouseX = -1, uiMouseY = -1;
		int uiFrameW = 1, uiFrameH = 1;
		std::map<std::pair<int, int>, Loot> containerLoot; // por célula do baú/pilha
		std::map<int, Loot> corpseLoot;                    // por criatura
		std::pair<int, int> openContainer{ -1, -1 };
		int weaponMaterialBonus = 0;

		// Estado para os saves.
		std::string mifName;
		std::string locationName; // nome mostrado no HUD e no save
		// Interior de um prédio: mapa e posição da rua para onde a saída leva.
		std::string returnMap;
		float returnX = 0, returnZ = 0, returnYaw = 0;
		bool isPrison() const;
		bool enterBuilding(int cellX, int cellZ, const Cell &cell);
		void leaveBuilding();
		bool escaped = false; // atravessou o Portão de Transição
		int nextCity = -1; // cidade de destino ao sair pelo portão (id local na província natal)
		Texture skyTexture; // degradê do céu (exteriores)
		double gateTimer = 0.0; // passagem pelo portão em andamento (clarão)
		bool chapterOpen = false; // painel de fim do capítulo da prisão

		// Missão principal (MainQuest.h): sonhos de Ria e achados, num painel que pausa o jogo.
		std::string visionTitle, visionText; // aberto quando visionText não está vazio
		void openVision(const std::string &title, const std::string &body);
		void tryDream();
		bool tryTakeQuestItem(size_t sprite);
		void drawVision(kit::Painter &painter, int width, int height);
		std::vector<std::tuple<float, float, std::string>> removedSprites;

		// Magia: pontos, conjuração em andamento, projéteis, efeitos ativos no jogador e nas criaturas.
		struct ScreenAnimation { std::vector<Texture> textures; std::vector<std::pair<int, int>> offsets; };
		struct Projectile { float x, y, z, dx, dy, dz; float travelled = 0.0f; int spell = -1; };
		struct Burst { float x, y, z; double age = 0.0; int element = 5; };
		struct PlayerEffect { SpellEffectType type; double remaining; int magnitude; int sub; double tick; };
		struct CreatureDot { int creature; double remaining; double tick; int damage; };
		int spellPoints = 0;
		int shieldPoints = 0;
		bool casting = false;
		bool castReleased = false;
		double castTime = 0.0;
		int castingSpell = -1;
		std::array<ScreenAnimation, 5> castAnimations; // fogo, gelo, choque, magia, brilho
		Texture castHands;
		std::array<Texture, 6> orbTextures;            // projétil por elemento
		std::vector<Projectile> projectiles;
		std::vector<Burst> bursts;
		std::vector<PlayerEffect> playerEffects;
		std::vector<CreatureDot> creatureDots;
	};
}
