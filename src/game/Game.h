#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>

#include "../assets/AssetManager.h"
#include "../assets/Font.h"
#include "../i18n/Localization.h"
#include "../render/Canvas.h"
#include "../audio/Audio.h"
#include "../render3d/Renderer3D.h"
#include "ArenaData.h"
#include "SideQuests.h"

namespace arena
{
	// Entrada independente de plataforma (o SDL fica só em main.cpp), o que
	// permite renderizar telas sem janela para testes e capturas.
	struct InputEvent
	{
		enum class Type { KeyDown, MouseMove, MouseDown, MouseWheel, TextInput, KeyUp };
		enum class Key { None, Escape, Enter, Up, Down, Left, Right, Backspace, Character, Shift };

		Type type = Type::KeyDown;
		Key key = Key::None;
		char character = 0; // A..Z / 0..9 em maiúsculas quando key == Character
		int x = 0;          // coordenadas do mouse no canvas 320x200
		int y = 0;
		int button = 1;     // 1 = esquerdo, 3 = direito
		int wheel = 0;      // >0 para cima
		std::string text;   // UTF-8 digitado (TextInput)
		float dx = 0.0f;    // movimento relativo do mouse (visão 3D)
		float dy = 0.0f;
		float viewX = -1.0f; // posição do mouse na tela inteira (0..1); -1 = desconhecida
		float viewY = -1.0f;

		bool isClick() const { return this->type == Type::MouseDown; }
		bool isKey(Key k) const { return this->type == Type::KeyDown && this->key == k; }
		bool isHotkey(char c) const { return this->type == Type::KeyDown && this->key == Key::Character && this->character == c; }
	};

	// Personagem em construção durante a criação.
	// Linha do diário: quando, o que aconteceu e, se houver, o texto completo para reler
	// (chave do idioma, ex.: "Quest.Dream3", formatada com o nome do personagem).
	struct JournalEntry
	{
		double minutes = 0.0;
		std::string detail;
		std::string text;
	};

	struct CharacterDraft
	{
		std::string name;
		bool male = true;
		int classIndex = -1;
		int raceIndex = -1;
		int portraitIndex = 0;
		std::array<int, AttributeCount> attributes{};
		std::array<int, AttributeCount> addedPoints{};
		int bonusPoints = 0;
		int maxHealth = 0;
		int maxStamina = 0;
		int maxSpellPoints = 0;
		int gold = 0;

		// Estado de jogo (gravado em saves/personagem.ini).
		std::vector<Item> inventory;
		int experience = 0;
		int level = 1;
		double gameMinutes = 8.0 * 60.0; // relógio do jogo (ver GameClock.h); começa às 8h do dia 1
		int levelPoints = 0;  // pontos de bônus de nível ainda não distribuídos
		int health = -1; // -1 = vida cheia
		int spellPoints = -1; // -1 = magia cheia
		std::vector<int> spells; // feitiços conhecidos (índices do SPELLSG.65), até 32 como no original
		int readySpell = -1;     // feitiço preparado para a tecla Q
		bool spellbookKnown = false; // a ficha já tinha a seção [Spellbook]
		int staffPieces = 0; // missão principal: pedaços do Cajado do Caos (ver MainQuest.h)
		int questStep = 0;   // passo da busca do próximo pedaço
		int questGiver = -1; // cidade (id global) cujo governante espera o item da pista
		std::vector<JournalEntry> journal; // diário, do mais antigo ao mais recente
		std::vector<Spell> customSpells;   // feitiços criados na Guilda dos Magos (índice >= CustomSpellBase)
		SideJob job;                       // trabalho pago do governante ou do taverneiro (SideQuests.h)
		ArtifactHunt hunt;                 // artefato cuja masmorra o jogador conhece
		int artifactsFound = 0;            // bit por artefato já achado (Artifacts.h)
		int cofferDay = -1;                // último dia em que o Cofre do Rei Orgnum foi aberto
		int disease = -1;                  // doença pega de uma criatura (índice de DiseaseNames), -1 = saudável
		double diseaseEnd = 0.0;           // minutos do jogo em que a doença passa sozinha
		std::map<int, double> wanted;      // crimes: cidade (id global) -> minutos do jogo até a guarda esquecer
		int holidaySeenDay = -1;           // último dia de feriado já anunciado (Holidays.h)
		std::vector<int> grantedSpells;    // feitiços que estão no livro só por causa de itens equipados (artefatos, encantamentos)
	};

