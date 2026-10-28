#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include <SDL.h>
#include <SDL_opengl.h>

#include "../render/Canvas.h"
#include "GlDevice.h"

// Funções do OpenGL além da 1.1 (carregadas pelo SDL em tempo de execução).
#define ARENA_GL_FUNCTIONS(X) \
	X(PFNGLACTIVETEXTUREPROC, ActiveTexture) \
	X(PFNGLCREATESHADERPROC, CreateShader) \
	X(PFNGLSHADERSOURCEPROC, ShaderSource) \
	X(PFNGLCOMPILESHADERPROC, CompileShader) \
	X(PFNGLGETSHADERIVPROC, GetShaderiv) \
	X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog) \
	X(PFNGLDELETESHADERPROC, DeleteShader) \
	X(PFNGLCREATEPROGRAMPROC, CreateProgram) \
	X(PFNGLATTACHSHADERPROC, AttachShader) \
	X(PFNGLLINKPROGRAMPROC, LinkProgram) \
	X(PFNGLGETPROGRAMIVPROC, GetProgramiv) \
	X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog) \
	X(PFNGLDELETEPROGRAMPROC, DeleteProgram) \
	X(PFNGLUSEPROGRAMPROC, UseProgram) \
	X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) \
	X(PFNGLUNIFORM1IPROC, Uniform1i) \
	X(PFNGLUNIFORM1FPROC, Uniform1f) \
	X(PFNGLUNIFORM2FPROC, Uniform2f) \
	X(PFNGLUNIFORM3FPROC, Uniform3f) \
	X(PFNGLUNIFORM3FVPROC, Uniform3fv) \
	X(PFNGLUNIFORM4FVPROC, Uniform4fv) \
	X(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv) \
	X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays) \
	X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray) \
	X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays) \
	X(PFNGLGENBUFFERSPROC, GenBuffers) \
	X(PFNGLBINDBUFFERPROC, BindBuffer) \
	X(PFNGLBUFFERDATAPROC, BufferData) \
	X(PFNGLDELETEBUFFERSPROC, DeleteBuffers) \
	X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer) \
	X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray) \
	X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers) \
	X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer) \
	X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D) \
	X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus) \
	X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers) \
	X(PFNGLBLITFRAMEBUFFERPROC, BlitFramebuffer) \
	X(PFNGLGENERATEMIPMAPPROC, GenerateMipmap)

namespace arena::gl
{
#define ARENA_GL_DECLARE(type, name) type name = nullptr;
	ARENA_GL_FUNCTIONS(ARENA_GL_DECLARE)
#undef ARENA_GL_DECLARE
}

namespace
{
	using namespace arena;

	constexpr float NearPlane = 0.05f;
	constexpr float FarPlane = 96.0f;
	constexpr int MaxLights = 64;

	// ---------------------------------------------------------------- shaders

	const char *FullscreenVertex = R"(#version 330 core
out vec2 vScene;   // coordenadas de textura das imagens da GPU (origem embaixo)
out vec2 vCpu;     // coordenadas de buffers vindos da CPU (primeira linha em cima)
void main()
{
	vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
	vScene = p * 0.5 + 0.5;
	vCpu = vec2(vScene.x, 1.0 - vScene.y);
	gl_Position = vec4(p, 0.0, 1.0);
}
)";

	const char *SceneVertex = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;
