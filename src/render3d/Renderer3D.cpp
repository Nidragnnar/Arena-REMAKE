#include <algorithm>
#include <cmath>
#include <atomic>
#include <thread>

#include "Renderer3D.h"

namespace
{
	constexpr float NearPlane = 0.05f;

	// Água e lava: a animação (320x100) cobre 5x1,56 voxels, ou seja, 64 texels por voxel.
	constexpr float LiquidScaleU = 64.0f / 320.0f;
	constexpr float LiquidScaleV = 64.0f / 100.0f;
	constexpr float WaveAmplitude = 0.045f;

	arena::Vec3 cross(const arena::Vec3 &a, const arena::Vec3 &b)
	{
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}

	float dot(const arena::Vec3 &a, const arena::Vec3 &b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	arena::Vec3 normalize(const arena::Vec3 &v)
	{
		const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		return (len > 0.0f) ? arena::Vec3{ v.x / len, v.y / len, v.z / len } : v;
	}

	// Escurecimento suave perto de uma borda (d = distância até a borda, em fração do voxel).
	float edgeOcclusion(float d)
	{
		const float t = std::clamp(d / 0.45f, 0.0f, 1.0f);
		return 0.5f + 0.5f * t * t * (3.0f - 2.0f * t);
	}

	// Derivadas da ondulação (mesma soma de senos de Renderer3D::waveHeight).
	arena::Vec3 waveNormal(float x, float z, float t)
	{
		const float a = x * 3.1f + t * 1.6f, b = z * 4.3f - t * 1.2f, c = (x + z) * 6.7f + t * 2.3f, d = (x - z) * 9.1f - t * 3.1f;
		const float dx = 0.5f * 3.1f * std::cos(a) + 0.25f * 6.7f * std::cos(c) + 0.12f * 9.1f * std::cos(d);
		const float dz = 0.35f * 4.3f * std::cos(b) + 0.25f * 6.7f * std::cos(c) - 0.12f * 9.1f * std::cos(d);
		return normalize({ -dx * WaveAmplitude, 1.0f, -dz * WaveAmplitude });
	}

	uint32_t pack(float r, float g, float b)
	{
		const auto c = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.0f, 255.0f)); };
		return 0xFF000000u | (c(b) << 16) | (c(g) << 8) | c(r);
	}
}

float arena::Renderer3D::waveHeight(float x, float z, float t)
{
	return WaveAmplitude * (0.5f * std::sin(x * 3.1f + t * 1.6f) + 0.35f * std::sin(z * 4.3f - t * 1.2f)
		+ 0.25f * std::sin((x + z) * 6.7f + t * 2.3f) + 0.12f * std::sin((x - z) * 9.1f - t * 3.1f));
}

void arena::Renderer3D::resize(int newWidth, int newHeight)
{
	this->width = std::max(16, newWidth);
	this->height = std::max(16, newHeight);
	const size_t count = static_cast<size_t>(this->width) * static_cast<size_t>(this->height);
	this->color.assign(count, 0xFF000000u);
	this->depth.assign(count, 0.0f);
	this->tags.assign(count, TagNone);

	// Vinheta separável: escurece até ~25% nos cantos.
	this->vignetteX.resize(static_cast<size_t>(this->width));
	this->vignetteY.resize(static_cast<size_t>(this->height));
	for (int x = 0; x < this->width; x++)
	{
		const float d = std::fabs((x + 0.5f) * 2.0f / this->width - 1.0f);
		this->vignetteX[static_cast<size_t>(x)] = 1.0f - 0.1f * std::pow(d, 2.4f);
	}
	for (int y = 0; y < this->height; y++)
	{
		const float d = std::fabs((y + 0.5f) * 2.0f / this->height - 1.0f);
		this->vignetteY[static_cast<size_t>(y)] = 1.0f - 0.08f * std::pow(d, 2.4f);
	}
}

void arena::Renderer3D::beginFrame(const Camera &cam, float ambientLight, const std::vector<PointLight> &frameLights, float frameTime,
	const GraphicsOptions &graphics)
{
	this->camera = cam;
	this->ambient = ambientLight;
	this->lights = frameLights;
	this->time = frameTime;
	this->options = graphics;

	const float cp = std::cos(cam.pitch);
	this->forward = { cp * std::cos(cam.yaw), std::sin(cam.pitch), cp * std::sin(cam.yaw) };
	this->right = normalize(cross(this->forward, Vec3{ 0, 1, 0 }));
	this->up = cross(this->right, this->forward);
	this->focal = (this->height * 0.5f) / std::tan(cam.fovY * 0.5f);

	this->queue.clear();
	if (this->gpu)
	{
		this->gpuVertices.clear();
		this->gpuTextures.clear();
	}
}

