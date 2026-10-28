#pragma once

#include <algorithm>
#include <cmath>

namespace arena::clock
{
	// Relógio do jogo em minutos desde 0h do dia 1 de Estrela da Manhã. Como no original, um
	// segundo real são 13 segundos de jogo (um dia dura cerca de 1h50 de jogo real).
	constexpr double GameMinutesPerRealSecond = 13.0 / 60.0;
	constexpr double MinutesPerDay = 24.0 * 60.0;
	constexpr int DaysPerMonth = 30;

	inline double minuteOfDay(double gameMinutes) { return std::fmod(std::max(0.0, gameMinutes), MinutesPerDay); }
	inline int hour(double gameMinutes) { return static_cast<int>(minuteOfDay(gameMinutes) / 60.0); }
	inline int minute(double gameMinutes) { return static_cast<int>(minuteOfDay(gameMinutes)) % 60; }
	inline int dayIndex(double gameMinutes) { return static_cast<int>(std::max(0.0, gameMinutes) / MinutesPerDay); }
	inline int dayOfMonth(double gameMinutes) { return dayIndex(gameMinutes) % DaysPerMonth + 1; }
	inline int month(double gameMinutes) { return (dayIndex(gameMinutes) / DaysPerMonth) % 12 + 1; }

	// Luz do dia de 0 (noite) a 1 (dia): amanhece das 5h30 às 7h, escurece das 17h30 às 19h.
	inline float daylight(double gameMinutes)
	{
		const double m = minuteOfDay(gameMinutes);
		auto ramp = [](double v, double a, double b) { return std::clamp((v - a) / (b - a), 0.0, 1.0); };
		return static_cast<float>(ramp(m, 5.5 * 60, 7.0 * 60) * (1.0 - ramp(m, 17.5 * 60, 19.0 * 60)));
	}

	// Noite para música, lojas e ruas (das 17h45 às 6h15 a música e os lampiões; lojas fecham às 21h).
	inline bool isNight(double gameMinutes) { const double m = minuteOfDay(gameMinutes); return m >= 17.75 * 60 || m < 6.25 * 60; }
	inline bool shopsClosed(double gameMinutes) { const int h = hour(gameMinutes); return h >= 21 || h < 6; }

	// Minutos até a próxima manhã (6h).
	inline double untilMorning(double gameMinutes)
	{
		const double m = minuteOfDay(gameMinutes);
		return (m < 6.0 * 60 ? 6.0 * 60 - m : MinutesPerDay - m + 6.0 * 60);
	}
}