layout(location = 5) in vec4 aSurface;
uniform mat4 uViewProj;
out vec3 vWorld;
out vec2 vUv;
out vec3 vNormal;
out vec3 vTangent;
out vec3 vBitangent;
flat out vec4 vSurface;
void main()
{
	vWorld = aPos;
	vUv = aUv;
	vNormal = aNormal;
	vTangent = aTangent;
	vBitangent = aBitangent;
	vSurface = aSurface;
	gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

	// Mesma iluminação do Renderer3D::rasterizeTriangle (valores de cor em 0..255).
	const char *SceneFragment = R"(#version 330 core
in vec3 vWorld;
in vec2 vUv;
in vec3 vNormal;
in vec3 vTangent;
in vec3 vBitangent;
flat in vec4 vSurface; // tipo, máscara de bordas, chão e teto da oclusão
uniform sampler2D uTexture;
uniform sampler2D uNormalMap;
uniform bool uHasNormalMap;
uniform bool uAlphaTest;
uniform bool uModern;
uniform bool uOcclusion;
uniform vec3 uEye;
uniform float uAmbient;
uniform float uTime;
uniform int uLightCount;
uniform vec3 uWindowColor;
uniform float uWindowGlow;
uniform vec4 uLightPos[64];   // xyz + raio
uniform vec3 uLightColor[64];
layout(location = 0) out vec4 outColor;

const float LiquidScaleU = 64.0 / 320.0;
const float LiquidScaleV = 64.0 / 100.0;
const float WaveAmplitude = 0.045;

vec3 waveNormal(float x, float z, float t)
{
	float a = x * 3.1 + t * 1.6, b = z * 4.3 - t * 1.2, c = (x + z) * 6.7 + t * 2.3, d = (x - z) * 9.1 - t * 3.1;
	float dx = 0.5 * 3.1 * cos(a) + 0.25 * 6.7 * cos(c) + 0.12 * 9.1 * cos(d);
	float dz = 0.35 * 4.3 * cos(b) + 0.25 * 6.7 * cos(c) - 0.12 * 9.1 * cos(d);
	return normalize(vec3(-dx * WaveAmplitude, 1.0, -dz * WaveAmplitude));
}

float edgeOcclusion(float d)
{
	float t = clamp(d / 0.45, 0.0, 1.0);
	return 0.5 + 0.5 * t * t * (3.0 - 2.0 * t);
}

void main()
{
	int kind = int(vSurface.x + 0.5);
	bool water = kind == 1;
	bool lava = kind == 2;
	bool emissive = kind == 3;

	vec2 uv = vUv;
	if (water || lava)
	{
		float flow = water ? 0.05 : 0.02;
		uv = vec2(vWorld.x * LiquidScaleU + uTime * flow + 0.012 * sin(vWorld.z * 5.0 + uTime * 1.7),
			vWorld.z * LiquidScaleV + 0.02 * sin(vWorld.x * 4.3 + uTime * 1.3));
	}

	vec4 texel = texture(uTexture, uv);
	if (uAlphaTest && texel.a < 0.5) discard;
	// Vidro das janelas (alfa 254): cor da hora; perto da borda, misturado pelo filtro.
	float windowFrac = uAlphaTest ? 0.0 : clamp((1.0 - texel.a) * 255.0, 0.0, 1.0);
	vec3 base = mix(texel.rgb, uWindowColor, windowFrac) * 255.0;

	if (kind == 4)
	{
		// Céu: a cor da textura, sem luz.
		outColor = vec4(texel.rgb, 0.0);
		return;
	}

	if (lava)
	{
		float pulse = 1.05 + 0.1 * sin(uTime * 2.0 + vWorld.x * 1.3 + vWorld.z * 0.7);
		outColor = vec4(min(base * pulse, vec3(255.0)) / 255.0, 1.0);
		return;
	}

	float ao = 1.0;
	if (uOcclusion)
	{
		int mask = int(vSurface.y + 0.5);
		if ((mask & 1) != 0) ao *= edgeOcclusion(vUv.x);
		if ((mask & 2) != 0) ao *= edgeOcclusion(1.0 - vUv.x);
		if ((mask & 4) != 0) ao *= edgeOcclusion(vUv.y);
		if ((mask & 8) != 0) ao *= edgeOcclusion(1.0 - vUv.y);
		ao *= edgeOcclusion((vWorld.y - vSurface.z) * 1.6);
		ao *= edgeOcclusion((vSurface.w - vWorld.y) * 2.2);
	}

	bool relief = uHasNormalMap && uModern && !water;
	vec3 n = water ? waveNormal(vWorld.x, vWorld.z, uTime) : normalize(vNormal);
	if (relief)
	{
		vec3 m = texture(uNormalMap, uv).xyz * 2.0 - 1.0;
		n = normalize(vTangent * m.x + vBitangent * m.y + vNormal * m.z);
	}

	vec3 viewDir = normalize(uEye - vWorld);
	vec3 light = vec3(uAmbient * ao * (water ? 3.0 : 1.0));
	vec3 spec = vec3(0.0);
	for (int i = 0; i < uLightCount; i++)
	{
		vec3 d = uLightPos[i].xyz - vWorld;
		float dist2 = dot(d, d);
		float radius = uLightPos[i].w;
		float falloff = 1.0 - dist2 / (radius * radius);
		if (falloff <= 0.0) continue;

		float k = falloff * falloff;
		if (uModern || water || relief)
		{
			vec3 l = d * inversesqrt(dist2 + 1e-4);
			if (uModern) k *= 0.62 + 0.6 * max(0.0, dot(n, l));
			vec3 h = normalize(l + viewDir);
			float s = max(0.0, dot(n, h));
			if (relief) spec += pow(s, 16.0) * falloff * falloff * 55.0 * uLightColor[i];
			if (water) spec += pow(s, 48.0) * falloff * 230.0 * uLightColor[i];
		}

		light += uLightColor[i] * k * ao;
	}

	vec3 color = base * light;
	float tag = kind == 5 ? 0.05 : 0.25; // geometria comum (figuras: fora do bloom)
	if (emissive)
	{
		// Partes claras (chamas) ignoram a escuridão.
		float luma = dot(base, vec3(0.3, 0.59, 0.11)) / 255.0;
		float self = clamp((luma - 0.45) / 0.35, 0.0, 1.0);
		color = max(color, base * self * 1.1);
		if (self > 0.3) tag = 1.0;
	}

	if (windowFrac > 0.0 && uWindowGlow > 0.0)
	{
		float k = windowFrac * uWindowGlow;
		color = max(color, base * k);
		if (k > 0.5) tag = 1.0;
	}

	if (water)
	{
		color = color * vec3(0.95, 1.02, 1.1) + spec;
		tag = 0.5;
	}
	else
	{
		color += spec;
	}

	outColor = vec4(min(color, vec3(255.0)) / 255.0, tag);
}
)";

	// Reflexos em espaço de tela (mesma marcha do Renderer3D::applyReflections).
	const char *ReflectionFragment = R"(#version 330 core
in vec2 vScene;
uniform sampler2D uColor;
uniform sampler2D uDepth;
uniform vec3 uEye;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec3 uForward;
uniform vec2 uTan;     // tan(fov/2) horizontal e vertical
uniform float uTime;
uniform float uNear;
uniform float uFar;
out vec4 outColor;

const float WaveAmplitude = 0.045;