template <typename Fn>
void arena::Renderer3D::forEachBand(int rows, Fn &&fn)
{
	// Divide as linhas em faixas pequenas que as threads do pool pegam conforme terminam
	// (a água e os reflexos se concentram em parte da tela; faixas fixas desequilibravam).
	const int bandHeight = std::max(4, rows / (this->pool->size() * 6));
	std::atomic<int> next{ 0 };
	this->pool->run([&fn, &next, rows, bandHeight](int)
	{
		for (int y0 = next.fetch_add(bandHeight); y0 < rows; y0 = next.fetch_add(bandHeight))
		{
			fn(y0, std::min(rows, y0 + bandHeight) - 1);
		}
	});
}

void arena::Renderer3D::drawQuad(const Vec3 (&v)[4], const float (&uv)[4][2], const Texture &texture, bool doubleSided, const Surface &surface)
{
	ClipVertex in[4];
	for (int i = 0; i < 4; i++)
	{
		const float dx = v[i].x - this->camera.position.x;
		const float dy = v[i].y - this->camera.position.y;
		const float dz = v[i].z - this->camera.position.z;
		in[i].vx = dx * this->right.x + dy * this->right.y + dz * this->right.z;
		in[i].vy = dx * this->up.x + dy * this->up.y + dz * this->up.z;
		in[i].vz = dx * this->forward.x + dy * this->forward.y + dz * this->forward.z;
		in[i].wx = v[i].x; in[i].wy = v[i].y; in[i].wz = v[i].z;
		in[i].u = uv[i][0]; in[i].v = uv[i][1];
	}

	// Descarte rápido: totalmente atrás da câmera.
	if (in[0].vz < NearPlane && in[1].vz < NearPlane && in[2].vz < NearPlane && in[3].vz < NearPlane)
	{
		return;
	}

	// Recorte contra o plano próximo (Sutherland-Hodgman).
	ClipVertex poly[8];
	int count = 0;
	for (int i = 0; i < 4; i++)
	{
		const ClipVertex &a = in[i];
		const ClipVertex &b = in[(i + 1) % 4];
		const bool aIn = a.vz >= NearPlane;
		const bool bIn = b.vz >= NearPlane;
		if (aIn) poly[count++] = a;
		if (aIn != bIn)
		{
			const float t = (NearPlane - a.vz) / (b.vz - a.vz);
			ClipVertex m;
			m.vx = a.vx + (b.vx - a.vx) * t; m.vy = a.vy + (b.vy - a.vy) * t; m.vz = NearPlane;
			m.wx = a.wx + (b.wx - a.wx) * t; m.wy = a.wy + (b.wy - a.wy) * t; m.wz = a.wz + (b.wz - a.wz) * t;
			m.u = a.u + (b.u - a.u) * t; m.v = a.v + (b.v - a.v) * t;
			poly[count++] = m;
		}
	}

	if (count < 3)
	{
		return;
	}

	// Descarte de face traseira (no espaço de tela, após projeção).
	if (!doubleSided)
	{
		auto sx = [this](const ClipVertex &p) { return p.vx / p.vz * this->focal; };
		auto sy = [this](const ClipVertex &p) { return -p.vy / p.vz * this->focal; };
		float area = 0.0f;
		for (int i = 0; i < count; i++)
		{
			const ClipVertex &a = poly[i];
			const ClipVertex &b = poly[(i + 1) % count];
			area += sx(a) * sy(b) - sx(b) * sy(a);
		}

		if (area <= 0.0f) return;
	}

	// Normal da face, sempre voltada para a câmera (faces de dois lados e billboards).
	Vec3 normal = normalize(cross({ v[1].x - v[0].x, v[1].y - v[0].y, v[1].z - v[0].z }, { v[3].x - v[0].x, v[3].y - v[0].y, v[3].z - v[0].z }));
	const Vec3 toCamera{ this->camera.position.x - v[0].x, this->camera.position.y - v[0].y, this->camera.position.z - v[0].z };
	if (dot(normal, toCamera) < 0.0f) normal = { -normal.x, -normal.y, -normal.z };
	Vec3 tangent = normalize({ v[1].x - v[0].x, v[1].y - v[0].y, v[1].z - v[0].z });
	Vec3 bitangent = normalize({ v[3].x - v[0].x, v[3].y - v[0].y, v[3].z - v[0].z });
	if (uv[1][0] < uv[0][0]) tangent = { -tangent.x, -tangent.y, -tangent.z };
	if (uv[3][1] < uv[0][1]) bitangent = { -bitangent.x, -bitangent.y, -bitangent.z };

	if (this->gpu)
	{
		const float kind = static_cast<float>(static_cast<int>(surface.kind));
		auto vertex = [&](const ClipVertex &p)
		{
			return GpuVertex{ p.wx, p.wy, p.wz, p.u, p.v, normal.x, normal.y, normal.z, tangent.x, tangent.y, tangent.z,
				bitangent.x, bitangent.y, bitangent.z, kind, static_cast<float>(surface.aoMask), surface.aoFloorY, surface.aoCeilingY };
		};
		for (int i = 1; i + 1 < count; i++)
		{
			this->gpuVertices.push_back(vertex(poly[0]));
			this->gpuVertices.push_back(vertex(poly[i]));
			this->gpuVertices.push_back(vertex(poly[i + 1]));
			this->gpuTextures.push_back(&texture);
		}
		return;
	}

	for (int i = 1; i + 1 < count; i++)
	{
		const ClipVertex *corners[3] = { &poly[0], &poly[i], &poly[i + 1] };
		float minY = 1e30f, maxY = -1e30f, nearest = 1e30f;
		for (const ClipVertex *p : corners)
		{
			const float sy = this->height * 0.5f - p->vy / p->vz * this->focal;
			minY = std::min(minY, sy);
			maxY = std::max(maxY, sy);
			nearest = std::min(nearest, p->vz);
		}

		if (maxY < 0.0f || minY > static_cast<float>(this->height)) continue;
		this->queue.push_back(QueuedTriangle{ poly[0], poly[i], poly[i + 1], &texture, surface, normal, tangent, bitangent, minY, maxY, nearest });
	}
}

