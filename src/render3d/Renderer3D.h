#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "../world/Level.h"
#include "GpuBackend.h"
#include "ThreadPool.h"

namespace arena
{
	struct Vec3 { float x = 0, y = 0, z = 0; };

	struct Camera
	{
		Vec3 position;
		float yaw = 0.0f;   // radianos, no plano XZ
		float pitch = 0.0f; // radianos, positivo = olhar para cima
		float fovY = 1.1f;  // campo de visão vertical (radianos)
	};

	struct PointLight
	{
		Vec3 position;
		float radius = 1.0f;
		float r = 1.0f, g = 1.0f, b = 1.0f;
	};

	// Opções gráficas ([Graphics] no settings.ini). Desligar tudo aproxima o visual do original.
	struct GraphicsOptions
	{
		bool modernLighting = true;    // luz difusa pela normal das superfícies
		bool ambientOcclusion = true;  // cantos e encontros de parede escurecidos
		bool reflections = true;       // reflexos na água (espaço de tela)
		bool bloom = true;             // brilho em chamas, lava e áreas muito claras
		bool vignette = true;          // bordas da tela levemente escurecidas
		bool hdTextures = true;        // texturas ampliadas 4x com grão de detalhe e filtro suave
		bool relief = true;            // relevo nas paredes, pisos e tetos (com ModernLighting)
	};

	// Como a superfície é sombreada.
	struct Surface
	{
		enum class Kind : uint8_t
		{
			Standard,
			Water,   // UV pelo mundo com ondulação, brilho especular e reflexos
			Lava,    // UV pelo mundo, emite luz própria
			Emissive, // partes claras da textura brilham sozinhas (chamas)
			Sky,      // sem luz nem sombra (céu dos exteriores)
			Figure    // pessoas e criaturas: luz normal, mas sem o brilho (bloom) das partes brancas
		};

		Kind kind = Kind::Standard;
		// Oclusão de ambiente: bits 0/1 = bordas u=0/u=1, bits 2/3 = bordas v=0/v=1 (pisos e tetos);
		// paredes também escurecem perto do chão (aoFloorY) e do teto (aoCeilingY).
		uint8_t aoMask = 0;
		float aoFloorY = -1e9f;
		float aoCeilingY = 1e9f;
	};

	// Rasterizador 3D por software: z-buffer, texturas com correção de perspectiva,
	// teste de alfa e iluminação por pixel, seguido de pós-processamento (reflexos na
	// água, bloom e vinheta). Desenha em qualquer resolução, independente dos 320x200 do original.
	class Renderer3D
	{
	public:
		void resize(int width, int height);
		void beginFrame(const Camera &camera, float ambient, const std::vector<PointLight> &lights, float time, const GraphicsOptions &options);

		// Quad com vértices em ordem (v0..v3) e UVs correspondentes.
		void drawQuad(const Vec3 (&v)[4], const float (&uv)[4][2], const Texture &texture, bool doubleSided, const Surface &surface = Surface{});

		// Rasteriza a cena enfileirada (em faixas, uma por thread) e aplica os efeitos de tela.
		// Com GPU, envia a cena para ela e getPixels() vira uma camada transparente para o HUD.
		void finishFrame();

		// Cor do vidro das janelas (0..255) e quanto ela brilha sozinha (0 = dia, 1 = noite).
		void setWindowLight(float r, float g, float b, float glow) { this->windowColor[0] = r; this->windowColor[1] = g; this->windowColor[2] = b; this->windowGlow = glow; }

		// Liga/desliga o desenho pela placa de vídeo (nullptr = renderizador por software).
		void setGpu(GpuBackend *backend) { this->gpu = backend; }
		bool usesGpu() const { return this->gpu != nullptr; }

		int getWidth() const { return this->width; }
		int getHeight() const { return this->height; }
		const std::vector<uint32_t> &getPixels() const { return this->color; }
		std::vector<uint32_t> &getPixels() { return this->color; }

		// Vetores da câmera (para billboards e interação).
		Vec3 getForward() const { return this->forward; }
		Vec3 getRight() const { return this->right; }

		// Altura da ondulação da água num ponto (também usada para a normal).
		static float waveHeight(float x, float z, float time);

	private:
		struct ClipVertex
		{
			float vx, vy, vz;   // espaço da câmera
			float wx, wy, wz;   // mundo (iluminação)
			float u, v;
		};

		enum PixelTag : uint8_t { TagNone = 0, TagWater = 1, TagGlow = 2, TagNoBloom = 3 };

		struct QueuedTriangle
		{
			ClipVertex a, b, c;
			const Texture *texture;
			Surface surface;
			Vec3 normal;
			Vec3 tangent, bitangent; // direções de u e v no mundo (relevo)
			float minY, maxY;        // linhas da tela cobertas (descarte por faixa)
			float nearest;           // menor profundidade (ordem da frente para trás)
		};

		void rasterizeTriangle(const QueuedTriangle &triangle, int bandMinY, int bandMaxY);
		void applyReflections(int bandMinY, int bandMaxY);
		template <typename Fn> void forEachBand(int rows, Fn &&fn);
		void applyBloom();
		void applyVignette();

		int width = 0;
		int height = 0;
		std::vector<uint32_t> color;
		std::vector<float> depth; // 1/z (maior = mais perto)
		std::vector<uint8_t> tags;
		std::vector<uint32_t> scratch;
		std::vector<float> bloomA, bloomB;
		std::vector<float> vignetteX, vignetteY;

		Camera camera;
		Vec3 forward, right, up;
		float focal = 1.0f;
		float ambient = 0.1f;
		float windowColor[3] = { 0, 0, 0 };
		float windowGlow = 0.0f;
		float time = 0.0f;
		GraphicsOptions options;
		std::vector<PointLight> lights;
		std::vector<QueuedTriangle> queue;
		std::unique_ptr<ThreadPool> pool = std::make_unique<ThreadPool>();

		// Caminho GPU: triângulos do quadro (3 vértices + textura cada), agrupados por textura no envio.
		GpuBackend *gpu = nullptr;
		std::vector<GpuVertex> gpuVertices, gpuSorted;
		std::vector<const Texture *> gpuTextures;
		std::vector<uint32_t> gpuOrder;
		std::vector<GpuBatch> gpuBatches;
	};
}
