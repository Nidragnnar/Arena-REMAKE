// arena_tool: utilitário de linha de comando para inspecionar os dados do Arena.
//
//   arena_tool unpack-exe <ACD.EXE> <saida.bin>
//   arena_tool bsa-list <GLOBAL.BSA>
//   arena_tool bsa-extract <GLOBAL.BSA> <NOME> <saida>
//   arena_tool export-sprites <pasta ARENA> <pasta de saida>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>

#include "assets/AssetManager.h"
#include "assets/Bsa.h"
#include "assets/Image.h"
#include "assets/Pklite.h"
#include "core/PngWriter.h"
#include "render/Canvas.h"
#include "audio/Audio.h"
#include "world/InfFile.h"
#include "world/MifFile.h"

#include <cctype>
#include <filesystem>
#include <map>
#include <set>

using namespace arena;

namespace
{
	int usage()
	{
		std::puts("Uso:\n"
			"  arena_tool unpack-exe <ACD.EXE> <saida.bin>\n"
			"  arena_tool bsa-list <GLOBAL.BSA>\n"
			"  arena_tool bsa-extract <GLOBAL.BSA> <NOME> <saida>\n"
			"  arena_tool export-sprites <pasta ARENA> <pasta de saida>");
		return 1;
	}

	// Exporta todas as imagens do jogo (soltas + GLOBAL.BSA) para PNG, por categoria.
	int exportSprites(const char *arenaDirArg, const char *outDirArg)
	{
		namespace fs = std::filesystem;
		const fs::path arenaDir = arenaDirArg, outDir = outDirArg;
		AssetManager assets;
		std::string error;
		if (!assets.init(arenaDir, arenaDir / "__sem_mods__", error))
		{
			std::fprintf(stderr, "Erro: %s\n", error.c_str());
			return 2;
		}

		auto upper = [](std::string v) { for (char &c : v) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return v; };
		auto extOf = [](const std::string &n) { const size_t d = n.rfind('.'); return d == std::string::npos ? std::string() : n.substr(d + 1); };
		auto starts = [](const std::string &n, const char *p) { return n.rfind(p, 0) == 0; };

		// Nomes: arquivos soltos e entradas do GLOBAL.BSA (sem repetir).
		std::set<std::string> names;
		for (const auto &entry : fs::directory_iterator(arenaDir))
			if (entry.is_regular_file()) names.insert(upper(entry.path().filename().string()));
		BsaArchive bsa;
		if (bsa.open(arenaDir / "GLOBAL.BSA", error))
			for (const auto &entry : bsa.getEntries()) names.insert(upper(entry.name));

		auto readPalette = [&](const char *name) { const auto d = assets.read(name); const auto pal = d ? loadColPalette(*d) : std::nullopt; return pal.value_or(Palette{}); };
		const Palette gamePalette = readPalette("PAL.COL"), sheetPalette = readPalette("CHARSHT.COL");
		// As telas do pergaminho de abertura (INTRO01..09, SCROLL01/02) usam a paleta embutida do SCROLL03.IMG.
		Palette scrollPalette = gamePalette;
		if (const auto d = assets.read("SCROLL03.IMG")) if (auto img = loadImg("SCROLL03.IMG", *d); img && img->palette) scrollPalette = *img->palette;

		FILE *index = nullptr;
		fs::create_directories(outDir);
		const std::string indexPath = (outDir / "indice.csv").string();
		fopen_s(&index, indexPath.c_str(), "w");
		if (index) std::fputs("arquivo;quadro;png;largura;altura;offsetX;offsetY;paleta\n", index);

		int files = 0, frames = 0;
		for (const std::string &name : names)
		{
			const std::string ext = extOf(name);
			if (ext != "IMG" && ext != "MNU" && ext != "CIF" && ext != "CFA" && ext != "DFA" && ext != "SET") continue;
			const auto data = assets.read(name);
			if (!data) continue;

			std::vector<IndexedImage> images;
			if (ext == "IMG" || ext == "MNU") { if (auto img = loadImg(name, *data)) images.push_back(std::move(*img)); }
			else if (ext == "CIF") images = loadCif(name, *data);
			else if (ext == "CFA") images = loadCfa(*data);
			else if (ext == "DFA") images = loadDfa(*data);
			else images = loadSet(*data);
			if (images.empty()) continue;

			// Paleta: embutida, da ficha do personagem (rostos e roupas) ou a do jogo.
			// Peças da ficha/livro de feitiços desenhadas na paleta da ficha (conferido lado a lado).
			static const std::set<std::string> sheetUi = { "BONUS.IMG", "PAGE2.IMG", "SPELLBK.IMG", "GOLD.IMG" };
			// Interface (botões, janelas, ícones de mapa) separada dos sprites do mundo.
			static const std::set<std::string> interfaceNames = { "ACCPREJT", "ARENARW", "BONUS", "CITY", "COMPASS", "DUNGEON", "LOADSAVE", "MAP", "NEGOTBUT",
				"NEWMENU", "NEWOLD", "NEWPOP", "NEW_CTR", "NEW_LFT", "NEW_RT", "NOCAMP", "NOEXIT", "NOSPELL", "OP", "P1", "PAGE2", "PARCHFLT", "POINTER", "POPTALK",
				"POPUP", "POPUP2", "POPUP3", "POPUP4", "POPUP5", "POPUP7", "POPUP8", "POPUP11", "S2", "SLIDER", "SLIDSID", "SLIDUP", "SPELLBK", "SPELL", "TOP", "TOWN",
				"UPDOWN", "VILLAGE", "YESNO", "DITHER", "DITHER2", "EQUIPMEN" };
			const std::string base = name.substr(0, name.rfind('.'));
			const bool sheet = sheetUi.count(name) > 0 || ext != "SET" && (starts(name, "FACES") || starts(name, "CHARSTAT") || starts(name, "BKFACES") || starts(name, "FORM") || starts(name, "CHARBK") || starts(name, "CHRBK") || (name.find("EQUIP") != std::string::npos && name != "EQUIPMEN.IMG")
				|| name.find("PANTS") != std::string::npos || name.find("SHIRT") != std::string::npos);
			const bool embedded = images.front().palette.has_value();
			// Rostos pequenos (FACES0x, retrato do HUD) usam a paleta do jogo.
			const bool sheetColors = (sheet && !starts(name, "FACES0") && !starts(name, "FACESF0"));
			const bool scroll = ext == "IMG" && (starts(name, "INTRO0") || starts(name, "SCROLL01") || starts(name, "SCROLL02"));
			const Palette &palette = embedded ? *images.front().palette : scroll ? scrollPalette : sheetColors ? sheetPalette : gamePalette;
			const char *paletteName = embedded ? "embutida" : scroll ? "SCROLL03.IMG" : sheetColors ? "CHARSHT.COL" : "PAL.COL";

			// Categoria.
			const IndexedImage &first = images.front();
			std::string folder;
			if (sheet && sheetUi.count(name) == 0) folder = "personagem_ficha";
			else if (ext == "CFA")
			{
				// Inimigos, humanos (classes com armadura, guardas, cidadãos) e efeitos de magia/projéteis.
				static const char *effects[] = { "EXP_", "FX_", "ACID", "FIRE", "ICE_", "MAGIC", "SHOCK", "SPARKLE", "SMOKE", "DUST", "BLOODEXP", "ARROW", "LGLIT" };
				static const char *humans[] = { "BABE", "FMGEN", "MLGEN", "GUARD" };
				const bool effect = std::any_of(std::begin(effects), std::end(effects), [&](const char *e) { return starts(name, e); });
				const bool human = std::isdigit(static_cast<unsigned char>(name[0])) || std::any_of(std::begin(humans), std::end(humans), [&](const char *h) { return starts(name, h); });
				folder = effect ? "efeitos_magia_projeteis" : human ? "humanos_e_npcs" : "inimigos";
			}
			else if (ext == "DFA") folder = "animacoes_do_mapa";
			else if (ext == "SET" || (ext == "IMG" && first.width == 64 && first.height == 64)) folder = "texturas";
			else if (ext == "CIF") folder = "armas_icones_retratos";
			else if (embedded || ext == "MNU" || (first.width >= 300 && first.height >= 190)) folder = "telas_e_menus";
			else if (ext == "IMG" && interfaceNames.count(base) > 0) folder = "interface";
			else folder = "sprites_do_mapa_e_itens";
			const bool opaque = folder == "texturas" || folder == "telas_e_menus";

			const std::string stem = name.substr(0, name.rfind('.')) + "_" + ext;
			const fs::path dir = images.size() > 1 ? outDir / folder / stem : outDir / folder;
			fs::create_directories(dir);
			for (size_t i = 0; i < images.size(); i++)
			{
				const IndexedImage &img = images[i];
				if (img.width <= 0 || img.height <= 0) continue;
				std::vector<uint8_t> rgba(static_cast<size_t>(img.width) * img.height * 4);
				for (size_t k = 0; k < img.pixels.size() && k * 4 < rgba.size(); k++)
				{
					const uint8_t colorIndex = img.pixels[k];
					const Color &c = palette[colorIndex];
					rgba[k * 4] = c.r; rgba[k * 4 + 1] = c.g; rgba[k * 4 + 2] = c.b;
					rgba[k * 4 + 3] = (!opaque && colorIndex == 0) ? 0 : 255;
					if (!opaque && !embedded && colorIndex >= 1 && colorIndex <= 13)
					{
						// Índices 1..13: translucidez do jogo (fantasmas, espectros), não cores reais.
						rgba[k * 4] = 210; rgba[k * 4 + 1] = 225; rgba[k * 4 + 2] = 255;
						rgba[k * 4 + 3] = static_cast<uint8_t>(40 + colorIndex * 15);
					}
				}
				char file[160];
				if (images.size() > 1) std::snprintf(file, sizeof(file), "%s_%03zu.png", stem.c_str(), i);
				else std::snprintf(file, sizeof(file), "%s.png", stem.c_str());
				writePngRGBA(dir / file, img.width, img.height, rgba.data());
				if (index) std::fprintf(index, "%s;%zu;%s/%s%s;%d;%d;%d;%d;%s\n", name.c_str(), i, folder.c_str(), images.size() > 1 ? (stem + "/").c_str() : "", file,
					img.width, img.height, img.offsetX, img.offsetY, paletteName);
				frames++;
			}
			files++;
		}

		if (index) std::fclose(index);
		std::printf("%d arquivos, %d imagens exportadas em %s\n", files, frames, outDir.string().c_str());
		return 0;
	}