vec3 waveNormal(float x, float z, float t)
{
	float a = x * 3.1 + t * 1.6, b = z * 4.3 - t * 1.2, c = (x + z) * 6.7 + t * 2.3, d = (x - z) * 9.1 - t * 3.1;
	float dx = 0.5 * 3.1 * cos(a) + 0.25 * 6.7 * cos(c) + 0.12 * 9.1 * cos(d);
	float dz = 0.35 * 4.3 * cos(b) + 0.25 * 6.7 * cos(c) - 0.12 * 9.1 * cos(d);
	return normalize(vec3(-dx * WaveAmplitude, 1.0, -dz * WaveAmplitude));
}

float linearDepth(vec2 uv)
{
	float d = texture(uDepth, uv).r * 2.0 - 1.0;
	return 2.0 * uNear * uFar / (uFar + uNear - d * (uFar - uNear));
}

bool project(vec3 p, out vec2 uv, out float depth)
{
	vec3 d = p - uEye;
	depth = dot(d, uForward);
	if (depth < uNear) return false;
	uv = vec2(dot(d, uRight) / (depth * uTan.x), dot(d, uUp) / (depth * uTan.y)) * 0.5 + 0.5;
	return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThan(uv, vec2(1.0)));
}

void main()
{
	vec4 base = texture(uColor, vScene);
	outColor = base;
	if (abs(base.a - 0.5) > 0.1) return; // só água

	float vz = linearDepth(vScene);
	vec2 ndc = vScene * 2.0 - 1.0;
	vec3 p = uEye + uRight * (ndc.x * uTan.x * vz) + uUp * (ndc.y * uTan.y * vz) + uForward * vz;
	vec3 n = waveNormal(p.x, p.z, uTime);
	vec3 dir = normalize(p - uEye);
	float dn = dot(dir, n);
	vec3 r = dir - 2.0 * dn * n;
	r.y = max(r.y, 0.02);

	vec3 hit = vec3(0.0);
	float hitFade = 0.0;
	float prevT = 0.0, stepT = 0.04;
	for (int i = 0; i < 26; i++)
	{
		float t = prevT + stepT;
		vec2 uv; float depth;
		if (!project(p + r * t, uv, depth)) break;
		float scene = linearDepth(uv);
		if (scene < depth && scene > depth - stepT * 2.5 - 0.1)
		{
			float lo = prevT, hi = t;
			for (int k = 0; k < 4; k++)
			{
				float mid = (lo + hi) * 0.5;
				vec2 muv; float mdepth;
				if (project(p + r * mid, muv, mdepth) && linearDepth(muv) < mdepth) hi = mid; else lo = mid;
			}

			vec2 fuv; float fdepth;
			if (project(p + r * hi, fuv, fdepth))
			{
				vec4 c = texture(uColor, fuv);
				if (abs(c.a - 0.5) > 0.1)
				{
					hit = c.rgb;
					float edge = min(min(fuv.x, 1.0 - fuv.x) * (uTan.x / uTan.y), min(fuv.y, 1.0 - fuv.y)) / 0.08;
					hitFade = clamp(edge, 0.0, 1.0) * clamp(1.0 - hi / 6.0, 0.0, 1.0);
				}
			}
			break;
		}

		prevT = t;
		stepT *= 1.22;
	}

	float m = 1.0 - max(0.0, -dn);
	float k = (0.3 + 0.65 * m * m) * hitFade;
	outColor = vec4(base.rgb * (1.0 - 0.3 * k) + hit * vec3(0.75, 0.8, 0.9) * k, base.a);
}
)";

	// Brilho: média 4x4 das partes claras (ou marcadas como chama/lava) em 1/4 da resolução.
	const char *BrightFragment = R"(#version 330 core
in vec2 vScene;
uniform sampler2D uColor;
uniform vec2 uTexel; // 1 / tamanho da imagem de origem
out vec4 outColor;
void main()
{
	vec3 sum = vec3(0.0);
	for (int y = 0; y < 4; y++)
	{
		for (int x = 0; x < 4; x++)
		{
			vec4 c = texture(uColor, vScene + (vec2(x, y) - 1.5) * uTexel);
			float luma = dot(c.rgb, vec3(0.3, 0.59, 0.11));
			float k = max(0.0, luma - 0.78) / 0.22;
			if (c.a > 0.75) k = max(k, 0.55);
			else if (c.a > 0.02 && c.a < 0.1) k = 0.0; // figuras (pessoas e criaturas)
			sum += c.rgb * k;
		}
	}
	outColor = vec4(sum / 16.0, 1.0);
}
)";

	const char *BlurFragment = R"(#version 330 core
in vec2 vScene;
uniform sampler2D uSource;
uniform vec2 uStep;
out vec4 outColor;
void main()
{
	vec3 sum = vec3(0.0);
	for (int k = -3; k <= 3; k++) sum += texture(uSource, vScene + uStep * float(k)).rgb;
	outColor = vec4(sum / 7.0, 1.0);
}
)";

	// Cena + bloom + vinheta + HUD, com o fade entre telas.
	const char *ComposeFragment = R"(#version 330 core
