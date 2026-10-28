#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../render3d/GpuBackend.h"
#include "../render3d/Renderer3D.h"

struct SDL_Window;

namespace arena
{
	// Renderização pela placa de vídeo (OpenGL 3.3 core). Faz a cena 3D com os mesmos
	// efeitos do renderizador por software (relevo, água, oclusão de ambiente, reflexos,
	// bloom, vinheta), compõe o HUD por cima e apresenta também as telas 2D.
	class GlDevice : public GpuBackend
	{
	public:
		GlDevice();
		~GlDevice() override;

		// Cria o contexto na janela (que deve ter SDL_WINDOW_OPENGL). renderScale: escala da cena 3D.
		bool init(SDL_Window *window, bool vsync, float renderScale, std::string &outError);
		std::string describe() const { return this->rendererName; }

		void renderScene(const GpuScene &scene) override;
		void setWorldBrightness(float value) override { this->brightness = value; }

		// Compõe a última cena + camada do HUD (RGBA, alfa não pré-multiplicado) no quadro final.
		void composeWorld(const std::vector<uint32_t> &overlay, int width, int height);
		// Mostra uma tela 2D 320x200 na área 4:3 (filter: sharp | nearest | linear).
		void composeCanvas(const std::vector<uint8_t> &rgba, int width, int height, int viewX, int viewY, int viewW, int viewH, const std::string &filter);
		// Copia o quadro final para a janela e troca os buffers.
		void present();
		// Lê o quadro final (RGBA, primeira linha em cima).
		void readFinal(std::vector<uint32_t> &out, int &width, int &height);
		// Espera a GPU terminar (medições de desempenho).
		void finish();

	private:
		struct Impl;
		std::unique_ptr<Impl> impl;
		SDL_Window *window = nullptr;
		void *context = nullptr;
		float renderScale = 1.0f;
		float brightness = 1.0f;
		std::string rendererName;
	};
}