	int unpackExe(const char *inPath, const char *outPath)
	{
		const auto exe = readWholeFile(inPath);
		if (!exe)
		{
			std::fprintf(stderr, "Falha ao ler %s\n", inPath);
			return 2;
		}

		ByteBuffer image;
		std::string error;
		if (!unpackPklite(*exe, image, error))
		{
			std::fprintf(stderr, "Erro: %s\n", error.c_str());
			return 3;
		}

		writeWholeFile(outPath, image.data(), image.size());
		std::printf("Imagem descompactada: %zu bytes -> %s\n", image.size(), outPath);
		return 0;
	}

	int bsaList(const char *path)
	{
		BsaArchive bsa;
		std::string error;
		if (!bsa.open(path, error))
		{
			std::fprintf(stderr, "Erro: %s\n", error.c_str());
			return 3;
		}

		for (const BsaArchive::Entry &entry : bsa.getEntries())
		{
			std::printf("%-12s %8u\n", entry.name.c_str(), entry.size);
		}

		std::printf("%zu entradas\n", bsa.getEntries().size());
		return 0;
	}

	int bsaExtract(const char *path, const char *name, const char *outPath)
	{
		BsaArchive bsa;
		std::string error;
		if (!bsa.open(path, error))
		{
			std::fprintf(stderr, "Erro: %s\n", error.c_str());
			return 3;
		}

		const auto data = bsa.read(name);
		if (!data)
		{
			std::fprintf(stderr, "Entrada não encontrada: %s\n", name);
			return 4;
		}

		writeWholeFile(outPath, data->data(), data->size());
		return 0;
	}