void arena::Renderer3D::rasterizeTriangle(const QueuedTriangle &triangle, int bandMinY, int bandMaxY)
{
	const ClipVertex &a = triangle.a, &b = triangle.b, &c = triangle.c;
	const Texture &texture = *triangle.texture;
	const Surface &surface = triangle.surface;
	const Vec3 &normal = triangle.normal;
	struct Projected { float x, y, iz, uz, vz, wxz, wyz, wzz; };
	const float cx = this->width * 0.5f;
	const float cy = this->height * 0.5f;
	auto project = [&](const ClipVertex &p)
	{
		const float iz = 1.0f / p.vz;
		return Projected{ cx + p.vx * iz * this->focal, cy - p.vy * iz * this->focal, iz,
			p.u * iz, p.v * iz, p.wx * iz, p.wy * iz, p.wz * iz };
	};

	Projected p0 = project(a), p1 = project(b), p2 = project(c);
	float area = (p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y);
	if (std::fabs(area) < 1e-6f)
	{
		return;
	}

	if (area < 0.0f)
	{
		std::swap(p1, p2);
		area = -area;
	}

	const int minX = std::max(0, static_cast<int>(std::floor(std::min({ p0.x, p1.x, p2.x }))));
	const int maxX = std::min(this->width - 1, static_cast<int>(std::ceil(std::max({ p0.x, p1.x, p2.x }))));
	const int minY = std::max(bandMinY, static_cast<int>(std::floor(std::min({ p0.y, p1.y, p2.y }))));
	const int maxY = std::min(bandMaxY, static_cast<int>(std::ceil(std::max({ p0.y, p1.y, p2.y }))));
	if (minX > maxX || minY > maxY)
	{
		return;
	}

	const float invArea = 1.0f / area;
	// Funções de aresta: w0 oposta a p0, etc.
	const float e0dx = p2.x - p1.x, e0dy = p2.y - p1.y;
	const float e1dx = p0.x - p2.x, e1dy = p0.y - p2.y;
	const float e2dx = p1.x - p0.x, e2dy = p1.y - p0.y;

	const int tw = texture.width;
	const int th = texture.height;
	const uint32_t *texels = texture.pixels.data();
	const bool alphaTest = texture.hasTransparency;
	const size_t lightCount = this->lights.size();
	const PointLight *lightData = this->lights.data();

	const bool smooth = texture.scale > 1;
	// Texturas com lados potência de 2 repetem com máscara (bem mais barato que %).
	const bool pow2 = (tw & (tw - 1)) == 0 && (th & (th - 1)) == 0;
	const int maskU = tw - 1, maskV = th - 1;
	auto wrapU = [=](int i) { if (pow2) return i & maskU; i %= tw; return i < 0 ? i + tw : i; };
	auto wrapV = [=](int i) { if (pow2) return i & maskV; i %= th; return i < 0 ? i + th : i; };
	auto fastFloor = [](float f) { const int i = static_cast<int>(f); return (f < static_cast<float>(i)) ? i - 1 : i; };
	const int8_t *normalMap = (texture.normals.empty() || !this->options.modernLighting) ? nullptr : texture.normals.data();
	const Vec3 &tangent = triangle.tangent, &bitangent = triangle.bitangent;
	const bool liquid = surface.kind == Surface::Kind::Water || surface.kind == Surface::Kind::Lava;
	const bool water = surface.kind == Surface::Kind::Water;
	const bool modern = this->options.modernLighting;
	const bool occlusion = this->options.ambientOcclusion && (surface.aoMask != 0 || surface.aoFloorY > -1e8f || surface.aoCeilingY < 1e8f);
	const Vec3 eye = this->camera.position;
	const float t = this->time;
	const uint8_t tag = water ? TagWater : surface.kind == Surface::Kind::Lava ? TagGlow : surface.kind == Surface::Kind::Figure ? TagNoBloom : TagNone;

	for (int y = minY; y <= maxY; y++)
	{
		const float py = y + 0.5f;
		float px = minX + 0.5f;
		// w = cross(aresta, P - início): positivo dentro (área já normalizada para > 0).
		float w0 = e0dx * (py - p1.y) - e0dy * (px - p1.x);
		float w1 = e1dx * (py - p2.y) - e1dy * (px - p2.x);
		float w2 = e2dx * (py - p0.y) - e2dy * (px - p0.x);

		const size_t rowOffset = static_cast<size_t>(y) * static_cast<size_t>(this->width);
		uint32_t *colorRow = this->color.data() + rowOffset;
		float *depthRow = this->depth.data() + rowOffset;
		uint8_t *tagRow = this->tags.data() + rowOffset;

		for (int x = minX; x <= maxX; x++, w0 -= e0dy, w1 -= e1dy, w2 -= e2dy)
		{
			if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
			{
				continue;
			}

			const float l0 = w0 * invArea, l1 = w1 * invArea, l2 = w2 * invArea;
			const float iz = l0 * p0.iz + l1 * p1.iz + l2 * p2.iz;
			if (iz <= depthRow[x])
			{
				continue;
			}

			const float z = 1.0f / iz;
			const float wx = (l0 * p0.wxz + l1 * p1.wxz + l2 * p2.wxz) * z;
			const float wy = (l0 * p0.wyz + l1 * p1.wyz + l2 * p2.wyz) * z;
			const float wz = (l0 * p0.wzz + l1 * p1.wzz + l2 * p2.wzz) * z;
			float u, v;
			if (liquid)
			{
				// Coordenadas pelo mundo, com a correnteza e uma leve distorção das ondas.
				const float flow = water ? 0.05f : 0.02f;
				u = wx * LiquidScaleU + t * flow + 0.012f * std::sin(wz * 5.0f + t * 1.7f);
				v = wz * LiquidScaleV + 0.02f * std::sin(wx * 4.3f + t * 1.3f);
			}
			else
			{
				u = (l0 * p0.uz + l1 * p1.uz + l2 * p2.uz) * z;
				v = (l0 * p0.vz + l1 * p1.vz + l2 * p2.vz) * z;
			}

			const int tx = wrapU(fastFloor(u * tw));
			const int ty = wrapV(fastFloor(v * th));
			float tr, tg, tb;
			float windowFrac = 0.0f;
			if (smooth)
			{
				// Texturas ampliadas: filtro bilinear; em sprites, só os texels opacos contam.
				const float fu = u * tw - 0.5f, fv = v * th - 0.5f;
				const int bx = fastFloor(fu), by = fastFloor(fv);
				const float ax = fu - bx, ay = fv - by;
				const int sx0 = wrapU(bx), sx1 = wrapU(bx + 1), sy0 = wrapV(by) * tw, sy1 = wrapV(by + 1) * tw;
				const uint32_t taps[4] = { texels[sy0 + sx0], texels[sy0 + sx1], texels[sy1 + sx0], texels[sy1 + sx1] };
				float acc[3] = { 0, 0, 0 }, weightSum = 0.0f;
				for (int k = 0; k < 4; k++)
				{
					const uint32_t s = taps[k];
					if (alphaTest && (s >> 24) == 0) continue;
					const float wgt = ((k & 1) ? ax : 1.0f - ax) * ((k >> 1) ? ay : 1.0f - ay);
					if (!alphaTest && (s >> 24) == 254u)
					{
						// Vidro de janela: cor da hora.
						acc[0] += this->windowColor[0] * wgt; acc[1] += this->windowColor[1] * wgt; acc[2] += this->windowColor[2] * wgt;
						windowFrac += wgt;
					}
					else
					{
						acc[0] += (s & 0xFF) * wgt; acc[1] += ((s >> 8) & 0xFF) * wgt; acc[2] += ((s >> 16) & 0xFF) * wgt;
					}
					weightSum += wgt;
				}
				if (weightSum < 0.5f) continue;
				tr = acc[0] / weightSum; tg = acc[1] / weightSum; tb = acc[2] / weightSum;
			}
			else
			{
				const uint32_t texel = texels[ty * tw + tx];
				if (alphaTest && (texel >> 24) == 0)
				{
					continue;
				}

				tr = static_cast<float>(texel & 0xFF);
				tg = static_cast<float>((texel >> 8) & 0xFF);
				tb = static_cast<float>((texel >> 16) & 0xFF);
				if (!alphaTest && (texel >> 24) == 254u) { tr = this->windowColor[0]; tg = this->windowColor[1]; tb = this->windowColor[2]; windowFrac = 1.0f; }
			}

			if (surface.kind == Surface::Kind::Lava)
			{
				// Lava brilha sozinha e pulsa devagar.
				const float pulse = 1.05f + 0.1f * std::sin(t * 2.0f + wx * 1.3f + wz * 0.7f);
				colorRow[x] = pack(tr * pulse, tg * pulse, tb * pulse);
				depthRow[x] = iz;
				tagRow[x] = tag;
				continue;
			}

			// Oclusão de ambiente: escurece bordas junto às paredes e os cantos.
			float ao = 1.0f;
			if (occlusion)
			{
				if (surface.aoMask & 1) ao *= edgeOcclusion(u);
				if (surface.aoMask & 2) ao *= edgeOcclusion(1.0f - u);
				if (surface.aoMask & 4) ao *= edgeOcclusion(v);
				if (surface.aoMask & 8) ao *= edgeOcclusion(1.0f - v);
				ao *= edgeOcclusion((wy - surface.aoFloorY) * 1.6f);
				ao *= edgeOcclusion((surface.aoCeilingY - wy) * 2.2f);
			}

			Vec3 n = water ? waveNormal(wx, wz, t) : normal;
			if (normalMap && !water)
			{
				// Relevo do mapa de normais no espaço da face.
				const int8_t *m = normalMap + static_cast<size_t>((ty * tw + tx) * 3);
				const float mx = m[0] / 127.0f, my = m[1] / 127.0f, mz = m[2] / 127.0f;
				// Base ortonormal e normal do mapa já unitária: dispensa normalizar de novo.
				n = { tangent.x * mx + bitangent.x * my + normal.x * mz, tangent.y * mx + bitangent.y * my + normal.y * mz,
					tangent.z * mx + bitangent.z * my + normal.z * mz };
			}
			Vec3 view{ eye.x - wx, eye.y - wy, eye.z - wz };
			if (water || normalMap) view = normalize(view);

			// A água capta um pouco mais da luz ambiente (continua visível longe da tocha).
			float lr = this->ambient * ao * (water ? 3.0f : 1.0f), lg = lr, lb = lr;
			float sr = 0.0f, sg = 0.0f, sb = 0.0f;
			for (size_t i = 0; i < lightCount; i++)
			{
				const PointLight &light = lightData[i];
				const float dx = light.position.x - wx, dy = light.position.y - wy, dz = light.position.z - wz;
				const float dist2 = dx * dx + dy * dy + dz * dz;
				const float falloff = 1.0f - dist2 / (light.radius * light.radius);
				if (falloff <= 0.0f) continue;

				float k = falloff * falloff;
				if (modern || water || normalMap)
				{
					const float inv = 1.0f / std::sqrt(dist2 + 1e-4f);
					const Vec3 l{ dx * inv, dy * inv, dz * inv };
					if (modern) k *= 0.62f + 0.6f * std::max(0.0f, dot(n, l));
					if (normalMap && !water)
					{
						// Brilho leve de pedra úmida.
						const Vec3 hh = normalize({ l.x + view.x, l.y + view.y, l.z + view.z });
						const float s = std::max(0.0f, dot(n, hh));
						const float s2 = s * s, s4 = s2 * s2, s8 = s4 * s4, s16 = s8 * s8;
						const float spec = s16 * falloff * falloff * 55.0f;
						sr += spec * light.r; sg += spec * light.g; sb += spec * light.b;
					}
					if (water)
					{
						// Brilho especular das ondas (Blinn-Phong).
						const Vec3 h = normalize({ l.x + view.x, l.y + view.y, l.z + view.z });
						const float s = std::max(0.0f, dot(n, h));
						const float s2 = s * s, s4 = s2 * s2, s8 = s4 * s4, s16 = s8 * s8, s32 = s16 * s16;
						const float spec = s32 * s16 * falloff * 230.0f;
						sr += spec * light.r; sg += spec * light.g; sb += spec * light.b;
					}
				}

				lr += light.r * k * ao; lg += light.g * k * ao; lb += light.b * k * ao;
			}

			float r = tr * lr, g = tg * lg, bl = tb * lb;
			if (surface.kind == Surface::Kind::Sky)
			{
				r = tr; g = tg; bl = tb;
				tagRow[x] = TagNone;
			}
			else if (surface.kind == Surface::Kind::Emissive)
			{
				// Partes claras (chamas) ignoram a escuridão.
				const float luma = (tr * 0.3f + tg * 0.59f + tb * 0.11f) / 255.0f;
				const float self = std::clamp((luma - 0.45f) / 0.35f, 0.0f, 1.0f);
				r = std::max(r, tr * self * 1.1f); g = std::max(g, tg * self * 1.1f); bl = std::max(bl, tb * self * 1.1f);
				if (self > 0.3f) tagRow[x] = TagGlow; else tagRow[x] = TagNone;
			}
			else
			{
				tagRow[x] = tag;
			}

			if (windowFrac > 0.0f && this->windowGlow > 0.0f)
			{
				// Janelas acesas à noite: brilham sozinhas e entram no bloom.
				const float k = windowFrac * this->windowGlow;
				r = std::max(r, tr * k); g = std::max(g, tg * k); bl = std::max(bl, tb * k);
				if (k > 0.5f) tagRow[x] = TagGlow;
			}

			if (water)
			{
				// Água um pouco mais escura e fria que a textura, com o brilho por cima.
				r = r * 0.95f + sr; g = g * 1.02f + sg; bl = bl * 1.1f + sb;
			}

			if (!water) { r += sr; g += sg; bl += sb; }
			colorRow[x] = pack(r, g, bl);
			depthRow[x] = iz;
		}
	}
}