in vec2 vScene;
in vec2 vCpu;
uniform sampler2D uColor;
uniform sampler2D uBloom;
uniform sampler2D uOverlay;
uniform bool uUseBloom;
uniform bool uVignette;
uniform float uBrightness;
out vec4 outColor;
void main()
{
	vec3 c = texture(uColor, vScene).rgb;
	if (uUseBloom) c += texture(uBloom, vScene).rgb * 1.4;
	c = min(c, vec3(1.0));
	if (uVignette)
	{
		vec2 d = abs(vScene * 2.0 - 1.0);
		c *= (1.0 - 0.1 * pow(d.x, 2.4)) * (1.0 - 0.08 * pow(d.y, 2.4));
	}
	vec4 hud = texture(uOverlay, vCpu);
	c = mix(c, hud.rgb, hud.a);
	outColor = vec4(c * uBrightness, 1.0);
}
)";

	// Telas 2D: "sharp bilinear" (pixels nítidos, bordas suavizadas só na transição).
	const char *CanvasFragment = R"(#version 330 core
in vec2 vCpu;
uniform sampler2D uCanvas;
uniform vec2 uSize;
uniform float uScale;
uniform int uMode; // 0 nearest, 1 linear, 2 sharp
out vec4 outColor;
void main()
{
	if (uMode == 0)
	{
		outColor = texelFetch(uCanvas, ivec2(min(vCpu * uSize, uSize - 1.0)), 0);
		return;
	}

	vec2 uv = vCpu;
	if (uMode == 2)
	{
		vec2 texel = vCpu * uSize;
		vec2 cell = floor(texel);
		vec2 center = fract(texel) - 0.5;
		vec2 region = vec2(0.5 - 0.5 / uScale);
		vec2 f = (center - clamp(center, -region, region)) * uScale + 0.5;
		uv = (cell + f) / uSize;
	}
	outColor = texture(uCanvas, uv);
}
)";

	// ---------------------------------------------------------------- utilitários

	GLuint compile(GLenum type, const char *source, std::string &error)
	{
		const GLuint shader = gl::CreateShader(type);
		gl::ShaderSource(shader, 1, &source, nullptr);
		gl::CompileShader(shader);
		GLint ok = 0;
		gl::GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
		if (!ok)
		{
			char log[2048] = {};
			gl::GetShaderInfoLog(shader, sizeof(log), nullptr, log);
			error = log;
			gl::DeleteShader(shader);
			return 0;
		}

		return shader;
	}

	GLuint link(const char *vertex, const char *fragment, std::string &error)
	{
		const GLuint vs = compile(GL_VERTEX_SHADER, vertex, error);
		const GLuint fs = vs ? compile(GL_FRAGMENT_SHADER, fragment, error) : 0;
		if (!vs || !fs) return 0;
		const GLuint program = gl::CreateProgram();
		gl::AttachShader(program, vs);
		gl::AttachShader(program, fs);
		gl::LinkProgram(program);
		gl::DeleteShader(vs);
		gl::DeleteShader(fs);
		GLint ok = 0;
		gl::GetProgramiv(program, GL_LINK_STATUS, &ok);
		if (!ok)
		{
			char log[2048] = {};
			gl::GetProgramInfoLog(program, sizeof(log), nullptr, log);
			error = log;
			gl::DeleteProgram(program);
			return 0;
		}

		return program;
	}

	GLuint makeTexture(int width, int height, GLint internalFormat, GLenum format, GLenum type, GLint filter)
	{
		GLuint id = 0;
		glGenTextures(1, &id);
		glBindTexture(GL_TEXTURE_2D, id);
		glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		return id;
	}

	// Alvo de renderização: textura de cor (e opcionalmente profundidade).
	struct Target
	{
		GLuint framebuffer = 0, color = 0, depth = 0;
		int width = 0, height = 0;

		void release()
		{
			if (this->framebuffer) gl::DeleteFramebuffers(1, &this->framebuffer);
			if (this->color) glDeleteTextures(1, &this->color);
			if (this->depth) glDeleteTextures(1, &this->depth);
			*this = Target{};
		}

		void ensure(int w, int h, GLint colorFormat, GLenum colorType, bool withDepth)
		{
			if (this->framebuffer && this->width == w && this->height == h) return;
			this->release();
			this->width = w;
			this->height = h;
			this->color = makeTexture(w, h, colorFormat, GL_RGBA, colorType, GL_LINEAR);
			gl::GenFramebuffers(1, &this->framebuffer);
			gl::BindFramebuffer(GL_FRAMEBUFFER, this->framebuffer);
			gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, this->color, 0);
			if (withDepth)
			{
				this->depth = makeTexture(w, h, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GL_NEAREST);
				gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, this->depth, 0);
			}
		}

		void bind() const
		{
			gl::BindFramebuffer(GL_FRAMEBUFFER, this->framebuffer);
			glViewport(0, 0, this->width, this->height);
		}
	};
}

struct arena::GlDevice::Impl
{
	GLuint sceneProgram = 0, reflectionProgram = 0, brightProgram = 0, blurProgram = 0, composeProgram = 0, canvasProgram = 0;
	GLuint sceneVao = 0, sceneVbo = 0, emptyVao = 0;
	size_t sceneVboSize = 0;
	Target scene, reflected, bloomA, bloomB, final;
	GLuint overlayTexture = 0, canvasTexture = 0;
	int overlayW = 0, overlayH = 0;
	bool sceneValid = false;
	bool useBloom = false, useVignette = false, useReflections = false;
	GLuint sceneOutput = 0; // textura de cor da cena já com reflexos