	class Game;

	class Screen
	{
	public:
		explicit Screen(Game &game) : game(game) { }
		virtual ~Screen() = default;

		virtual void handleInput(const InputEvent &) { }
		virtual void update(double) { }
		virtual void render(Canvas &canvas) = 0;
		virtual const Palette &getPalette() const = 0;

		// Telas 3D desenham direto num quadro RGBA na resolução da janela.
		virtual bool usesWorldView() const { return false; }
		virtual void renderWorld(std::vector<uint32_t> &, int, int) { }
		virtual bool wantsRelativeMouse() const { return false; }
		// Interface desenhada em resolução nativa (sem a escala 3D do renderizador por software).
		virtual bool nativeResolution() const { return false; }

		// Comandos extras do modo roteiro (testes automatizados); true se reconhecido.
		virtual bool debugCommand(const std::string &, const std::string &) { return false; }

	protected:
		Game &game;
	};

	struct Fonts
	{
		BitmapFont a, b, c, d, s, arena, character, teeny, four;
	};

	class Game
	{
	public:
		bool init(const std::filesystem::path &arenaDir, const std::filesystem::path &dataDir, std::string &outError);

		// Troca de tela com fade (escurece, troca, clareia), como no original.
		void changeScreen(std::unique_ptr<Screen> next);
		void requestQuit() { this->quitRequested = true; }

		void handleInput(const InputEvent &event);
		void update(double dt);
		void render(Canvas &canvas, float &outBrightness) const;
		const Palette &getPalette() const;

		bool usesWorldView() const { return this->current && this->current->usesWorldView(); }
		bool nativeResolution() const { return this->current && this->current->nativeResolution(); }
		bool wantsRelativeMouse() const { return this->current && this->current->wantsRelativeMouse() && !this->pending; }
		void renderWorld(std::vector<uint32_t> &pixels, int width, int height) const;
		bool debugCommand(const std::string &name, const std::string &arg) { return this->current && this->current->debugCommand(name, arg); }

		// Personagem gravado ao fim da criação (saves/personagem.ini).
		// O save automático (personagem.ini) e os 10 espaços (jogo1.ini..jogo10.ini) usam o mesmo formato:
		// [Slot] (resumo), [Character], [Inventory], [Spellbook] e o estado do mundo ([World], [Creatures], [Loot]).
		bool loadSavedCharacter() { return this->loadSavedCharacter(this->getSavePath()); }
		bool loadSavedCharacter(const std::filesystem::path &path);
		bool saveCharacter() const { return this->saveCharacter(this->getSavePath()); }
		bool saveCharacter(const std::filesystem::path &path, const std::string &world = std::string(), const std::string &location = std::string()) const;
		bool hasSavedCharacter() const;
		std::filesystem::path getSavePath() const { return this->dataDirectory.parent_path() / "saves" / "personagem.ini"; }
		std::filesystem::path getSlotPath(int slot) const { return this->dataDirectory.parent_path() / "saves" / ("jogo" + std::to_string(slot) + ".ini"); }
		static constexpr int SaveSlots = 10;

		// Resumo de um save para as listas de salvar/carregar.
		struct SaveSummary { bool exists = false; std::string name, date, location; int level = 1; };
		SaveSummary readSaveSummary(const std::filesystem::path &path) const;

		// Estado do mundo lido do último save carregado (consumido pela tela do mundo).
		std::vector<std::pair<std::string, std::string>> takePendingWorld() { return std::move(this->pendingWorld); }
		// Posição ("x,z,graus") para o próximo mundo aberto (saída de prédios).
		void setPendingSpawn(std::string spawn) { this->pendingSpawn = std::move(spawn); }
		std::string takePendingSpawn() { return std::exchange(this->pendingSpawn, std::string()); }
		// Mensagem para mostrar ao abrir o próximo mundo (ex.: chegada de viagem).
		void setPendingNote(std::string note) { this->pendingNote = std::move(note); }
		std::string takePendingNote() { return std::exchange(this->pendingNote, std::string()); }
		// Painel (título, texto) para a próxima tela do mundo mostrar ao abrir (ex.: fala do governante).
		void setPendingVision(std::string title, std::string body) { this->pendingVision = { std::move(title), std::move(body) }; }
		std::pair<std::string, std::string> takePendingVision() { return std::exchange(this->pendingVision, {}); }
		// Mapa do jogo carregado (World.Map) ou a Prisão Imperial.
		std::string startingMap() const
		{
			for (const auto &[key, value] : this->pendingWorld) if (key == "World.Map" && !value.empty()) return value;
			if (!this->importedMap.empty()) return this->importedMap;
			return this->text.get("World.StartDungeonMif");
		}
		// Save do Arena original (OriginalSave.h): troca o personagem e guarda o mapa de partida.
		bool importOriginalSave(int slot, std::string &outError);
		const std::filesystem::path &getArenaDir() const { return this->arenaDirectory;
		}