	// Converte um .IMG (solto ou dentro do GLOBAL.BSA) para PNG.
	int imgToPng(const char *arenaDir, const char *name, const char *outPath, int scale = 1, const char *paletteName = nullptr)
	{
		AssetManager assets;
		std::string error;
		if (!assets.init(arenaDir, {}, error))
		{
			std::fprintf(stderr, "Erro: %s\n", error.c_str());
			return 3;
		}

		const auto data = assets.read(name);
		const auto image = data ? loadImg(name, *data) : std::nullopt;
		if (!image)
		{
			std::fprintf(stderr, "Não foi possível carregar %s\n", name);
			return 4;
		}

		Palette palette = image->palette ? *image->palette : *loadColPalette(*assets.read("PAL.COL"));
		if (paletteName != nullptr)
		{
			// Paleta de outro arquivo: .COL ou paleta embutida de outro .IMG/.MNU.
			const auto paletteData = assets.read(paletteName);
			if (auto col = paletteData ? loadColPalette(*paletteData) : std::nullopt) palette = *col;
			else if (auto img = paletteData ? loadImg(paletteName, *paletteData) : std::nullopt; img && img->palette) palette = *img->palette;
		}
		const int outW = image->width * scale;
		const int outH = image->height * scale;
		std::vector<uint8_t> rgba(static_cast<size_t>(outW * outH) * 4);
		for (int y = 0; y < outH; y++)
		{
			for (int x = 0; x < outW; x++)
			{
				const Color &c = palette[image->at(x / scale, y / scale)];
				const size_t i = static_cast<size_t>(x + y * outW) * 4;
				// Grade a cada 10 pixels originais para facilitar medições.
				const bool grid = scale > 1 && ((x % (10 * scale)) == 0 || (y % (10 * scale)) == 0);
				rgba[i + 0] = grid ? 0 : c.r;
				rgba[i + 1] = grid ? 255 : c.g;
				rgba[i + 2] = grid ? 0 : c.b;
				rgba[i + 3] = 255;
			}
		}

		writePngRGBA(outPath, outW, outH, rgba.data());
		std::printf("%s: %dx%d%s\n", name, image->width, image->height, image->palette ? " (paleta embutida)" : "");
		return 0;
	}