void arena::Renderer3D::finishFrame()
{
	if (this->gpu)
	{
		// Agrupa os triângulos por textura (menos trocas de estado na GPU).
		const size_t triangles = this->gpuTextures.size();
		this->gpuOrder.resize(triangles);
		for (size_t i = 0; i < triangles; i++) this->gpuOrder[i] = static_cast<uint32_t>(i);
		std::stable_sort(this->gpuOrder.begin(), this->gpuOrder.end(),
			[this](uint32_t a, uint32_t b) { return this->gpuTextures[a]->id < this->gpuTextures[b]->id; });
		this->gpuSorted.resize(this->gpuVertices.size());
		this->gpuBatches.clear();
		for (size_t i = 0; i < triangles; i++)
		{
			const uint32_t source = this->gpuOrder[i];
			std::copy_n(this->gpuVertices.begin() + static_cast<std::ptrdiff_t>(source) * 3, 3, this->gpuSorted.begin() + static_cast<std::ptrdiff_t>(i) * 3);
			const Texture *texture = this->gpuTextures[source];
			if (this->gpuBatches.empty() || this->gpuBatches.back().texture != texture)
			{
				this->gpuBatches.push_back(GpuBatch{ texture, static_cast<uint32_t>(i * 3), 0 });
			}
			this->gpuBatches.back().count += 3;
		}

		GpuScene scene;
		scene.camera = &this->camera;
		scene.lights = &this->lights;
		scene.options = &this->options;
		scene.ambient = this->ambient;
		scene.time = this->time;
		for (int c = 0; c < 3; c++) scene.windowColor[c] = this->windowColor[c] / 255.0f;
		scene.windowGlow = this->windowGlow;
		scene.width = this->width;
		scene.height = this->height;
		scene.vertices = &this->gpuSorted;
		scene.batches = &this->gpuBatches;
		this->gpu->renderScene(scene);

		// O que vier depois (HUD, arma, janelas) desenha numa camada transparente.
		std::fill(this->color.begin(), this->color.end(), 0u);
		return;
	}

	// Da frente para trás: o z-buffer descarta cedo o que fica escondido (menos pixels sombreados).
	std::sort(this->queue.begin(), this->queue.end(), [](const QueuedTriangle &a, const QueuedTriangle &b) { return a.nearest < b.nearest; });

	const size_t w = static_cast<size_t>(this->width);
	this->forEachBand(this->height, [this, w](int y0, int y1)
	{
		const size_t begin = static_cast<size_t>(y0) * w, end = static_cast<size_t>(y1 + 1) * w;
		std::fill(this->color.begin() + static_cast<std::ptrdiff_t>(begin), this->color.begin() + static_cast<std::ptrdiff_t>(end), 0xFF000000u);
		std::fill(this->depth.begin() + static_cast<std::ptrdiff_t>(begin), this->depth.begin() + static_cast<std::ptrdiff_t>(end), 0.0f);
		std::fill(this->tags.begin() + static_cast<std::ptrdiff_t>(begin), this->tags.begin() + static_cast<std::ptrdiff_t>(end), static_cast<uint8_t>(TagNone));
		for (const QueuedTriangle &triangle : this->queue)
		{
			if (triangle.maxY < static_cast<float>(y0) - 1.0f || triangle.minY > static_cast<float>(y1) + 1.0f) continue;
			this->rasterizeTriangle(triangle, y0, y1);
		}
	});

	if (this->options.reflections)
	{
		// Calcula em scratch (só pixels de água) e copia de volta, sem duplicar o quadro inteiro.
		this->scratch.resize(this->color.size());
		this->forEachBand(this->height, [this](int y0, int y1) { this->applyReflections(y0, y1); });
		this->forEachBand(this->height, [this, w](int y0, int y1)
		{
			for (size_t i = static_cast<size_t>(y0) * w; i < static_cast<size_t>(y1 + 1) * w; i++)
			{
				if (this->tags[i] == TagWater) this->color[i] = this->scratch[i];
			}
		});
	}

	if (this->options.bloom) this->applyBloom();
	if (this->options.vignette) this->applyVignette();
}