	struct CachedTexture { GLuint color = 0, normals = 0; uint64_t lastUsed = 0; };
	std::unordered_map<uint32_t, CachedTexture> textures;
	uint64_t frame = 0;

	// Uniformes da cena
	GLint uViewProj = -1, uTexture = -1, uNormalMap = -1, uHasNormalMap = -1, uAlphaTest = -1, uModern = -1, uOcclusion = -1,
		uEye = -1, uAmbient = -1, uTime = -1, uWindowColor = -1, uWindowGlow = -1, uLightCount = -1, uLightPos = -1, uLightColor = -1;

	GLint uniform(GLuint program, const char *name) const { return gl::GetUniformLocation(program, name); }

	const CachedTexture &upload(const Texture &texture)
	{
		CachedTexture &entry = this->textures[texture.id];
		entry.lastUsed = this->frame;
		if (entry.color) return entry;

		// Texels transparentes herdam a cor de um vizinho opaco: o filtro bilinear não escurece as bordas.
		std::vector<uint32_t> pixels = texture.pixels;
		if (texture.hasTransparency)
		{
			const int w = texture.width, h = texture.height;
			for (int pass = 0; pass < 2; pass++)
			{
				std::vector<uint32_t> source = pixels;
				for (int y = 0; y < h; y++)
				{
					for (int x = 0; x < w; x++)
					{
						uint32_t &p = pixels[static_cast<size_t>(y) * w + x];
						if ((p >> 24) != 0) continue;
						for (int k = 0; k < 4; k++)
						{
							const int nx = x + (k == 0) - (k == 1), ny = y + (k == 2) - (k == 3);
							if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
							const uint32_t q = source[static_cast<size_t>(ny) * w + nx];
							if ((q >> 24) != 0 || (q & 0xFFFFFF) != 0) { p = q & 0x00FFFFFFu; break; }
						}
					}
				}
			}
		}

		// Estilo HD: filtro suave com mipmaps (sem cintilação à distância); clássico: pixels nítidos.
		const bool smooth = texture.scale > 1;
		const bool mipmaps = smooth && !texture.hasTransparency;
		glGenTextures(1, &entry.color);
		glBindTexture(GL_TEXTURE_2D, entry.color);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture.width, texture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : (smooth ? GL_LINEAR : GL_NEAREST));
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		if (mipmaps) gl::GenerateMipmap(GL_TEXTURE_2D);

