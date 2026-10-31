#include <algorithm>
#include <cctype>
#include <cstdio>

#include "../world/SpriteOverrides.h"
#include "Cutscene.h"

namespace
{
	std::string pngName(const std::string &name, int frame)
	{
		char suffix[16];
		std::snprintf(suffix, sizeof(suffix), "_%03d", frame);
		return name + suffix;
	}
}

void arena::Cutscene::start(Game &owner, std::vector<std::string> files)
{
	this->game = &owner;
	this->queue.assign(files.begin(), files.end());
	this->playing = this->openNext();
}

bool arena::Cutscene::openNext()
{
	while (!this->queue.empty())
	{
		const std::string file = this->queue.front();
		this->queue.pop_front();
		const auto data = this->game->getAssets().read(file);
		std::string error;
		if (!data || !this->player.open(*data, error))
		{
			std::printf("Cutscene %s não encontrada ou inválida.\n", file.c_str());
			continue;
		}

		this->name = file.substr(0, file.rfind('.'));
		for (char &c : this->name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

		// Quantos quadros redesenhados existem (o maior número encontrado conta).
		int pngFrames = 0;
		for (int i = 0; i < 4000; i++)
			if (hasModPng(pngName(this->name, i))) pngFrames = i + 1;
			else if (i >= std::max(pngFrames, this->player.frameCount()) + 8) break;

		const double originalSeconds = this->player.frameCount() * this->player.frameDelayMs() / 1000.0;
		this->pngOnly = pngFrames > this->player.frameCount();
		this->frames = this->pngOnly ? pngFrames : this->player.frameCount();
		this->frameSeconds = this->pngOnly ? originalSeconds / pngFrames : this->player.frameDelayMs() / 1000.0;
		if (pngFrames > 0) std::printf("Cutscene %s: %d quadro(s) redesenhado(s)%s\n", this->name.c_str(), pngFrames, this->pngOnly ? " (só PNG)" : "");
		this->frame = -1;
		this->timer = 0.0;
		this->showFrame(0);
		return true;
	}

	return false;
}

void arena::Cutscene::showFrame(int index)
{
	// Avança o FLC até o quadro (também quando há PNG, para os quadros que faltarem).
	if (!this->pngOnly)
		while (this->player.frameIndex() < index && this->player.nextFrame()) {}

	this->frame = index;
	int w = 0, h = 0;
	std::vector<uint32_t> png;
	if (loadModPng(pngName(this->name, index), png, w, h))
	{
		this->image = std::move(png);
		this->imageW = w;
		this->imageH = h;
		this->imageFromPng = true;
	}
	else if (!this->pngOnly)
	{
		this->image = this->player.rgba();
		this->imageW = this->player.width();
		this->imageH = this->player.height();
		this->imageFromPng = false;
	}
	// Só PNG com um quadro faltando: fica o anterior.
}

void arena::Cutscene::update(double dt)
{
	if (!this->playing) return;
	this->timer += std::min(dt, 0.1); // sem pular quadros depois de um travamento (carregamento)
	while (this->timer >= this->frameSeconds)
	{
		this->timer -= this->frameSeconds;
		if (this->frame + 1 >= this->frames)
		{
			this->playing = this->openNext();
			return;
		}
		this->showFrame(this->frame + 1);
	}
}

void arena::Cutscene::skip()
{
	if (this->playing) this->playing = this->openNext();
}

void arena::Cutscene::draw(kit::Painter &painter, int width, int height) const
{
	painter.fill(Rect{ 0, 0, width, height }, 0xFF000000u);
	if (!this->playing || this->image.empty()) return;

	// 320x200 aparecia esticado num monitor 4:3; PNG em 16:10 segue igual, outras proporções ficam como são.
	const bool vga = this->imageW * 10 == this->imageH * 16;
	const double aspect = vga ? 4.0 / 3.0 : static_cast<double>(this->imageW) / this->imageH;
	int drawW = width, drawH = static_cast<int>(width / aspect);
	if (drawH > height) { drawH = height; drawW = static_cast<int>(height * aspect); }
	painter.image(this->image, this->imageW, this->imageH, Rect{ (width - drawW) / 2, (height - drawH) / 2, drawW, drawH });
}

arena::CutsceneScreen::CutsceneScreen(Game &game, std::vector<std::string> files, NextScreen nextScreen) : Screen(game), next(nextScreen)
{
	this->cutscene.start(game, std::move(files));
}

void arena::CutsceneScreen::handleInput(const InputEvent &event)
{
	if (this->finished) return;
	if (event.isKey(InputEvent::Key::Escape)) this->cutscene.stop();
	else if (event.type == InputEvent::Type::KeyDown || event.isClick()) this->cutscene.skip();
}

void arena::CutsceneScreen::update(double dt)
{
	this->cutscene.update(dt);
	if (!this->cutscene.active() && !this->finished)
	{
		this->finished = true;
		this->game.changeScreen(this->next(this->game));
	}
}

void arena::CutsceneScreen::render(Canvas &canvas)
{
	canvas.clear(0);
}

void arena::CutsceneScreen::renderWorld(std::vector<uint32_t> &pixels, int width, int height)
{
	pixels.assign(static_cast<size_t>(width) * height, 0xFF000000u);
	hud::Frame frame{ pixels, width, height };
	kit::Painter painter(frame, this->game.getFonts());
	this->cutscene.draw(painter, width, height);
}