void arena::Renderer3D::applyReflections(int bandMinY, int bandMaxY)
{
	// Reflexos em espaço de tela: o raio refletido avança pelo z-buffer até encontrar a cena.
	const int w = this->width, h = this->height;
	const float cx = w * 0.5f, cy = h * 0.5f;
	const Vec3 eye = this->camera.position;

	auto project = [&](const Vec3 &p, float &sx, float &sy, float &iz)
	{
		const Vec3 d{ p.x - eye.x, p.y - eye.y, p.z - eye.z };
		const float vz = dot(d, this->forward);
		if (vz < NearPlane) return false;
		iz = 1.0f / vz;
		sx = cx + dot(d, this->right) * iz * this->focal;
		sy = cy - dot(d, this->up) * iz * this->focal;
		return sx >= 0.0f && sy >= 0.0f && sx < w && sy < h;
	};

	for (int y = bandMinY; y <= bandMaxY; y++)
	{
		// Xadrez: metade dos pixels marcha o raio; a outra metade reaproveita o vizinho da esquerda.
		uint32_t lastHitColor = 0;
		float lastHitFade = 0.0f;
		int lastX = -10;
		for (int x = 0; x < w; x++)
		{
			const size_t index = static_cast<size_t>(y) * w + x;
			if (this->tags[index] != TagWater) continue;

			// Ponto da água reconstruído a partir da profundidade.
			const float vz = 1.0f / this->depth[index];
			const float vx = (x + 0.5f - cx) / this->focal * vz;
			const float vy = -(y + 0.5f - cy) / this->focal * vz;
			const Vec3 p{ eye.x + this->right.x * vx + this->up.x * vy + this->forward.x * vz,
				eye.y + this->right.y * vx + this->up.y * vy + this->forward.y * vz,
				eye.z + this->right.z * vx + this->up.z * vy + this->forward.z * vz };
			const Vec3 n = waveNormal(p.x, p.z, this->time);
			const Vec3 dir = normalize({ p.x - eye.x, p.y - eye.y, p.z - eye.z });
			const float dn = dot(dir, n);
			Vec3 r{ dir.x - 2.0f * dn * n.x, dir.y - 2.0f * dn * n.y, dir.z - 2.0f * dn * n.z };
			r.y = std::max(r.y, 0.02f);

			// Marcha com passos crescentes e refinamento binário no cruzamento.
			uint32_t hitColor = 0;
			float hitFade = 0.0f;
			const bool reuse = ((x + y) & 1) != 0 && lastX == x - 1;
			if (reuse)
			{
				hitColor = lastHitColor;
				hitFade = lastHitFade;
			}
			float prevT = 0.0f, stepT = 0.04f;
			for (int i = 0; i < 26 && !reuse; i++)
			{
				const float tt = prevT + stepT;
				float sx, sy, iz;
				if (!project({ p.x + r.x * tt, p.y + r.y * tt, p.z + r.z * tt }, sx, sy, iz)) break;
				const size_t s = static_cast<size_t>(sy) * w + static_cast<size_t>(sx);
				const float sceneIz = this->depth[s];
				if (sceneIz > iz && 1.0f / sceneIz > 1.0f / iz - stepT * 2.5f - 0.1f)
				{
					float lo = prevT, hi = tt;
					for (int k = 0; k < 4; k++)
					{
						const float mid = (lo + hi) * 0.5f;
						float mx, my, miz;
						if (project({ p.x + r.x * mid, p.y + r.y * mid, p.z + r.z * mid }, mx, my, miz)
							&& this->depth[static_cast<size_t>(my) * w + static_cast<size_t>(mx)] > miz) hi = mid;
						else lo = mid;
					}

					float fx, fy, fiz;
					if (project({ p.x + r.x * hi, p.y + r.y * hi, p.z + r.z * hi }, fx, fy, fiz))
					{
						const size_t f = static_cast<size_t>(fy) * w + static_cast<size_t>(fx);
						if (this->tags[f] != TagWater)
						{
							hitColor = this->color[f];
							// Some aos poucos perto das bordas da tela e com a distância percorrida.
							const float edge = std::min({ fx, w - fx, fy, h - fy }) / (0.08f * h);
							hitFade = std::clamp(edge, 0.0f, 1.0f) * std::clamp(1.0f - hi / 6.0f, 0.0f, 1.0f);
						}
					}
					break;
				}

				prevT = tt;
				stepT *= 1.22f;
			}

			if (!reuse)
			{
				lastHitColor = hitColor;
				lastHitFade = hitFade;
				lastX = x;
			}

			// Fresnel: reflete mais quando se olha rente à superfície.
			const float cosine = std::max(0.0f, -dn);
			const float m = 1.0f - cosine;
			const float fresnel = 0.3f + 0.65f * m * m;
			const uint32_t base = this->color[index];
			float r0 = static_cast<float>(base & 0xFF), g0 = static_cast<float>((base >> 8) & 0xFF), b0 = static_cast<float>((base >> 16) & 0xFF);
			if (hitFade > 0.0f)
			{
				const float k = fresnel * hitFade;
				r0 = r0 * (1.0f - 0.3f * k) + static_cast<float>(hitColor & 0xFF) * 0.75f * k;
				g0 = g0 * (1.0f - 0.3f * k) + static_cast<float>((hitColor >> 8) & 0xFF) * 0.8f * k;
				b0 = b0 * (1.0f - 0.3f * k) + static_cast<float>((hitColor >> 16) & 0xFF) * 0.9f * k;
			}
			this->scratch[index] = pack(r0, g0, b0);
		}
	}
}