		if (!texture.normals.empty())
		{
			std::vector<uint8_t> packed(static_cast<size_t>(texture.width) * texture.height * 4, 255);
			for (size_t i = 0; i < static_cast<size_t>(texture.width) * texture.height; i++)
			{
				for (int c = 0; c < 3; c++) packed[i * 4 + c] = static_cast<uint8_t>(std::clamp(texture.normals[i * 3 + c] / 127.0f * 127.5f + 127.5f, 0.0f, 255.0f));
			}
			glGenTextures(1, &entry.normals);
			glBindTexture(GL_TEXTURE_2D, entry.normals);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture.width, texture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, packed.data());
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			gl::GenerateMipmap(GL_TEXTURE_2D);
		}

		return entry;
	}

	// Libera texturas que não aparecem há um tempo (níveis antigos, quadros de criaturas mortas).
	void collectGarbage()
	{
		if (this->frame % 600 != 0) return;
		for (auto it = this->textures.begin(); it != this->textures.end();)
		{
			if (this->frame - it->second.lastUsed > 1800)
			{
				glDeleteTextures(1, &it->second.color);
				if (it->second.normals) glDeleteTextures(1, &it->second.normals);
				it = this->textures.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
};

arena::GlDevice::GlDevice() : impl(std::make_unique<Impl>())
{
}

arena::GlDevice::~GlDevice()
{
	if (this->context)
	{
		SDL_GL_DeleteContext(static_cast<SDL_GLContext>(this->context));
	}
}

bool arena::GlDevice::init(SDL_Window *targetWindow, bool vsync, float scale, std::string &outError)
{
	this->window = targetWindow;
	this->renderScale = std::clamp(scale, 0.25f, 2.0f);
	SDL_GLContext created = SDL_GL_CreateContext(targetWindow);
	if (!created)
	{
		outError = std::string("Contexto OpenGL 3.3 indisponível: ") + SDL_GetError();
		return false;
	}

	this->context = created;
	SDL_GL_MakeCurrent(targetWindow, created);
	SDL_GL_SetSwapInterval(vsync ? 1 : 0);

#define ARENA_GL_LOAD(type, name) \
	gl::name = reinterpret_cast<type>(SDL_GL_GetProcAddress("gl" #name)); \
	if (!gl::name) { outError = "Função OpenGL ausente: gl" #name; return false; }
	ARENA_GL_FUNCTIONS(ARENA_GL_LOAD)
#undef ARENA_GL_LOAD

	const char *vendor = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
	const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
	this->rendererName = std::string(vendor ? vendor : "?") + " (OpenGL " + (version ? version : "?") + ")";

	Impl &d = *this->impl;
	std::string error;
	d.sceneProgram = link(SceneVertex, SceneFragment, error);
	if (d.sceneProgram) d.reflectionProgram = link(FullscreenVertex, ReflectionFragment, error);
	if (d.reflectionProgram) d.brightProgram = link(FullscreenVertex, BrightFragment, error);
	if (d.brightProgram) d.blurProgram = link(FullscreenVertex, BlurFragment, error);
	if (d.blurProgram) d.composeProgram = link(FullscreenVertex, ComposeFragment, error);
	if (d.composeProgram) d.canvasProgram = link(FullscreenVertex, CanvasFragment, error);
	if (!d.canvasProgram)
	{
		outError = "Shader OpenGL não compilou: " + error;
		return false;
	}

	d.uViewProj = d.uniform(d.sceneProgram, "uViewProj");
	d.uTexture = d.uniform(d.sceneProgram, "uTexture");
	d.uNormalMap = d.uniform(d.sceneProgram, "uNormalMap");
	d.uHasNormalMap = d.uniform(d.sceneProgram, "uHasNormalMap");
	d.uAlphaTest = d.uniform(d.sceneProgram, "uAlphaTest");
	d.uModern = d.uniform(d.sceneProgram, "uModern");
	d.uOcclusion = d.uniform(d.sceneProgram, "uOcclusion");
	d.uEye = d.uniform(d.sceneProgram, "uEye");
	d.uAmbient = d.uniform(d.sceneProgram, "uAmbient");
	d.uTime = d.uniform(d.sceneProgram, "uTime");
	d.uWindowColor = d.uniform(d.sceneProgram, "uWindowColor");
	d.uWindowGlow = d.uniform(d.sceneProgram, "uWindowGlow");
	d.uLightCount = d.uniform(d.sceneProgram, "uLightCount");
	d.uLightPos = d.uniform(d.sceneProgram, "uLightPos");
	d.uLightColor = d.uniform(d.sceneProgram, "uLightColor");

	gl::GenVertexArrays(1, &d.emptyVao);
	gl::GenVertexArrays(1, &d.sceneVao);
	gl::GenBuffers(1, &d.sceneVbo);
	gl::BindVertexArray(d.sceneVao);
	gl::BindBuffer(GL_ARRAY_BUFFER, d.sceneVbo);
	const GLsizei stride = sizeof(GpuVertex);
	auto attribute = [stride](GLuint index, GLint size, size_t offset)
	{
		gl::EnableVertexAttribArray(index);
		gl::VertexAttribPointer(index, size, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void *>(offset));
	};
	attribute(0, 3, offsetof(GpuVertex, x));
	attribute(1, 2, offsetof(GpuVertex, u));
	attribute(2, 3, offsetof(GpuVertex, nx));
	attribute(3, 3, offsetof(GpuVertex, tx));
	attribute(4, 3, offsetof(GpuVertex, bx));
	attribute(5, 4, offsetof(GpuVertex, kind));
	gl::BindVertexArray(0);
	return true;
}

void arena::GlDevice::renderScene(const GpuScene &scene)
{
	Impl &d = *this->impl;
	d.frame++;
	const Camera &camera = *scene.camera;
	const GraphicsOptions &options = *scene.options;
	const int w = std::max(16, static_cast<int>(scene.width * this->renderScale));
	const int h = std::max(16, static_cast<int>(scene.height * this->renderScale));
	d.scene.ensure(w, h, GL_RGBA8, GL_UNSIGNED_BYTE, true);

	// Base da câmera igual à do Renderer3D.
	const float cp = std::cos(camera.pitch);
	const Vec3 forward{ cp * std::cos(camera.yaw), std::sin(camera.pitch), cp * std::sin(camera.yaw) };
	Vec3 right{ forward.y * 0.0f - forward.z * 1.0f, forward.z * 0.0f - forward.x * 0.0f, forward.x * 1.0f - forward.y * 0.0f };
	const float rightLength = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
	right = { right.x / rightLength, right.y / rightLength, right.z / rightLength };
	const Vec3 up{ right.y * forward.z - right.z * forward.y, right.z * forward.x - right.x * forward.z, right.x * forward.y - right.y * forward.x };
	const Vec3 &eye = camera.position;
	const float tanY = std::tan(camera.fovY * 0.5f);
	const float aspect = static_cast<float>(scene.width) / static_cast<float>(scene.height);

	// Matrizes em ordem de coluna (OpenGL olha para -Z).
	float view[16] = {
		right.x, up.x, -forward.x, 0,
		right.y, up.y, -forward.y, 0,
		right.z, up.z, -forward.z, 0,
		-(right.x * eye.x + right.y * eye.y + right.z * eye.z), -(up.x * eye.x + up.y * eye.y + up.z * eye.z), forward.x * eye.x + forward.y * eye.y + forward.z * eye.z, 1 };
	const float f = 1.0f / tanY;
	float projection[16] = {
		f / aspect, 0, 0, 0,
		0, f, 0, 0,
		0, 0, (FarPlane + NearPlane) / (NearPlane - FarPlane), -1,
		0, 0, 2.0f * FarPlane * NearPlane / (NearPlane - FarPlane), 0 };
	float viewProj[16];
	for (int c = 0; c < 4; c++)
	{
		for (int r = 0; r < 4; r++)
		{
			float sum = 0.0f;
			for (int k = 0; k < 4; k++) sum += projection[k * 4 + r] * view[c * 4 + k];
			viewProj[c * 4 + r] = sum;
		}
	}

	// Cena
	d.scene.bind();
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glClearColor(0, 0, 0, 0);
	glClearDepth(1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	gl::UseProgram(d.sceneProgram);
	gl::UniformMatrix4fv(d.uViewProj, 1, GL_FALSE, viewProj);
	gl::Uniform1i(d.uTexture, 0);
	gl::Uniform1i(d.uNormalMap, 1);
	gl::Uniform1i(d.uModern, options.modernLighting ? 1 : 0);
	gl::Uniform1i(d.uOcclusion, options.ambientOcclusion ? 1 : 0);
	gl::Uniform3f(d.uEye, eye.x, eye.y, eye.z);
	gl::Uniform1f(d.uAmbient, scene.ambient);
	gl::Uniform1f(d.uTime, scene.time);
	gl::Uniform3f(d.uWindowColor, scene.windowColor[0], scene.windowColor[1], scene.windowColor[2]);
	gl::Uniform1f(d.uWindowGlow, scene.windowGlow);
	const std::vector<PointLight> &lights = *scene.lights;
	const int lightCount = std::min(MaxLights, static_cast<int>(lights.size()));
	std::vector<float> lightPos(static_cast<size_t>(lightCount) * 4), lightColor(static_cast<size_t>(lightCount) * 3);
	for (int i = 0; i < lightCount; i++)
	{
		const PointLight &light = lights[static_cast<size_t>(i)];
		lightPos[static_cast<size_t>(i) * 4 + 0] = light.position.x;
		lightPos[static_cast<size_t>(i) * 4 + 1] = light.position.y;
		lightPos[static_cast<size_t>(i) * 4 + 2] = light.position.z;
		lightPos[static_cast<size_t>(i) * 4 + 3] = light.radius;
		lightColor[static_cast<size_t>(i) * 3 + 0] = light.r;
		lightColor[static_cast<size_t>(i) * 3 + 1] = light.g;
		lightColor[static_cast<size_t>(i) * 3 + 2] = light.b;
	}
	gl::Uniform1i(d.uLightCount, lightCount);
	if (lightCount > 0)
	{
		gl::Uniform4fv(d.uLightPos, lightCount, lightPos.data());
		gl::Uniform3fv(d.uLightColor, lightCount, lightColor.data());
	}

	const std::vector<GpuVertex> &vertices = *scene.vertices;
	gl::BindVertexArray(d.sceneVao);
	gl::BindBuffer(GL_ARRAY_BUFFER, d.sceneVbo);
	gl::BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(GpuVertex)), vertices.data(), GL_STREAM_DRAW);
	for (const GpuBatch &batch : *scene.batches)
	{
		const Impl::CachedTexture &cached = d.upload(*batch.texture);
		gl::ActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, cached.normals);
		gl::ActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, cached.color);
		gl::Uniform1i(d.uHasNormalMap, cached.normals ? 1 : 0);
		gl::Uniform1i(d.uAlphaTest, batch.texture->hasTransparency ? 1 : 0);
		glDrawArrays(GL_TRIANGLES, static_cast<GLint>(batch.first), static_cast<GLsizei>(batch.count));
	}
	gl::BindVertexArray(0);
	glDisable(GL_DEPTH_TEST);

	// Reflexos na água
	gl::BindVertexArray(d.emptyVao);
	d.sceneOutput = d.scene.color;
	if (options.reflections)
	{
		d.reflected.ensure(w, h, GL_RGBA8, GL_UNSIGNED_BYTE, false);
		d.reflected.bind();
		gl::UseProgram(d.reflectionProgram);
		gl::ActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, d.scene.depth);
		gl::ActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, d.scene.color);
		gl::Uniform1i(d.uniform(d.reflectionProgram, "uColor"), 0);
		gl::Uniform1i(d.uniform(d.reflectionProgram, "uDepth"), 1);
		gl::Uniform3f(d.uniform(d.reflectionProgram, "uEye"), eye.x, eye.y, eye.z);
		gl::Uniform3f(d.uniform(d.reflectionProgram, "uRight"), right.x, right.y, right.z);
		gl::Uniform3f(d.uniform(d.reflectionProgram, "uUp"), up.x, up.y, up.z);
		gl::Uniform3f(d.uniform(d.reflectionProgram, "uForward"), forward.x, forward.y, forward.z);
		gl::Uniform2f(d.uniform(d.reflectionProgram, "uTan"), tanY * aspect, tanY);
		gl::Uniform1f(d.uniform(d.reflectionProgram, "uTime"), scene.time);
		gl::Uniform1f(d.uniform(d.reflectionProgram, "uNear"), NearPlane);
		gl::Uniform1f(d.uniform(d.reflectionProgram, "uFar"), FarPlane);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		d.sceneOutput = d.reflected.color;
	}

	// Bloom em 1/4 da resolução, desfocado duas vezes em cada eixo.
	d.useBloom = options.bloom;
	if (options.bloom)
	{
		const int bw = (w + 3) / 4, bh = (h + 3) / 4;
		d.bloomA.ensure(bw, bh, GL_RGBA16F, GL_FLOAT, false);
		d.bloomB.ensure(bw, bh, GL_RGBA16F, GL_FLOAT, false);
		d.bloomA.bind();
		gl::UseProgram(d.brightProgram);
		gl::ActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, d.sceneOutput);
		gl::Uniform1i(d.uniform(d.brightProgram, "uColor"), 0);
		gl::Uniform2f(d.uniform(d.brightProgram, "uTexel"), 1.0f / w, 1.0f / h);
		glDrawArrays(GL_TRIANGLES, 0, 3);

		gl::UseProgram(d.blurProgram);
		gl::Uniform1i(d.uniform(d.blurProgram, "uSource"), 0);
		const GLint step = d.uniform(d.blurProgram, "uStep");
		for (int pass = 0; pass < 2; pass++)
		{
			d.bloomB.bind();
			glBindTexture(GL_TEXTURE_2D, d.bloomA.color);
			gl::Uniform2f(step, 1.0f / bw, 0.0f);
			glDrawArrays(GL_TRIANGLES, 0, 3);
			d.bloomA.bind();
			glBindTexture(GL_TEXTURE_2D, d.bloomB.color);
			gl::Uniform2f(step, 0.0f, 1.0f / bh);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}

	d.useVignette = options.vignette;
	d.sceneValid = true;
	gl::BindVertexArray(0);
	d.collectGarbage();
}