	// Resume arquivos .VOC (taxa, duração; grava .WAV ao lado) e .XMI (eventos, duração).
	int audioInfo(const char *arenaDir, const std::vector<std::string> &files)
	{
		AssetManager assets;
		std::string error;
		if (!assets.init(arenaDir, {}, error))
		{
			std::fprintf(stderr, "Erro: %s\n", error.c_str());
			return 3;
		}

		for (const std::string &file : files)
		{
			const auto data = assets.read(file);
			const std::string upper = toUpperAscii(file);
			if (!data) { std::printf("%s: não encontrado\n", file.c_str()); continue; }
			if (upper.ends_with(".VOC"))
			{
				const auto sound = loadVoc(*data);
				if (!sound) { std::printf("%s: VOC inválido\n", file.c_str()); continue; }
				std::printf("%s: %d Hz, %zu amostras (%.2f s)\n", file.c_str(), sound->sampleRate, sound->samples.size(),
					static_cast<double>(sound->samples.size()) / sound->sampleRate);

				// WAV PCM 8 bits mono para conferência.
				std::vector<uint8_t> wav(44 + sound->samples.size());
				auto put32 = [&wav](size_t at, uint32_t v) { for (int i = 0; i < 4; i++) wav[at + i] = static_cast<uint8_t>(v >> (8 * i)); };
				std::memcpy(wav.data(), "RIFF", 4); put32(4, static_cast<uint32_t>(wav.size() - 8));
				std::memcpy(wav.data() + 8, "WAVEfmt ", 8); put32(16, 16); wav[20] = 1; wav[22] = 1;
				put32(24, static_cast<uint32_t>(sound->sampleRate)); put32(28, static_cast<uint32_t>(sound->sampleRate)); wav[32] = 1; wav[34] = 8;
				std::memcpy(wav.data() + 36, "data", 4); put32(40, static_cast<uint32_t>(sound->samples.size()));
				for (size_t i = 0; i < sound->samples.size(); i++) wav[44 + i] = static_cast<uint8_t>(std::clamp(sound->samples[i] * 128.0f + 128.0f, 0.0f, 255.0f));
				writeWholeFile(upper.substr(0, upper.size() - 4) + ".WAV", wav.data(), wav.size());
			}
			else if (upper.ends_with(".XMI"))
			{
				const auto events = loadXmi(*data);
				int notes = 0;
				for (const auto &e : events) if ((e.message & 0xF0) == 0x90) notes++;
				std::printf("%s: %zu eventos, %d notas, %.1f s\n", file.c_str(), events.size(), notes,
					events.empty() ? 0.0 : events.back().tick / 120.0);
			}
		}

		return 0;
	}