void arena::Renderer3D::applyBloom()
{
	// Partes muito claras (e chamas/lava) espalham luz: reduz 4x, desfoca e soma de volta.
	const int w = this->width, h = this->height;
	const int bw = (w + 3) / 4, bh = (h + 3) / 4;
	this->bloomA.assign(static_cast<size_t>(bw) * bh * 3, 0.0f);
	this->bloomB.assign(this->bloomA.size(), 0.0f);
	std::atomic<bool> any{ false };
	this->forEachBand(bh, [&](int r0, int r1)
	{
		bool found = false;
		for (int y = r0 * 4; y < std::min(h, (r1 + 1) * 4); y++)
		{
			const size_t row = static_cast<size_t>(y / 4) * bw;
			for (int x = 0; x < w; x++)
			{
				const size_t index = static_cast<size_t>(y) * w + x;
				const uint32_t p = this->color[index];
				const float r = static_cast<float>(p & 0xFF), g = static_cast<float>((p >> 8) & 0xFF), b = static_cast<float>((p >> 16) & 0xFF);
				const float luma = (r * 0.3f + g * 0.59f + b * 0.11f) / 255.0f;
				float k = std::max(0.0f, luma - 0.78f) / 0.22f;
				if (this->tags[index] == TagGlow) k = std::max(k, 0.55f);
				else if (this->tags[index] == TagNoBloom) k = 0.0f;
				if (k <= 0.0f) continue;
				found = true;
				float *dst = &this->bloomA[(row + static_cast<size_t>(x / 4)) * 3];
				dst[0] += r * k / 16.0f; dst[1] += g * k / 16.0f; dst[2] += b * k / 16.0f;
			}
		}
		if (found) any = true;
	});

	if (!any) return;

	auto blur = [&](const std::vector<float> &src, std::vector<float> &dst, int dx, int dy)
	{
		this->forEachBand(bh, [&](int r0, int r1)
		{
			constexpr int Radius = 3;
			for (int y = r0; y <= r1; y++)
			{
				for (int x = 0; x < bw; x++)
				{
					float acc[3] = { 0, 0, 0 };
					for (int k = -Radius; k <= Radius; k++)
					{
						const int sx = std::clamp(x + k * dx, 0, bw - 1), sy = std::clamp(y + k * dy, 0, bh - 1);
						const float *p = &src[(static_cast<size_t>(sy) * bw + sx) * 3];
						acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2];
					}
					float *d = &dst[(static_cast<size_t>(y) * bw + x) * 3];
					d[0] = acc[0] / (2 * Radius + 1); d[1] = acc[1] / (2 * Radius + 1); d[2] = acc[2] / (2 * Radius + 1);
				}
			}
		});
	};

	for (int pass = 0; pass < 2; pass++)
	{
		blur(this->bloomA, this->bloomB, 1, 0);
		blur(this->bloomB, this->bloomA, 0, 1);
	}

	constexpr float Intensity = 1.4f;
	this->forEachBand(h, [&](int y0, int y1)
	{
		for (int y = y0; y <= y1; y++)
		{
			const float fy = std::clamp((y + 0.5f) / 4.0f - 0.5f, 0.0f, bh - 1.0f);
			const int by0 = static_cast<int>(fy), by1 = std::min(bh - 1, by0 + 1);
			const float ty = fy - by0;
			for (int x = 0; x < w; x++)
			{
				const float fx = std::clamp((x + 0.5f) / 4.0f - 0.5f, 0.0f, bw - 1.0f);
				const int bx0 = static_cast<int>(fx), bx1 = std::min(bw - 1, bx0 + 1);
				const float tx = fx - bx0;
				const float *a00 = &this->bloomA[(static_cast<size_t>(by0) * bw + bx0) * 3], *a10 = &this->bloomA[(static_cast<size_t>(by0) * bw + bx1) * 3];
				const float *a01 = &this->bloomA[(static_cast<size_t>(by1) * bw + bx0) * 3], *a11 = &this->bloomA[(static_cast<size_t>(by1) * bw + bx1) * 3];
				float add[3];
				for (int c = 0; c < 3; c++)
				{
					const float top = a00[c] + (a10[c] - a00[c]) * tx, bottom = a01[c] + (a11[c] - a01[c]) * tx;
					add[c] = (top + (bottom - top) * ty) * Intensity;
				}
				if (add[0] + add[1] + add[2] < 0.5f) continue;

				uint32_t &p = this->color[static_cast<size_t>(y) * w + x];
				p = pack(static_cast<float>(p & 0xFF) + add[0], static_cast<float>((p >> 8) & 0xFF) + add[1], static_cast<float>((p >> 16) & 0xFF) + add[2]);
			}
		}
	});
}

void arena::Renderer3D::applyVignette()
{
	this->forEachBand(this->height, [this](int y0, int y1)
	{
		for (int y = y0; y <= y1; y++)
		{
			const float vy = this->vignetteY[static_cast<size_t>(y)];
			uint32_t *row = this->color.data() + static_cast<size_t>(y) * this->width;
			for (int x = 0; x < this->width; x++)
			{
				const float k = vy * this->vignetteX[static_cast<size_t>(x)];
				const uint32_t p = row[x];
				row[x] = pack((p & 0xFF) * k, ((p >> 8) & 0xFF) * k, ((p >> 16) & 0xFF) * k);
			}
		}
	});
}
