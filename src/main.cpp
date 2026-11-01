// The Elder Scrolls: Arena — Remake em C++ (PT-BR)
//
// Uso:
//   ArenaRemake [--arena <pasta ARENA>]
//   ArenaRemake --screenshot <title|menu|menu-popup|class> <saida.png>   (sem janela)
//   ArenaRemake --continuar                                           (entra no mundo com o personagem salvo)
//   ArenaRemake --script "k:x;k:n;shot:a.png" [--seed N] [--gpu]        (roteiro sem janela; --gpu testa a placa de vídeo)
//   ArenaRemake --cpu                                                  (força o renderizador por software)

#define SDL_MAIN_HANDLED
#include <SDL.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

#include <chrono>
#include <map>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "core/KeyValueFile.h"
#include "core/PngWriter.h"
#include "game/Game.h"
#include "game/GameWorldScreen.h"
#include "gpu/GlDevice.h"

using namespace arena;

namespace
{
	// Argumentos da linha de comando em UTF-8 (no Windows o argv vem na página de código ANSI).
	std::vector<std::string> utf8Arguments(int argc, char **argv)
	{
		std::vector<std::string> args(argv, argv + argc);
#ifdef _WIN32
		int count = 0;
		LPWSTR *wide = CommandLineToArgvW(GetCommandLineW(), &count);
		if (wide != nullptr)
		{
			args.clear();
			for (int i = 0; i < count; i++)
			{
				const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
				std::string arg(static_cast<size_t>(std::max(0, size - 1)), 0);
				WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, arg.data(), size, nullptr, nullptr);
				args.push_back(std::move(arg));
			}

			LocalFree(wide);
		}
#endif
		return args;
	}

	struct Settings
	{
		std::string arenaPath;
		bool fullscreen = false;
		bool vsync = true;
		std::string filter = "sharp"; // sharp | nearest | linear
		int windowWidth = 1280;
		int windowHeight = 960;
		float renderScale = 0.6f; // resolução interna da visão 3D em relação à janela (renderizador por software)
		std::string renderer = "gpu"; // gpu (OpenGL, com volta automática para cpu) | cpu
		float gpuRenderScale = 1.0f;  // resolução da cena 3D na placa de vídeo em relação à janela
		float soundVolume = 0.8f;
		float musicVolume = 0.6f;
		arena::GraphicsOptions graphics;
		std::map<char, char> keyRemap; // tecla apertada -> tecla do jogo ([Controls])
		bool gamepad = true;
		float gamepadLookSpeed = 1.0f;
	};

	// Ações do teclado e a tecla padrão de cada uma ([Controls] em settings.ini).
	const std::map<std::string, char> DefaultControls =
	{
		{ "Forward", 'W' }, { "Back", 'S' }, { "StrafeLeft", 'A' }, { "StrafeRight", 'D' }, { "Attack", 'F' }, { "Use", 'E' },
		{ "Cast", 'Q' }, { "Rest", 'R' }, { "Character", 'C' }, { "Inventory", 'I' }, { "Spellbook", 'B' }, { "Map", 'M' },
		{ "Travel", 'T' }, { "Journal", 'J' }, { "Help", 'H' }
	};

	Settings loadSettings(const std::filesystem::path &path)
	{
		Settings settings;
		parseKeyValueFile(path, [&settings](const std::string &key, const std::string &value)
		{
			if (key == "Game.ArenaPath") settings.arenaPath = value;
			else if (key == "Video.Fullscreen") settings.fullscreen = value == "1" || value == "true";
			else if (key == "Video.VSync") settings.vsync = value == "1" || value == "true";
			else if (key == "Video.Filter") settings.filter = value;
			else if (key == "Video.WindowWidth") settings.windowWidth = std::max(320, std::atoi(value.c_str()));
			else if (key == "Audio.SoundVolume") settings.soundVolume = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.0f, 1.0f);
			else if (key == "Audio.MusicVolume") settings.musicVolume = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.0f, 1.0f);
			else if (key == "Video.Renderer") settings.renderer = value;
			else if (key == "Video.GpuRenderScale") settings.gpuRenderScale = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.25f, 2.0f);
			else if (key == "Video.UiScale") arena::kit::setInterfaceScale(static_cast<float>(std::atof(value.c_str())));
			else if (key == "Video.RenderScale") settings.renderScale = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.2f, 1.0f);
			else if (key.rfind("Graphics.", 0) == 0)
			{
				const bool on = value == "1" || value == "true";
				if (key == "Graphics.ModernLighting") settings.graphics.modernLighting = on;
				else if (key == "Graphics.AmbientOcclusion") settings.graphics.ambientOcclusion = on;
				else if (key == "Graphics.Reflections") settings.graphics.reflections = on;
				else if (key == "Graphics.Bloom") settings.graphics.bloom = on;
				else if (key == "Graphics.Vignette") settings.graphics.vignette = on;
				else if (key == "Graphics.HDTextures") settings.graphics.hdTextures = on;
				else if (key == "Graphics.Relief") settings.graphics.relief = on;
			}
			else if (key == "Video.WindowHeight") settings.windowHeight = std::max(240, std::atoi(value.c_str()));
			else if (key == "Controls.Gamepad") settings.gamepad = value == "1" || value == "true";
			else if (key == "Controls.GamepadLookSpeed") settings.gamepadLookSpeed = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.2f, 3.0f);
			else if (key.rfind("Controls.", 0) == 0 && value.size() == 1)
			{
				// Ação=Tecla: a tecla escolhida passa a fazer o que a tecla padrão da ação faz.
				const auto it = DefaultControls.find(key.substr(9));
				const char chosen = static_cast<char>(std::toupper(static_cast<unsigned char>(value[0])));
				if (it != DefaultControls.end() && std::isalnum(static_cast<unsigned char>(chosen)) && chosen != it->second) settings.keyRemap[chosen] = it->second;
			}
		});

		return settings;
	}

	std::filesystem::path findArenaDir(const std::string &configured)
	{
		std::vector<std::filesystem::path> candidates;
		if (!configured.empty()) candidates.emplace_back(configured);
		candidates.emplace_back("ARENA");
		candidates.emplace_back("../original/ARENA");
		candidates.emplace_back("Z:/SteamLibrary/steamapps/common/The Elder Scrolls Arena/ARENA");
		candidates.emplace_back("C:/Program Files (x86)/Steam/steamapps/common/The Elder Scrolls Arena/ARENA");
		candidates.emplace_back("C:/Program Files/Steam/steamapps/common/The Elder Scrolls Arena/ARENA");

		for (const auto &dir : candidates)
		{
			std::error_code ec;
			if (std::filesystem::exists(dir / "GLOBAL.BSA", ec))
			{
				return dir;
			}
		}

		return {};
	}

	// Proporção 4:3: o modo 320x200 do VGA era exibido com pixels 20% mais altos.
	SDL_Rect computeViewport(int windowW, int windowH)
	{
		const double target = 4.0 / 3.0;
		int w = windowW;
		int h = static_cast<int>(std::lround(windowW / target));
		if (h > windowH)
		{
			h = windowH;
			w = static_cast<int>(std::lround(windowH * target));
		}

		return SDL_Rect{ (windowW - w) / 2, (windowH - h) / 2, w, h };
	}

	InputEvent::Key translateKey(SDL_Keycode key, char &outChar)
	{
		outChar = 0;
		switch (key)
		{
		case SDLK_ESCAPE: return InputEvent::Key::Escape;
		case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: return InputEvent::Key::Enter;
		case SDLK_UP: return InputEvent::Key::Up;
		case SDLK_DOWN: return InputEvent::Key::Down;
		case SDLK_LEFT: return InputEvent::Key::Left;
		case SDLK_RIGHT: return InputEvent::Key::Right;
		case SDLK_BACKSPACE: return InputEvent::Key::Backspace;
		case SDLK_LSHIFT: case SDLK_RSHIFT: return InputEvent::Key::Shift;
		default: break;
		}

		if (key >= SDLK_a && key <= SDLK_z) { outChar = static_cast<char>('A' + (key - SDLK_a)); return InputEvent::Key::Character; }
		if (key >= SDLK_0 && key <= SDLK_9) { outChar = static_cast<char>('0' + (key - SDLK_0)); return InputEvent::Key::Character; }
		return InputEvent::Key::None;
	}

	// Tamanho das capturas da visão 3D (roteiro: size:1920,1080).
	int shotWidth = 960, shotHeight = 540;

	// Placa de vídeo em uso (nullptr = renderizador por software).
	GlDevice *activeGpu = nullptr;

	// Renderiza a visão 3D no tamanho pedido; com GPU, compõe cena + HUD e lê o quadro final.
	void renderWorldFrame(const Game &game, std::vector<uint32_t> &pixels, int width, int height)
	{
		game.renderWorld(pixels, width, height);
		if (activeGpu && game.getGpu())
		{
			activeGpu->composeWorld(pixels, width, height);
		}
	}

	bool saveCanvas(const Game &game, const std::filesystem::path &path)
	{
		if (game.usesWorldView())
		{
			int W = shotWidth, H = shotHeight;
			std::vector<uint32_t> pixels;
			renderWorldFrame(game, pixels, W, H);
			if (activeGpu && game.getGpu()) activeGpu->readFinal(pixels, W, H);
			return writePngRGBA(path, W, H, reinterpret_cast<const uint8_t*>(pixels.data()));
		}

		Canvas canvas;
		float brightness = 1.0f;
		game.render(canvas, brightness);
		std::vector<uint8_t> rgba;
		canvas.toRGBA(game.getPalette(), brightness, rgba);
		if (activeGpu)
		{
			// Mesmo caminho da janela: tela 2D ampliada pela GPU na área 4:3.
			const SDL_Rect viewport = computeViewport(shotWidth, shotHeight);
			activeGpu->composeCanvas(rgba, shotWidth, shotHeight, viewport.x, viewport.y, viewport.w, viewport.h, "sharp");
			std::vector<uint32_t> pixels;
			int W = 0, H = 0;
			activeGpu->readFinal(pixels, W, H);
			return writePngRGBA(path, W, H, reinterpret_cast<const uint8_t*>(pixels.data()));
		}

		return writePngRGBA(path, Canvas::Width, Canvas::Height, rgba.data());
	}

	// Executa um roteiro de entradas sem janela, para testes automatizados.
	// Comandos separados por ';':
	//   k:X (tecla de atalho), enter, esc, up, down, left, right, bs,
	//   click:x,y  rclick:x,y  move:x,y  wheel:n  text:Nome  shot:arquivo.png
	int runScript(Game &game, const std::string &script)
	{
		auto settle = [&game]() { game.update(1.0); game.update(1.0); };
		auto send = [&game, &settle](InputEvent event) { game.handleInput(event); settle(); };
		auto keyEvent = [](InputEvent::Key key, char c = 0)
		{
			InputEvent event;
			event.key = key;
			event.character = c;
			return event;
		};

		settle();
		size_t start = 0;
		while (start < script.size())
		{
			size_t end = script.find(';', start);
			if (end == std::string::npos) end = script.size();
			const std::string command = script.substr(start, end - start);
			start = end + 1;

			const size_t colon = command.find(':');
			const std::string name = command.substr(0, colon);
			const std::string arg = (colon == std::string::npos) ? std::string() : command.substr(colon + 1);
			int x = 0, y = 0;
			if (const size_t comma = arg.find(','); comma != std::string::npos)
			{
				x = std::atoi(arg.c_str());
				y = std::atoi(arg.c_str() + comma + 1);
			}

			if (name == "k" && !arg.empty()) send(keyEvent(InputEvent::Key::Character, static_cast<char>(std::toupper(static_cast<unsigned char>(arg[0])))));
			else if (name == "enter") send(keyEvent(InputEvent::Key::Enter));
			else if (name == "esc") send(keyEvent(InputEvent::Key::Escape));
			else if (name == "up") send(keyEvent(InputEvent::Key::Up));
			else if (name == "down") send(keyEvent(InputEvent::Key::Down));
			else if (name == "left") send(keyEvent(InputEvent::Key::Left));
			else if (name == "right") send(keyEvent(InputEvent::Key::Right));
			else if (name == "bs") send(keyEvent(InputEvent::Key::Backspace));
			else if (name == "click" || name == "rclick" || name == "move")
			{
				InputEvent event;
				event.type = (name == "move") ? InputEvent::Type::MouseMove : InputEvent::Type::MouseDown;
				event.button = (name == "rclick") ? 3 : 1;
				event.x = x;
				event.y = y;
				send(event);
			}
			else if (name == "hold" || name == "run")
			{
				// hold:W,0.5 segura a tecla por 0,5 s de jogo (run: com Shift).
				const char key = static_cast<char>(std::toupper(static_cast<unsigned char>(arg.empty() ? 'W' : arg[0])));
				const double seconds = (arg.find(',') != std::string::npos) ? std::atof(arg.c_str() + arg.find(',') + 1) : 0.5;
				InputEvent shift = keyEvent(InputEvent::Key::Shift);
				if (name == "run") game.handleInput(shift);
				game.handleInput(keyEvent(InputEvent::Key::Character, key));
				for (double t = 0.0; t < seconds; t += 1.0 / 60.0) game.update(1.0 / 60.0);
				InputEvent up = keyEvent(InputEvent::Key::Character, key);
				up.type = InputEvent::Type::KeyUp;
				game.handleInput(up);
				if (name == "run") { shift.type = InputEvent::Type::KeyUp; game.handleInput(shift); }
			}
			else if (name == "turn" || name == "look")
			{
				// turn:graus gira a câmera; look:graus inclina para cima/baixo.
				InputEvent event;
				event.type = InputEvent::Type::MouseMove;
				const float pixels = static_cast<float>(std::atof(arg.c_str())) * 3.14159265f / 180.0f / 0.0035f;
				if (name == "turn") event.dx = pixels; else event.dy = -pixels;
				send(event);
			}
			else if (name == "press" && !arg.empty())
			{
				// press:E aperta a tecla sem avançar o tempo do jogo.
				game.handleInput(keyEvent(InputEvent::Key::Character, static_cast<char>(std::toupper(static_cast<unsigned char>(arg[0])))));
				game.update(1.0 / 60.0);
			}
			else if (name == "attack")
			{
				// attack:0.1 clica (ataca) e avança só esse tempo, para capturar o golpe no meio.
				InputEvent event;
				event.type = InputEvent::Type::MouseDown;
				game.handleInput(event);
				for (double t = 0.0; t < std::atof(arg.c_str()); t += 1.0 / 60.0) game.update(1.0 / 60.0);
			}
			else if ((name == "vclick" || name == "vmove") && arg.find(',') != std::string::npos)
			{
				// vclick:0.5,0.25 clica numa fração da tela (interface em resolução nativa).
				InputEvent event;
				event.type = (name == "vmove") ? InputEvent::Type::MouseMove : InputEvent::Type::MouseDown;
				event.viewX = static_cast<float>(std::atof(arg.c_str()));
				event.viewY = static_cast<float>(std::atof(arg.c_str() + arg.find(',') + 1));
				game.handleInput(event);
				game.update(1.0 / 60.0);
			}
			else if (name == "size")
			{
				// size:L,A muda a resolução das capturas e do bench da visão 3D.
				shotWidth = std::max(160, x);
				shotHeight = std::max(100, y);
			}
			else if (name == "bench")
			{
				// bench:N renderiza N quadros na resolução das capturas e informa o tempo médio.
				const int frames = std::max(1, std::atoi(arg.c_str()));
				std::vector<uint32_t> pixels;
				const auto begin = std::chrono::steady_clock::now();
				for (int i = 0; i < frames; i++) renderWorldFrame(game, pixels, shotWidth, shotHeight);
				if (activeGpu) activeGpu->finish();
				const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / frames;
				std::printf("Média: %.2f ms por quadro (%.0f FPS) em %dx%d%s\n", ms, 1000.0 / ms, shotWidth, shotHeight, activeGpu ? " (GPU)" : "");
			}
			else if (name == "wait")
			{
				for (double t = 0.0; t < std::atof(arg.c_str()); t += 1.0 / 60.0) game.update(1.0 / 60.0);
			}
			else if (name == "wheel")
			{
				InputEvent event;
				event.type = InputEvent::Type::MouseWheel;
				event.wheel = std::atoi(arg.c_str());
				send(event);
			}
			else if (name == "text")
			{
				InputEvent event;
				event.type = InputEvent::Type::TextInput;
				event.text = arg;
				send(event);
			}
			else if (name == "shot")
			{
				if (!saveCanvas(game, arg))
				{
					std::fprintf(stderr, "Falha ao gravar %s\n", arg.c_str());
					return 1;
				}

				std::printf("Captura gravada em %s\n", arg.c_str());
			}
			else if (game.debugCommand(name, arg))
			{
			}
			else if (!name.empty())
			{
				std::fprintf(stderr, "Comando desconhecido no roteiro: %s\n", name.c_str());
				return 1;
			}
		}

		return 0;
	}

	int runHeadless(Game &game, const std::string &scenario, const std::filesystem::path &outPath)
	{
		std::string script;
		if (scenario == "menu") script = "k:x";
		else if (scenario == "menu-popup") script = "k:x;k:c";
		else if (scenario == "class") script = "k:x;k:n";
		return runScript(game, script + ";shot:" + outPath.string());
	}
}

