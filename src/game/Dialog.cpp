#include <algorithm>
#include <cctype>

#include "Dialog.h"

void arena::Dialog::open(std::string message, const BitmapFont &dialogFont, Style dialogStyle, std::vector<std::string> dialogOptions, int dialogCenterY)
{
	this->text = std::move(message);
	this->font = &dialogFont;
	this->style = dialogStyle;
	this->options = std::move(dialogOptions);
	this->centerY = dialogCenterY;
	this->hovered = -1;
	this->visible = true;
	this->layout();
}

void arena::Dialog::layout()
{
	int textW = 0, textH = 0;
	ui::measureBlock(*this->font, this->text, 1, textW, textH);

	int optionW = 0;
	for (const std::string &option : this->options)
	{
		optionW = std::max(optionW, ui::textWidth(*this->font, option));
	}

	// As peças do pergaminho têm 20x20, então a caixa precisa de ao menos 40x40.
	const bool parchment = this->style == Style::Parchment;
	const int minSize = parchment ? 40 : 16;
	const int width = std::min(Canvas::Width - 4, std::max({ minSize, textW + 24, optionW + 24 }));
	const int titleH = std::max(minSize, textH + (parchment ? 22 : 14));
	const int itemH = parchment ? 40 : 24;
	const int totalH = titleH + itemH * static_cast<int>(this->options.size());

	const int x = (Canvas::Width - width) / 2;
	const int y = std::clamp(this->centerY - totalH / 2, 0, std::max(0, Canvas::Height - totalH));
	this->titleRect = Rect{ x, y, width, titleH };
	this->optionRects.clear();
	for (size_t i = 0; i < this->options.size(); i++)
	{
		this->optionRects.push_back(Rect{ x, y + titleH + itemH * static_cast<int>(i), width, itemH });
	}
}

int arena::Dialog::handleInput(const InputEvent &event)
{
	if (!this->visible)
	{
		return None;
	}

	if (this->options.empty())
	{
		if (event.type == InputEvent::Type::MouseDown || event.type == InputEvent::Type::KeyDown)
		{
			this->visible = false;
			return Dismissed;
		}

		return None;
	}

	auto choose = [this](int index)
	{
		this->visible = false;
		return index;
	};

	switch (event.type)
	{
	case InputEvent::Type::MouseMove:
		this->hovered = -1;
		for (size_t i = 0; i < this->optionRects.size(); i++)
		{
			if (this->optionRects[i].contains(event.x, event.y)) this->hovered = static_cast<int>(i);
		}
		break;
	case InputEvent::Type::MouseDown:
		for (size_t i = 0; i < this->optionRects.size(); i++)
		{
			if (this->optionRects[i].contains(event.x, event.y)) return choose(static_cast<int>(i));
		}
		break;
	case InputEvent::Type::KeyDown:
		if (event.key == InputEvent::Key::Escape)
		{
			return choose(static_cast<int>(this->options.size()) - 1); // última opção = cancelar
		}
		if (event.key == InputEvent::Key::Up)
		{
			this->hovered = (this->hovered <= 0) ? static_cast<int>(this->options.size()) - 1 : this->hovered - 1;
		}
		else if (event.key == InputEvent::Key::Down)
		{
			this->hovered = (this->hovered + 1) % static_cast<int>(this->options.size());
		}
		else if (event.key == InputEvent::Key::Enter && this->hovered >= 0)
		{
			return choose(this->hovered);
		}
		else if (event.key == InputEvent::Key::Character)
		{
			// Atalho: primeira letra da opção.
			for (size_t i = 0; i < this->options.size(); i++)
			{
				const char first = this->options[i].empty() ? 0 : static_cast<char>(std::toupper(static_cast<unsigned char>(this->options[i][0])));
				if (first == event.character) return choose(static_cast<int>(i));
			}
		}
		break;
	default:
		break;
	}

	return None;
}

void arena::Dialog::render(Canvas &canvas, Game &game, const Palette &palette) const
{
	if (!this->visible)
	{
		return;
	}

	const bool parchment = this->style == Style::Parchment;
	const uint8_t textColor = parchment ? nearestColor(palette, 48, 12, 12) : nearestColor(palette, 199, 199, 199);
	const uint8_t optionColor = parchment ? nearestColor(palette, 48, 12, 12) : nearestColor(palette, 190, 113, 0);
	const uint8_t hoverColor = parchment ? nearestColor(palette, 185, 30, 20) : nearestColor(palette, 231, 162, 0);

	auto drawBox = [&](const Rect &r)
	{
		if (parchment) canvas.blit(game.getParchment(r.w, r.h), r.x, r.y);
		else ui::drawDarkBox(canvas, palette, r);
	};

	drawBox(this->titleRect);
	int textW = 0, textH = 0;
	ui::measureBlock(*this->font, this->text, 1, textW, textH);
	canvas.drawTextBlock(*this->font, this->text, this->titleRect.centerX(), this->titleRect.centerY() - textH / 2,
		textColor, -1, TextAlign::Center, 1);

	for (size_t i = 0; i < this->options.size(); i++)
	{
		const Rect &r = this->optionRects[i];
		drawBox(r);
		const uint8_t color = (static_cast<int>(i) == this->hovered) ? hoverColor : optionColor;
		canvas.drawText(*this->font, this->options[i], r.centerX(), r.centerY() - this->font->getHeight() / 2, color, -1, TextAlign::Center);
	}
}
