#pragma once

#include <cstdint>
#include <vector>

#include "../world/Level.h"

namespace arena
{
	struct Camera;
	struct PointLight;
	struct GraphicsOptions;

	// Vértice da cena no espaço do mundo, com tudo que o shader precisa para sombrear
	// exatamente como o renderizador por software (relevo, oclusão de ambiente, água...).
	struct GpuVertex
	{
		float x, y, z;
		float u, v;
		float nx, ny, nz;      // normal da face (voltada para a câmera)
		float tx, ty, tz;      // direção de u no mundo
		float bx, by, bz;      // direção de v no mundo
		float kind;            // Surface::Kind
		float aoMask;          // bits de borda (Surface::aoMask)
		float aoFloorY, aoCeilingY;
	};

	// Faixa de vértices que usa a mesma textura.
	struct GpuBatch
	{
		const Texture *texture = nullptr;
		uint32_t first = 0;
		uint32_t count = 0;
	};

	struct GpuScene
	{
		const Camera *camera = nullptr;
		const std::vector<PointLight> *lights = nullptr;
		const GraphicsOptions *options = nullptr;
		float ambient = 0.0f;
		float time = 0.0f;
		float windowColor[3] = { 0, 0, 0 }; // 0..1
		float windowGlow = 0.0f;
		int width = 0, height = 0; // tamanho da saída (a GPU pode renderizar em escala própria)
		const std::vector<GpuVertex> *vertices = nullptr;
		const std::vector<GpuBatch> *batches = nullptr;
	};

	// Implementado pelo executável (OpenGL); o núcleo do jogo só conhece esta interface.
	class GpuBackend
	{
	public:
		virtual ~GpuBackend() = default;

		// Desenha a cena 3D (com reflexos, bloom e vinheta) no buffer interno da GPU.
		virtual void renderScene(const GpuScene &scene) = 0;
		// Brilho do quadro (fade entre telas), aplicado na composição final.
		virtual void setWorldBrightness(float brightness) = 0;
	};
}