		bool shouldQuit() const { return this->quitRequested; }

		// Propostas de trabalho já ouvidas hoje (lugar, dia): não se repetem na mesma visita ao jogo.
		std::set<std::pair<int, int>> heardOffers;

		// Carrega um .IMG; em caso de falha retorna uma imagem preta 320x200.
		IndexedImage loadImage(const std::string &name) const;
		std::vector<IndexedImage> loadFrames(const std::string &name) const;
		std::optional<Palette> loadPalette(const std::string &name) const;

		// Caixa de pergaminho (PARCH.CIF) no tamanho pedido, em cache.
		const IndexedImage &getParchment(int width, int height);

		AssetManager &getAssets() { return this->assets; }
		const Localization &getText() const { return this->text; }
		const Fonts &getFonts() const { return this->fonts; }
		const Palette &getDefaultPalette() const { return this->defaultPalette; }
		const ArenaData &getData() const { return this->rules; }
		const std::filesystem::path &getDataDir() const { return this->dataDirectory; }
		// Grava Secao.Chave=valor no settings.ini (ao lado da pasta data), mantendo o resto.
		void saveSetting(const std::string &section, const std::string &key, const std::string &value) const;

		CharacterDraft &getCharacter() { return this->character; }
		const CharacterDraft &getCharacter() const { return this->character; }
		std::mt19937 &getRandom() { return this->rng; }

		// Placa de vídeo (OpenGL) quando disponível; nullptr = renderizador por software.
		GpuBackend *getGpu() const { return this->gpu; }
		void setGpu(GpuBackend *backend) { this->gpu = backend; }

		// Opções do renderizador 3D ([Graphics] no settings.ini).
		const GraphicsOptions &getGraphics() const { return this->graphics; }
		void setGraphics(const GraphicsOptions &options)
		{
			this->graphics = options;
			setTextureStyle({ options.hdTextures, options.relief });
		}

		// Áudio: efeitos .VOC (mixer) e música .XMI (MIDI do sistema).
		void enableAudio(float soundVolume, float musicVolume);
		AudioMixer &getMixer() { return this->mixer; }
		void playSound(const std::string &name, float volume = 1.0f, float pan = 0.0f);
		void playMusic(const std::string &name);
		void stopMusic();

		// Nomes traduzidos (com o original em inglês como reserva).
		std::string className(int index) const;
		std::string raceName(int index, bool plural) const;
		std::string provinceName(int index) const;
		std::string attributeName(int index, bool shortName) const;

		// Texto com variante feminina opcional ("Chave_F").
		const std::string &gendered(const std::string &key) const;
		std::string localized(const std::string &key, const std::string &fallback) const;

	private:
		static constexpr double FadeDuration = 0.25;

		AssetManager assets;
		Localization text;
		Fonts fonts;
		Palette defaultPalette{};
		ArenaData rules;
		std::filesystem::path dataDirectory, arenaDirectory;
		std::string importedMap; // mapa de partida do último save do original importado
		CharacterDraft character;
		std::mt19937 rng{ std::random_device{}() };
		std::vector<IndexedImage> parchmentTiles;
		std::map<std::pair<int, int>, IndexedImage> parchmentCache;

		AudioMixer mixer;
		GraphicsOptions graphics;
		std::vector<std::pair<std::string, std::string>> pendingWorld;
		std::string pendingSpawn;
		std::string pendingNote;
		std::pair<std::string, std::string> pendingVision;
		GpuBackend *gpu = nullptr;
		std::unique_ptr<MusicPlayer> music;
		std::string currentMusic;
		std::map<std::string, std::shared_ptr<const SoundBuffer>> soundCache;

		std::unique_ptr<Screen> current;
		std::unique_ptr<Screen> pending;
		double fadeTimer = 0.0; // >0 escurecendo (troca pendente) ou clareando
		bool fadingOut = false;
		bool quitRequested = false;
	};
}