void arena::GlDevice::composeWorld(const std::vector<uint32_t> &overlay, int width, int height)
{
	Impl &d = *this->impl;
	if (!d.overlayTexture || d.overlayW != width || d.overlayH != height)
	{
		if (d.overlayTexture) glDeleteTextures(1, &d.overlayTexture);
		d.overlayTexture = makeTexture(width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
		d.overlayW = width;
		d.overlayH = height;
	}

	glBindTexture(GL_TEXTURE_2D, d.overlayTexture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	if (overlay.size() >= static_cast<size_t>(width) * height)
	{
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, overlay.data());
	}

	d.final.ensure(width, height, GL_RGBA8, GL_UNSIGNED_BYTE, false);
	d.final.bind();
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	gl::UseProgram(d.composeProgram);
	gl::ActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, d.sceneValid ? d.sceneOutput : d.overlayTexture);
	gl::ActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, d.bloomA.color);
	gl::ActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, d.overlayTexture);
	gl::ActiveTexture(GL_TEXTURE0);
	gl::Uniform1i(d.uniform(d.composeProgram, "uColor"), 0);
	gl::Uniform1i(d.uniform(d.composeProgram, "uBloom"), 1);
	gl::Uniform1i(d.uniform(d.composeProgram, "uOverlay"), 2);
	gl::Uniform1i(d.uniform(d.composeProgram, "uUseBloom"), (d.sceneValid && d.useBloom && d.bloomA.color) ? 1 : 0);
	gl::Uniform1i(d.uniform(d.composeProgram, "uVignette"), (d.sceneValid && d.useVignette) ? 1 : 0);
	gl::Uniform1f(d.uniform(d.composeProgram, "uBrightness"), this->brightness);
	gl::BindVertexArray(d.emptyVao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	gl::BindVertexArray(0);
	d.sceneValid = false;
	this->brightness = 1.0f;
}