int main(int argc, char **argv)
{
	SDL_SetMainReady();

	const std::filesystem::path baseDir = [argv]()
	{
		char *sdlBase = SDL_GetBasePath();
		std::filesystem::path path = sdlBase ? std::filesystem::path(sdlBase) : std::filesystem::path(argv[0]).parent_path();
		SDL_free(sdlBase);
		return path;
	}();

	Settings settings = loadSettings(baseDir / "settings.ini");
	std::string screenshotScenario;
	std::filesystem::path screenshotPath;
	std::string script;
	long long seed = -1;
	bool continueSaved = false;
	bool headlessGpu = false;
	const std::vector<std::string> args = utf8Arguments(argc, argv);
	const int argCount = static_cast<int>(args.size());
	for (int i = 1; i < argCount; i++)
	{
		const std::string &arg = args[static_cast<size_t>(i)];
		auto next = [&args, &i]() -> const std::string & { return args[static_cast<size_t>(++i)]; };
		if (arg == "--arena" && i + 1 < argCount) settings.arenaPath = next();
		else if (arg == "--screenshot" && i + 2 < argCount) { screenshotScenario = next(); const std::string &path = next(); screenshotPath = std::filesystem::path(std::u8string(path.begin(), path.end())); }
		else if (arg == "--script" && i + 1 < argCount) script = next();
		else if (arg == "--seed" && i + 1 < argCount) seed = std::atoll(next().c_str());
		else if (arg == "--fullscreen") settings.fullscreen = true;
		else if (arg == "--continuar") continueSaved = true;
		else if (arg == "--cpu") settings.renderer = "cpu";
		else if (arg == "--gpu") headlessGpu = true;
		else if (arg == "--classico") settings.graphics = { false, false, false, false, false, false, false }; // visual próximo do original
	}

	const bool headless = !screenshotScenario.empty() || !script.empty();

	const std::filesystem::path arenaDir = findArenaDir(settings.arenaPath);
	Game game;
	game.setGraphics(settings.graphics);
	if (!headless)
	{
		// Antes do init para a tela de título já tocar a música.
		game.enableAudio(settings.soundVolume, settings.musicVolume);
	}

	std::string error;
	if (arenaDir.empty() || !game.init(arenaDir, baseDir / "data", error))
	{
		const std::string message = arenaDir.empty()
			? "Pasta do Arena não encontrada.\nDefina ArenaPath em settings.ini ou use --arena <pasta>."
			: error;
		std::fprintf(stderr, "%s\n", message.c_str());
		if (!headless)
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Arena Remake", message.c_str(), nullptr);
		}

		return 1;
	}

	if (seed >= 0)
	{
		game.getRandom().seed(static_cast<uint32_t>(seed));
	}

	// Atalho: entra direto no mundo com o personagem salvo.
	if (continueSaved && game.loadSavedCharacter())
	{
		game.changeScreen(std::make_unique<GameWorldScreen>(game, game.startingMap()));
	}

	GlDevice gpu;
	auto requestGlContext = []()
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
	};

	if (headless)
	{
		SDL_Window *hidden = nullptr;
		if (headlessGpu)
		{
			std::string gpuError;
			if (SDL_Init(SDL_INIT_VIDEO) == 0)
			{
				requestGlContext();
				hidden = SDL_CreateWindow("Arena Remake (testes)", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
			}
			if (hidden && gpu.init(hidden, false, settings.gpuRenderScale, gpuError))
			{
				activeGpu = &gpu;
				game.setGpu(&gpu);
				std::printf("GPU: %s\n", gpu.describe().c_str());
			}
			else
			{
				std::fprintf(stderr, "GPU indisponível (%s); usando o renderizador por software.\n", gpuError.empty() ? SDL_GetError() : gpuError.c_str());
			}
		}

		const int result = !script.empty() ? runScript(game, script) : runHeadless(game, screenshotScenario, screenshotPath);
		if (hidden) SDL_DestroyWindow(hidden);
		return result;
	}

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
	{
		std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}

	// Efeitos sonoros: estéreo float a 44,1 kHz, misturados pelo AudioMixer do jogo.
	SDL_AudioSpec wanted{};
	wanted.freq = 44100;
	wanted.format = AUDIO_F32SYS;
	wanted.channels = 2;
	wanted.samples = 1024;
	wanted.userdata = &game;
	wanted.callback = [](void *userdata, Uint8 *stream, int length)
	{
		auto *owner = static_cast<Game*>(userdata);
		owner->getMixer().mix(reinterpret_cast<float*>(stream), length / static_cast<int>(sizeof(float) * 2), 44100);
	};
	const SDL_AudioDeviceID audioDevice = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
	if (audioDevice != 0) SDL_PauseAudioDevice(audioDevice, 0);
	else std::fprintf(stderr, "Sem áudio: %s\n", SDL_GetError());

	const Uint32 windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
	auto createWindow = [&settings](Uint32 flags)
	{
		return SDL_CreateWindow("The Elder Scrolls: Arena — Remake PT-BR",
			SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, settings.windowWidth, settings.windowHeight, flags);
	};

	SDL_Window *window = nullptr;
	SDL_Renderer *renderer = nullptr;
	if (settings.renderer != "cpu")
	{
		requestGlContext();
		window = createWindow(windowFlags | SDL_WINDOW_OPENGL);
		std::string gpuError;
		if (window && gpu.init(window, settings.vsync, settings.gpuRenderScale, gpuError))
		{
			activeGpu = &gpu;
			game.setGpu(&gpu);
			std::printf("Renderizando pela placa de vídeo: %s\n", gpu.describe().c_str());
		}
		else
		{
			std::fprintf(stderr, "OpenGL indisponível (%s); usando o renderizador por software.\n", gpuError.empty() ? SDL_GetError() : gpuError.c_str());
			if (window) SDL_DestroyWindow(window);
			window = nullptr;
		}
	}

	if (!activeGpu)
	{
		window = createWindow(windowFlags);
		renderer = window ? SDL_CreateRenderer(window, -1,
			SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE | (settings.vsync ? SDL_RENDERER_PRESENTVSYNC : 0)) : nullptr;
		if (!renderer)
		{
			std::fprintf(stderr, "Falha ao criar janela: %s\n", SDL_GetError());
			return 1;
		}
	}

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
	SDL_Texture *frame = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, Canvas::Width, Canvas::Height) : nullptr;

	// "Sharp bilinear": amplia por um fator inteiro com nearest e depois reduz
	// com filtro linear até o tamanho final — pixels nítidos sem serrilhado irregular.
	SDL_Texture *upscaled = nullptr;
	int upscaleFactor = 0;

	Canvas canvas;
	std::vector<uint8_t> rgba;
	SDL_Texture *worldTexture = nullptr;
	int worldTextureW = 0, worldTextureH = 0;
	std::vector<uint32_t> worldPixels;
	bool relativeMouse = false;
	bool running = true;
	bool fullscreen = settings.fullscreen;
	auto last = std::chrono::steady_clock::now();

	// Controle de videogame (SDL_GameController): o primeiro conectado.
	SDL_GameController *pad = nullptr;
	auto openPad = [&]()
	{
		if (pad || !settings.gamepad) return;
		for (int i = 0; i < SDL_NumJoysticks() && !pad; i++)
			if (SDL_IsGameController(i)) pad = SDL_GameControllerOpen(i);
		if (pad) std::printf("Controle conectado: %s\n", SDL_GameControllerName(pad));
	};
	openPad();
	std::map<char, bool> stickKeys; // W/S/A/D seguradas pelo analógico esquerdo
	bool rightTrigger = false, leftTrigger = false;
	float padCursorX = 0.0f, padCursorY = 0.0f; // cursor movido pelo analógico nos menus (fração de pixel)
	double padWheelTimer = 0.0;
	auto sendKey = [&game](InputEvent::Key key, char character, bool down)
	{
		InputEvent event;
		event.type = down ? InputEvent::Type::KeyDown : InputEvent::Type::KeyUp;
		event.key = key;
		event.character = character;
		game.handleInput(event);
	};

	while (running && !game.shouldQuit())
	{
		int outW = 0, outH = 0;
		if (activeGpu) SDL_GL_GetDrawableSize(window, &outW, &outH);
		else SDL_GetRendererOutputSize(renderer, &outW, &outH);
		int winW = 0, winH = 0;
		SDL_GetWindowSize(window, &winW, &winH);
		const SDL_Rect viewport = computeViewport(outW, outH);

		auto toCanvas = [&](int wx, int wy, int &cx, int &cy)
		{
			// Coordenadas do mouse vêm em pontos da janela; converte para pixels de saída (HiDPI).
			const double px = wx * (static_cast<double>(outW) / std::max(1, winW));
			const double py = wy * (static_cast<double>(outH) / std::max(1, winH));
			cx = static_cast<int>((px - viewport.x) * Canvas::Width / std::max(1, viewport.w));
			cy = static_cast<int>((py - viewport.y) * Canvas::Height / std::max(1, viewport.h));
		};

		// Posição do mouse relativa à janela inteira (interface em resolução nativa).
		auto toView = [&](int wx, int wy, InputEvent &event)
		{
			event.viewX = static_cast<float>(wx) / std::max(1, winW);
			event.viewY = static_cast<float>(wy) / std::max(1, winH);
		};

		SDL_Event sdlEvent;
		while (SDL_PollEvent(&sdlEvent))
		{
			InputEvent event;
			switch (sdlEvent.type)
			{
			case SDL_QUIT:
				running = false;
				break;
			case SDL_KEYDOWN:
			{
				const SDL_Keycode key = sdlEvent.key.keysym.sym;
				const bool altEnter = key == SDLK_RETURN && (sdlEvent.key.keysym.mod & KMOD_ALT);
				if (key == SDLK_F11 || altEnter)
				{
					fullscreen = !fullscreen;
					SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
					break;
				}

				if (key == SDLK_F12)
				{
					std::filesystem::create_directories(baseDir / "screenshots");
					const auto name = "arena_" + std::to_string(std::time(nullptr)) + ".png";
					saveCanvas(game, baseDir / "screenshots" / name);
					break;
				}

				event.type = InputEvent::Type::KeyDown;
				event.key = translateKey(key, event.character);
				// Explorando, o Espaço pula (nos menus continua sendo Enter).
				if (key == SDLK_SPACE && game.wantsRelativeMouse()) { event.key = InputEvent::Key::Character; event.character = ' '; }
				if (event.key == InputEvent::Key::Character && settings.keyRemap.contains(event.character)) event.character = settings.keyRemap.at(event.character);
				if (event.key != InputEvent::Key::None)
				{
					game.handleInput(event);
				}
				break;
			}
			case SDL_KEYUP:
				event.type = InputEvent::Type::KeyUp;
				event.key = translateKey(sdlEvent.key.keysym.sym, event.character);
				if (event.key == InputEvent::Key::Character && settings.keyRemap.contains(event.character)) event.character = settings.keyRemap.at(event.character);
				if (event.key != InputEvent::Key::None)
				{
					game.handleInput(event);
				}
				break;
			case SDL_MOUSEMOTION:
				event.type = InputEvent::Type::MouseMove;
				event.dx = static_cast<float>(sdlEvent.motion.xrel);
				event.dy = static_cast<float>(sdlEvent.motion.yrel);
				toCanvas(sdlEvent.motion.x, sdlEvent.motion.y, event.x, event.y);
				toView(sdlEvent.motion.x, sdlEvent.motion.y, event);
				game.handleInput(event);
				break;
			case SDL_MOUSEBUTTONDOWN:
				event.type = InputEvent::Type::MouseDown;
				event.button = sdlEvent.button.button == SDL_BUTTON_RIGHT ? 3 : 1;
				toCanvas(sdlEvent.button.x, sdlEvent.button.y, event.x, event.y);
				toView(sdlEvent.button.x, sdlEvent.button.y, event);
				game.handleInput(event);
				break;
			case SDL_MOUSEWHEEL:
				event.type = InputEvent::Type::MouseWheel;
				event.wheel = sdlEvent.wheel.y;
				{
					int mx = 0, my = 0;
					SDL_GetMouseState(&mx, &my);
					toView(mx, my, event);
				}
				game.handleInput(event);
				break;
			case SDL_TEXTINPUT:
				event.type = InputEvent::Type::TextInput;
				event.text = sdlEvent.text.text;
				game.handleInput(event);
				break;
			case SDL_CONTROLLERDEVICEADDED:
				openPad();
				break;
			case SDL_CONTROLLERDEVICEREMOVED:
				if (pad && !SDL_GameControllerGetAttached(pad)) { SDL_GameControllerClose(pad); pad = nullptr; }
				break;
			case SDL_CONTROLLERBUTTONDOWN:
			case SDL_CONTROLLERBUTTONUP:
			{
				// A confirma, B volta, X usa, Y diário, LB ficha, RB descansa, Back viagem,
				// Start pausa, direcional = setas, analógico esquerdo apertado = correr.
				const bool down = sdlEvent.type == SDL_CONTROLLERBUTTONDOWN;
				switch (sdlEvent.cbutton.button)
				{
				case SDL_CONTROLLER_BUTTON_A:
					if (!game.wantsRelativeMouse())
					{
						// Nos menus o A clica onde está o cursor (que o analógico move).
						if (!down) break;
						int mx = 0, my = 0;
						SDL_GetMouseState(&mx, &my);
						InputEvent click;
						click.type = InputEvent::Type::MouseDown;
						toCanvas(mx, my, click.x, click.y);
						toView(mx, my, click);
						game.handleInput(click);
					}
					else sendKey(InputEvent::Key::Character, ' ', down); // explorando, o A pula (o X usa)
					break;
				case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_START: sendKey(InputEvent::Key::Escape, 0, down); break;
				case SDL_CONTROLLER_BUTTON_X: sendKey(InputEvent::Key::Character, 'E', down); break;
				case SDL_CONTROLLER_BUTTON_Y: sendKey(InputEvent::Key::Character, 'J', down); break;
				case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: sendKey(InputEvent::Key::Character, 'C', down); break;
				case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: sendKey(InputEvent::Key::Character, 'R', down); break;
				case SDL_CONTROLLER_BUTTON_BACK: sendKey(InputEvent::Key::Character, 'T', down); break;
				case SDL_CONTROLLER_BUTTON_LEFTSTICK: sendKey(InputEvent::Key::Shift, 0, down); break;
				case SDL_CONTROLLER_BUTTON_DPAD_UP: sendKey(InputEvent::Key::Up, 0, down); break;
				case SDL_CONTROLLER_BUTTON_DPAD_DOWN: sendKey(InputEvent::Key::Down, 0, down); break;
				case SDL_CONTROLLER_BUTTON_DPAD_LEFT: sendKey(InputEvent::Key::Left, 0, down); break;
				case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: sendKey(InputEvent::Key::Right, 0, down); break;
				default: break;
				}
				break;
			}
			default:
				break;
			}
		}

		const auto now = std::chrono::steady_clock::now();
		const double dt = std::min(0.1, std::chrono::duration<double>(now - last).count());
		last = now;

		if (pad)
		{
			auto axis = [pad](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(pad, a) / 32767.0f; };
			// Analógico esquerdo: segura W/S/A/D enquanto passa da zona morta (só explorando).
			const float lx = axis(SDL_CONTROLLER_AXIS_LEFTX), ly = axis(SDL_CONTROLLER_AXIS_LEFTY);
			const bool exploring = game.wantsRelativeMouse();
			const std::pair<char, bool> stickWanted[] = { { 'W', exploring && ly < -0.35f }, { 'S', exploring && ly > 0.35f }, { 'A', exploring && lx < -0.35f }, { 'D', exploring && lx > 0.35f } };
			for (const auto &[key, on] : stickWanted)
				if (stickKeys[key] != on) { stickKeys[key] = on; sendKey(InputEvent::Key::Character, key, on); }

			// Analógico direito: olhar (como o mouse) durante a exploração.
			const float rx = axis(SDL_CONTROLLER_AXIS_RIGHTX), ry = axis(SDL_CONTROLLER_AXIS_RIGHTY);
			if (game.wantsRelativeMouse() && (std::fabs(rx) > 0.15f || std::fabs(ry) > 0.15f))
			{
				InputEvent look;
				look.type = InputEvent::Type::MouseMove;
				const float speed = 900.0f * settings.gamepadLookSpeed * static_cast<float>(dt);
				look.dx = (std::fabs(rx) > 0.15f ? rx * std::fabs(rx) : 0.0f) * speed;
				look.dy = (std::fabs(ry) > 0.15f ? ry * std::fabs(ry) : 0.0f) * speed * 0.6f;
				game.handleInput(look);
			}

			// Menus: o analógico esquerdo move o cursor do mouse e o direito rola as listas.
			if (!exploring)
			{
				const float speed = std::max(winW, winH) * 0.9f * static_cast<float>(dt);
				const float cx = std::fabs(lx) > 0.15f ? lx * std::fabs(lx) * speed : 0.0f, cy = std::fabs(ly) > 0.15f ? ly * std::fabs(ly) * speed : 0.0f;
				if (cx != 0.0f || cy != 0.0f)
				{
					int mx = 0, my = 0;
					SDL_GetMouseState(&mx, &my);
					padCursorX = (std::fabs(padCursorX - mx) > 2.0f ? mx : padCursorX) + cx;
					padCursorY = (std::fabs(padCursorY - my) > 2.0f ? my : padCursorY) + cy;
					padCursorX = std::clamp(padCursorX, 0.0f, static_cast<float>(winW - 1));
					padCursorY = std::clamp(padCursorY, 0.0f, static_cast<float>(winH - 1));
					SDL_WarpMouseInWindow(window, static_cast<int>(padCursorX), static_cast<int>(padCursorY));
				}

				const float ry2 = axis(SDL_CONTROLLER_AXIS_RIGHTY);
				padWheelTimer -= dt;
				if (std::fabs(ry2) > 0.5f && padWheelTimer <= 0.0)
				{
					padWheelTimer = 0.12;
					int mx = 0, my = 0;
					SDL_GetMouseState(&mx, &my);
					InputEvent wheel;
					wheel.type = InputEvent::Type::MouseWheel;
					wheel.wheel = ry2 < 0.0f ? 1 : -1;
					toView(mx, my, wheel);
					game.handleInput(wheel);
				}
			}

			// Gatilhos: direito ataca (como o clique), esquerdo conjura (Q).
			const bool rt = axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.5f, lt = axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 0.5f;
			if (rt && !rightTrigger && exploring) { InputEvent attack; attack.type = InputEvent::Type::MouseDown; game.handleInput(attack); }
			if (lt != leftTrigger && (exploring || !lt)) sendKey(InputEvent::Key::Character, 'Q', lt);
			rightTrigger = rt;
			leftTrigger = lt;
		}

		game.update(dt);

		// Mouse preso à janela durante a exploração em primeira pessoa.
		const bool wantRelative = game.wantsRelativeMouse();
		if (wantRelative != relativeMouse)
		{
			SDL_SetRelativeMouseMode(wantRelative ? SDL_TRUE : SDL_FALSE);
			relativeMouse = wantRelative;
		}

		if (activeGpu)
		{
			if (game.usesWorldView())
			{
				// Cena 3D na resolução da janela (vezes GpuRenderScale) + HUD em resolução cheia.
				renderWorldFrame(game, worldPixels, std::max(160, outW), std::max(100, outH));
			}
			else
			{
				float brightness = 1.0f;
				game.render(canvas, brightness);
				canvas.toRGBA(game.getPalette(), brightness, rgba);
				activeGpu->composeCanvas(rgba, outW, outH, viewport.x, viewport.y, viewport.w, viewport.h, settings.filter);
			}

			activeGpu->present();
			continue;
		}

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);

		if (game.usesWorldView())
		{
			// Visão 3D em widescreen, na resolução da janela vezes RenderScale.
			const float scale = game.nativeResolution() ? 1.0f : settings.renderScale;
			const int worldW = std::max(160, static_cast<int>(outW * scale));
			const int worldH = std::max(100, static_cast<int>(outH * scale));
			if (worldW != worldTextureW || worldH != worldTextureH)
			{
				if (worldTexture) SDL_DestroyTexture(worldTexture);
				worldTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, worldW, worldH);
				SDL_SetTextureScaleMode(worldTexture, SDL_ScaleModeLinear);
				worldTextureW = worldW;
				worldTextureH = worldH;
			}

			game.renderWorld(worldPixels, worldW, worldH);
			SDL_UpdateTexture(worldTexture, nullptr, worldPixels.data(), worldW * 4);
			SDL_RenderCopy(renderer, worldTexture, nullptr, nullptr);
			SDL_RenderPresent(renderer);
			continue;
		}

		float brightness = 1.0f;
		game.render(canvas, brightness);
		canvas.toRGBA(game.getPalette(), brightness, rgba);
		SDL_UpdateTexture(frame, nullptr, rgba.data(), Canvas::Width * 4);

		if (settings.filter == "sharp")
		{
			const int factor = std::max(1, static_cast<int>(std::ceil(static_cast<double>(viewport.h) / Canvas::Height)));
			if (factor != upscaleFactor)
			{
				if (upscaled) SDL_DestroyTexture(upscaled);
				upscaled = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
					Canvas::Width * factor, Canvas::Height * factor);
				SDL_SetTextureScaleMode(upscaled, SDL_ScaleModeLinear);
				upscaleFactor = factor;
			}

			SDL_SetTextureScaleMode(frame, SDL_ScaleModeNearest);
			SDL_SetRenderTarget(renderer, upscaled);
			SDL_RenderCopy(renderer, frame, nullptr, nullptr);
			SDL_SetRenderTarget(renderer, nullptr);
			SDL_RenderCopy(renderer, upscaled, nullptr, &viewport);
		}
		else
		{
			SDL_SetTextureScaleMode(frame, settings.filter == "linear" ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
			SDL_RenderCopy(renderer, frame, nullptr, &viewport);
		}

		SDL_RenderPresent(renderer);
	}

	if (upscaled) SDL_DestroyTexture(upscaled);
	if (worldTexture) SDL_DestroyTexture(worldTexture);
	if (frame) SDL_DestroyTexture(frame);
	if (renderer) SDL_DestroyRenderer(renderer);
	game.setGpu(nullptr);
	activeGpu = nullptr;
	SDL_DestroyWindow(window);
	if (audioDevice != 0) SDL_CloseAudioDevice(audioDevice);
	SDL_Quit();
	return 0;
}