	// Folha com todos os quadros de arquivos .CFA/.CIF/.DFA (uma linha por arquivo), ampliada 2x.
	int animSheet(const char *arenaDir, const char *outPath, const std::vector<std::string> &files)
	{
		AssetManager assets;
		std::string error;
		if (!assets.init(arenaDir, {}, error))
		{
			std::fprintf(stderr, "Erro: %s\n", error.c_str());
			return 3;
		}

		const Palette palette = *loadColPalette(*assets.read("PAL.COL"));
		std::vector<std::vector<IndexedImage>> rows;
		int sheetW = 0, sheetH = 0;
		for (const std::string &file : files)
		{
			const auto data = assets.read(file);
			std::vector<IndexedImage> frames;
			if (data)
			{
				const std::string upper = toUpperAscii(file);
				if (upper.ends_with(".CFA")) frames = loadCfa(*data);
				else if (upper.ends_with(".DFA")) frames = loadDfa(*data);
				else frames = loadCif(file, *data);
			}

			int rowW = 0, rowH = 0;
			for (const auto &f : frames) { rowW += f.width + 2; rowH = std::max(rowH, f.height); }
			std::printf("%s: %zu quadros%s\n", file.c_str(), frames.size(), frames.empty() ? " (falhou)" : "");
			for (size_t i = 0; i < frames.size(); i++)
				std::printf("  %zu: %dx%d desloc (%d,%d)\n", i, frames[i].width, frames[i].height, frames[i].offsetX, frames[i].offsetY);
			sheetW = std::max(sheetW, rowW);
			sheetH += rowH + 2;
			rows.push_back(std::move(frames));
		}

		constexpr int Scale = 2;
		const int outW = std::max(1, sheetW) * Scale, outH = std::max(1, sheetH) * Scale;
		std::vector<uint8_t> rgba(static_cast<size_t>(outW * outH) * 4, 0);
		for (size_t i = 0; i < rgba.size(); i += 4) { rgba[i] = 40; rgba[i + 1] = 0; rgba[i + 2] = 40; rgba[i + 3] = 255; }
		int y0 = 0;
		for (const auto &frames : rows)
		{
			int x0 = 0, rowH = 0;
			for (const auto &f : frames)
			{
				for (int y = 0; y < f.height * Scale; y++)
					for (int x = 0; x < f.width * Scale; x++)
					{
						const uint8_t index = f.at(x / Scale, y / Scale);
						if (index == 0) continue;
						const size_t o = (static_cast<size_t>(y0 * Scale + y) * outW + x0 * Scale + x) * 4;
						rgba[o] = palette[index].r; rgba[o + 1] = palette[index].g; rgba[o + 2] = palette[index].b;
					}
				x0 += f.width + 2;
				rowH = std::max(rowH, f.height);
			}
			y0 += rowH + 2;
		}

		writePngRGBA(outPath, outW, outH, rgba.data());
		return 0;
	}