void arena::GlDevice::composeCanvas(const std::vector<uint8_t> &rgba, int width, int height, int viewX, int viewY, int viewW, int viewH,
	const std::string &filter)
{
	Impl &d = *this->impl;
	if (!d.canvasTexture)
	{
		d.canvasTexture = makeTexture(Canvas::Width, Canvas::Height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
	}

	glBindTexture(GL_TEXTURE_2D, d.canvasTexture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Canvas::Width, Canvas::Height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

	d.final.ensure(width, height, GL_RGBA8, GL_UNSIGNED_BYTE, false);
	d.final.bind();
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	// A área 4:3 vem em coordenadas com origem em cima; o OpenGL conta de baixo.
	glViewport(viewX, height - viewY - viewH, viewW, viewH);
	gl::UseProgram(d.canvasProgram);
	gl::ActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, d.canvasTexture);
	gl::Uniform1i(d.uniform(d.canvasProgram, "uCanvas"), 0);
	gl::Uniform2f(d.uniform(d.canvasProgram, "uSize"), static_cast<float>(Canvas::Width), static_cast<float>(Canvas::Height));
	gl::Uniform1f(d.uniform(d.canvasProgram, "uScale"), std::max(1.0f, static_cast<float>(viewH) / Canvas::Height));
	gl::Uniform1i(d.uniform(d.canvasProgram, "uMode"), filter == "nearest" ? 0 : filter == "linear" ? 1 : 2);
	gl::BindVertexArray(d.emptyVao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	gl::BindVertexArray(0);
}

void arena::GlDevice::present()
{
	Impl &d = *this->impl;
	int drawableW = 0, drawableH = 0;
	SDL_GL_GetDrawableSize(this->window, &drawableW, &drawableH);
	gl::BindFramebuffer(GL_READ_FRAMEBUFFER, d.final.framebuffer);
	gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glViewport(0, 0, drawableW, drawableH);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	gl::BlitFramebuffer(0, 0, d.final.width, d.final.height, 0, 0, drawableW, drawableH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
	gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
	SDL_GL_SwapWindow(this->window);
}

void arena::GlDevice::readFinal(std::vector<uint32_t> &out, int &width, int &height)
{
	Impl &d = *this->impl;
	width = d.final.width;
	height = d.final.height;
	std::vector<uint32_t> flipped(static_cast<size_t>(width) * height);
	gl::BindFramebuffer(GL_FRAMEBUFFER, d.final.framebuffer);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
	out.resize(flipped.size());
	for (int y = 0; y < height; y++)
	{
		std::copy_n(flipped.begin() + static_cast<std::ptrdiff_t>(height - 1 - y) * width, width, out.begin() + static_cast<std::ptrdiff_t>(y) * width);
	}
}

void arena::GlDevice::finish()
{
	glFinish();
}