	// Mostra cabeçalho, .INF e um mapa ASCII de cada andar de um .MIF.
	//   . chão   # parede   D porta   A arco   | cerca/grade   / diagonal   ~ abismo   * sprite   ^ plataforma
	int mifDump(const char *arenaDir, const char *name)
	{
		AssetManager assets;
		std::string error;
		if (!assets.init(arenaDir, {}, error))
		{
			std::fprintf(stderr, "Erro: %s\n", error.c_str());
			return 3;
		}

		const auto data = assets.read(name);
		const auto mif = data ? loadMif(*data, error) : std::nullopt;
		if (!mif)
		{
			std::fprintf(stderr, "Erro: %s\n", error.c_str());
			return 4;
		}

		std::printf("%s: %dx%d, andar inicial %d, %zu andar(es)\n", name, mif->width, mif->depth, mif->startingLevel, mif->levels.size());
		for (const auto &[sx, sy] : mif->startPoints)
		{
			std::printf("  início: (%d, %d) -> voxel (%.2f, %.2f)\n", sx, sy, sx / 128.0, sy / 128.0);
		}

		for (const MifLevel &level : mif->levels)
		{
			std::printf("\nAndar '%s' INF=%s FLOR=%zu MAP1=%zu MAP2=%zu locks=%zu trig=%zu\n", level.name.c_str(), level.info.c_str(),
				level.flor.size(), level.map1.size(), level.map2.size(), level.locks.size(), level.triggers.size());
			if (std::getenv("ARENA_VOXEL"))
			{
				// ARENA_VOXEL=n: células do MAP1 que usam a textura n (msb ou lsb = n + 1).
				const int wanted = std::atoi(std::getenv("ARENA_VOXEL")) + 1;
				for (size_t i = 0; i < level.map1.size(); i++)
				{
					const uint16_t v = level.map1[i];
					if (v != 0 && (((v >> 8) & 0x7F) == wanted || (v & 0xFF) == wanted))
						std::printf("  voxel %04X em x=%zu z=%zu\n", v, i % static_cast<size_t>(mif->width), i / static_cast<size_t>(mif->width));
				}
				for (size_t i = 0; i < level.flor.size(); i++)
				{
					const uint16_t v = level.flor[i];
					if (((v >> 8) & 0xFF) == wanted || ((v >> 8) & 0xFF) == wanted - 1)
						std::printf("  chão %04X em x=%zu z=%zu\n", v, i % static_cast<size_t>(mif->width), i / static_cast<size_t>(mif->width));
				}
				for (size_t i = 0; i < level.map2.size(); i++)
					if (level.map2[i] != 0) { std::printf("  map2 tem dados\n"); break; }
			}

			if (const auto infData = assets.read(level.info))
			{
				if (const auto inf = loadInf(*infData, error))
				{
					std::printf("  INF: %zu texturas, %zu flats, teto=%d\n", inf->voxelTextures.size(), inf->flats.size(), inf->ceilingHeight);
					for (size_t i = 0; i < inf->voxelTextures.size(); i++)
						std::printf("    tex %2zu: %s%s\n", i, inf->voxelTextures[i].filename.c_str(),
							inf->voxelTextures[i].setIndex >= 0 ? ("#" + std::to_string(inf->voxelTextures[i].setIndex)).c_str() : "");
					for (size_t i = 0; i < inf->flats.size(); i++)
						std::printf("    flat %2zu: %s y=%d luz=%d item=%d\n", i, inf->flats[i].filename.c_str(), inf->flats[i].yOffset, inf->flats[i].lightRadius, inf->flats[i].itemId.value_or(-1));
				}
			}

			std::map<uint16_t, int> histogram;
			for (int y = 0; y < mif->depth; y++)
			{
				std::string row;
				for (int x = 0; x < mif->width; x++)
				{
					const size_t i = static_cast<size_t>(x + y * mif->width);
					const uint16_t m1 = level.map1.empty() ? 0 : level.map1[i];
					const uint16_t fl = level.flor.empty() ? 0 : level.flor[i];
					histogram[m1]++;
					const int nibble = m1 >> 12;
					const int floorTex = fl >> 8;
					char c = (floorTex >= 0xC && floorTex <= 0xE) ? '~' : '.';
					if (m1 != 0)
					{
						if ((m1 & 0x8000) == 0) c = ((m1 >> 8) == (m1 & 0xFF)) ? '#' : '^';
						else if (nibble == 0x8) c = ((m1 & 0xFF) == 0x2F) ? 'r' : ((m1 & 0xFF) == 0x30) ? 'g' : '*';
						else if (nibble == 0x9) c = 'A';
						else if (nibble == 0xA) c = '|';
						else if (nibble == 0xB) c = 'D';
						else if (nibble == 0xD) c = '/';
						else c = '?';
					}

					if (!level.map2.empty() && level.map2[i] != 0 && c == '.') c = ',';
					row.push_back(c);
				}

				std::printf("  %3d %s\n", y, row.c_str());
			}

			for (const auto &t : level.triggers)
				std::printf("  gatilho: col %d linha %d texto %d som %d\n", t.x, t.y, t.textIndex, t.soundIndex);
			for (const auto &l : level.locks)
				std::printf("  trava: col %d linha %d nível %d\n", l.x, l.y, l.level);

			std::map<uint16_t, int> floorHistogram;
			for (uint16_t value : level.flor) floorHistogram[value]++;
			std::printf("  Valores FLOR distintos: %zu\n  ", floorHistogram.size());
			for (const auto &[value, count] : floorHistogram) std::printf("%04X:%d ", value, count);
			std::printf("\n");

			std::printf("  Valores MAP1 distintos: %zu\n  ", histogram.size());
			for (const auto &[value, count] : histogram) std::printf("%04X:%d ", value, count);
			std::printf("\n");
		}

		return 0;
	}

	// Renderiza uma amostra de cada fonte com acentuação em português.
	int fontSheet(const char *arenaDir, const char *outPath)
	{
		AssetManager assets;
		std::string error;
		if (!assets.init(arenaDir, {}, error))
		{
			std::fprintf(stderr, "Erro: %s\n", error.c_str());
			return 3;
		}

		const Palette palette = *loadColPalette(*assets.read("PAL.COL"));
		Canvas canvas;
		canvas.clear(nearestColor(palette, 40, 30, 20));
		const uint8_t white = nearestColor(palette, 255, 255, 255);
		const uint8_t black = nearestColor(palette, 0, 0, 0);

		int y = 4;
		for (const char *fontName : { "FONT_A.DAT", "FONT_B.DAT", "FONT_C.DAT", "FONT_D.DAT", "FONT_S.DAT", "FONT4.DAT", "ARENAFNT.DAT", "CHARFNT.DAT", "TEENYFNT.DAT" })
		{
			BitmapFont font;
			const auto data = assets.read(fontName);
			if (!data || !font.load(*data))
			{
				continue;
			}

			canvas.drawText(font, std::string(fontName) + ": Ação, Coração, Você, É Á Ã Ç", 4, y + 3, white, black);
			y += font.getHeight() + 6;
		}

		std::vector<uint8_t> rgba;
		canvas.toRGBA(palette, 1.0f, rgba);
		writePngRGBA(outPath, Canvas::Width, Canvas::Height, rgba.data());
		return 0;
	}
}

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		return usage();
	}

	const std::string command = argv[1];
	if (command == "unpack-exe" && argc == 4)
	{
		return unpackExe(argv[2], argv[3]);
	}
	else if (command == "bsa-list")
	{
		return bsaList(argv[2]);
	}
	else if (command == "bsa-extract" && argc == 5)
	{
		return bsaExtract(argv[2], argv[3], argv[4]);
	}

	else if (command == "audio-info" && argc >= 4)
	{
		return audioInfo(argv[2], std::vector<std::string>(argv + 3, argv + argc));
	}
	else if (command == "anim-sheet" && argc >= 5)
	{
		return animSheet(argv[2], argv[3], std::vector<std::string>(argv + 4, argv + argc));
	}
	else if (command == "inf-dump" && argc == 4)
	{
		// Lista as texturas e os FLATS de um .INF.
		AssetManager assets;
		std::string error;
		if (!assets.init(argv[2], {}, error)) { std::fprintf(stderr, "Erro: %s\n", error.c_str()); return 3; }
		const auto data = assets.read(argv[3]);
		const auto inf = data ? loadInf(*data, error) : std::nullopt;
		if (!inf) { std::fprintf(stderr, "Erro: %s\n", error.c_str()); return 4; }
		std::printf("levelUp=%d levelDown=%d\n", inf->levelUp.value_or(-1), inf->levelDown.value_or(-1));
		for (size_t i = 0; i < inf->voxelTextures.size(); i++) std::printf("tex %2zu: %s [%d]\n", i, inf->voxelTextures[i].filename.c_str(), inf->voxelTextures[i].setIndex);
		for (size_t i = 0; i < inf->flats.size(); i++) std::printf("flat %2zu: %s\n", i, inf->flats[i].filename.c_str());
		return 0;
	}
	else if (command == "mif-dump" && argc == 4)
	{
		return mifDump(argv[2], argv[3]);
	}
	else if (command == "font-sheet" && argc == 4)
	{
		return fontSheet(argv[2], argv[3]);
	}
	else if (command == "export-sprites" && argc == 4)
	{
		return exportSprites(argv[2], argv[3]);
	}
	else if (command == "img-to-png" && argc >= 5 && argc <= 7)
	{
		return imgToPng(argv[2], argv[3], argv[4], argc >= 6 ? std::atoi(argv[5]) : 1, argc == 7 ? argv[6] : nullptr);
	}

	return usage();
}
