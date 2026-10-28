#include <algorithm>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <thread>

#include "Audio.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#endif

// ---------------------------------------------------------------------------
// VOC

std::optional<arena::SoundBuffer> arena::loadVoc(const ByteBuffer &data)
{
	constexpr char Signature[] = "Creative Voice File";
	if (data.size() < 26 || std::memcmp(data.data(), Signature, sizeof(Signature) - 1) != 0)
	{
		return std::nullopt;
	}

	SoundBuffer sound;
	size_t offset = readLE16(data.data() + 0x14);
	while (offset + 4 <= data.size())
	{
		const uint8_t type = data[offset];
		if (type == 0) break; // terminador
		const size_t size = data[offset + 1] | (data[offset + 2] << 8) | (data[offset + 3] << 16);
		const size_t body = offset + 4;
		const size_t end = std::min(data.size(), body + size);
		if (type == 1 && body + 2 <= end)
		{
			// Divisor de frequência: taxa = 1.000.000 / (256 - divisor). Codec 0 = PCM 8 bits.
			sound.sampleRate = 1000000 / (256 - data[body]);
			if (data[body + 1] == 0)
			{
				for (size_t i = body + 2; i < end; i++) sound.samples.push_back((data[i] - 128) / 128.0f);
			}
		}
		else if (type == 2)
		{
			for (size_t i = body; i < end; i++) sound.samples.push_back((data[i] - 128) / 128.0f);
		}

		offset = body + size;
	}

	if (sound.samples.empty())
	{
		return std::nullopt;
	}

	return sound;
}

// ---------------------------------------------------------------------------
// Mixer

void arena::AudioMixer::play(std::shared_ptr<const SoundBuffer> sound, float volume, float pan)
{
	if (!sound || volume <= 0.01f)
	{
		return;
	}

	// Pan de potência constante (-1 esquerda .. 1 direita).
	pan = std::clamp(pan, -1.0f, 1.0f);
	Voice voice;
	voice.sound = std::move(sound);
	voice.left = volume * std::sqrt(0.5f * (1.0f - pan));
	voice.right = volume * std::sqrt(0.5f * (1.0f + pan));

	std::lock_guard lock(this->mutex);
	if (this->voices.size() >= 24)
	{
		this->voices.erase(this->voices.begin()); // descarta o mais antigo
	}

	this->voices.push_back(std::move(voice));
}

void arena::AudioMixer::stopAll()
{
	std::lock_guard lock(this->mutex);
	this->voices.clear();
}

void arena::AudioMixer::mix(float *out, int frames, int deviceRate)
{
	std::fill(out, out + frames * 2, 0.0f);
	const float gain = this->master.load();
	std::lock_guard lock(this->mutex);
	for (Voice &voice : this->voices)
	{
		const auto &samples = voice.sound->samples;
		const double step = static_cast<double>(voice.sound->sampleRate) / deviceRate;
		for (int f = 0; f < frames; f++)
		{
			const size_t i = static_cast<size_t>(voice.position);
			if (i + 1 >= samples.size())
			{
				voice.position = static_cast<double>(samples.size());
				break;
			}

			// Interpolação linear (os VOC originais têm ~11 kHz).
			const float t = static_cast<float>(voice.position - static_cast<double>(i));
			const float s = (samples[i] + (samples[i + 1] - samples[i]) * t) * gain;
			out[f * 2] += s * voice.left;
			out[f * 2 + 1] += s * voice.right;
			voice.position += step;
		}
	}

	std::erase_if(this->voices, [](const Voice &v) { return v.position + 1 >= static_cast<double>(v.sound->samples.size()); });
	for (int i = 0; i < frames * 2; i++) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// XMI

namespace
{
	uint32_t readBE32(const uint8_t *p)
	{
		return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
	}

	uint32_t readVlq(const uint8_t *&p, const uint8_t *end)
	{
		uint32_t value = 0;
		while (p < end)
		{
			const uint8_t b = *(p++);
			value = (value << 7) | (b & 0x7F);
			if ((b & 0x80) == 0) break;
		}

		return value;
	}
}

std::vector<arena::MidiEvent> arena::loadXmi(const ByteBuffer &data)
{
	std::vector<MidiEvent> events;

	// Procura o bloco "EVNT" (o XMI é um IFF: FORM XDIR / CAT XMID / FORM XMID).
	size_t evnt = 0;
	for (size_t i = 0; i + 8 <= data.size(); i++)
	{
		if (std::memcmp(data.data() + i, "EVNT", 4) == 0) { evnt = i; break; }
	}

	if (evnt == 0)
	{
		return events;
	}

	const uint8_t *p = data.data() + evnt + 8;
	const uint8_t *end = std::min(data.data() + data.size(), p + readBE32(data.data() + evnt + 4));
	uint32_t tick = 0;
	while (p < end)
	{
		// Intervalo: soma de bytes < 0x80 antes do próximo evento.
		while (p < end && *p < 0x80) tick += *(p++);
		if (p >= end) break;

		const uint8_t status = *(p++);
		const uint8_t kind = status & 0xF0;
		if (status == 0xFF)
		{
			if (p >= end) break;
			const uint8_t meta = *(p++);
			const uint32_t length = readVlq(p, end);
			p += length;
			if (meta == 0x2F) break; // fim da faixa
		}
		else if (status == 0xF0 || status == 0xF7)
		{
			p += readVlq(p, end);
		}
		else if (kind == 0x90)
		{
			if (end - p < 2) break;
			const uint8_t note = *(p++);
			const uint8_t velocity = *(p++);
			const uint32_t duration = readVlq(p, end);
			events.push_back({ tick, static_cast<uint32_t>(status | (note << 8) | (velocity << 16)) });
			// Notas XMI trazem a duração; gera o note-off correspondente.
			events.push_back({ tick + duration, static_cast<uint32_t>((0x80 | (status & 0x0F)) | (note << 8)) });
		}
		else if (kind == 0xC0 || kind == 0xD0)
		{
			if (p >= end) break;
			events.push_back({ tick, static_cast<uint32_t>(status | (*(p++) << 8)) });
		}
		else
		{
			if (end - p < 2) break;
			const uint8_t d1 = *(p++);
			const uint8_t d2 = *(p++);
			events.push_back({ tick, static_cast<uint32_t>(status | (d1 << 8) | (d2 << 16)) });
		}
	}

	std::stable_sort(events.begin(), events.end(), [](const MidiEvent &a, const MidiEvent &b) { return a.tick < b.tick; });
	return events;
}

// ---------------------------------------------------------------------------
// Música via MIDI do Windows (winmm). Em outros sistemas fica silenciosa.

struct arena::MusicPlayer::Impl
{
#ifdef _WIN32
	HMIDIOUT device = nullptr;
#endif
	std::thread thread;
	std::mutex mutex;
	std::condition_variable wake;
	std::vector<MidiEvent> events;
	bool loop = false;
	bool stopRequested = false;
	bool quit = false;
	uint64_t generation = 0;
	float volume = 0.7f;

	void send(uint32_t message)
	{
#ifdef _WIN32
		if (this->device == nullptr) return;
		// Volume aplicado à velocidade das notas (note-on).
		if ((message & 0xF0) == 0x90)
		{
			const uint32_t velocity = (message >> 16) & 0x7F;
			const uint32_t scaled = static_cast<uint32_t>(velocity * this->volume);
			message = (message & 0x00FFFF) | (scaled << 16);
		}

		midiOutShortMsg(this->device, message);
#else
		(void)message;
#endif
	}

	void allNotesOff()
	{
		for (uint32_t channel = 0; channel < 16; channel++)
		{
			this->send(0xB0 | channel | (123 << 8)); // All Notes Off
			this->send(0xB0 | channel | (121 << 8)); // Reset Controllers
		}
	}

	void run()
	{
		std::unique_lock lock(this->mutex);
		while (!this->quit)
		{
			this->wake.wait(lock, [this] { return this->quit || !this->events.empty(); });
			if (this->quit) break;

			const uint64_t myGeneration = this->generation;
			const std::vector<MidiEvent> song = this->events;
			const bool looping = this->loop;
			do
			{
				const auto start = std::chrono::steady_clock::now();
				for (const MidiEvent &event : song)
				{
					const auto due = start + std::chrono::microseconds(static_cast<int64_t>(event.tick) * 1000000 / 120);
					if (this->wake.wait_until(lock, due, [&] { return this->quit || this->generation != myGeneration; })) break;
					this->send(event.message);
				}

				this->allNotesOff();
			} while (looping && !this->quit && this->generation == myGeneration);

			if (this->generation == myGeneration) this->events.clear();
		}

		this->allNotesOff();
	}
};

arena::MusicPlayer::MusicPlayer() : impl(std::make_unique<Impl>())
{
#ifdef _WIN32
	if (midiOutOpen(&this->impl->device, MIDI_MAPPER, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
	{
		this->impl->device = nullptr;
	}
#endif
	this->impl->thread = std::thread([this] { this->impl->run(); });
}

arena::MusicPlayer::~MusicPlayer()
{
	{
		std::lock_guard lock(this->impl->mutex);
		this->impl->quit = true;
		this->impl->generation++;
	}

	this->impl->wake.notify_all();
	this->impl->thread.join();
#ifdef _WIN32
	if (this->impl->device != nullptr) midiOutClose(this->impl->device);
#endif
}

bool arena::MusicPlayer::isAvailable() const
{
#ifdef _WIN32
	return this->impl->device != nullptr;
#else
	return false;
#endif
}

void arena::MusicPlayer::play(std::vector<MidiEvent> events, bool loop)
{
	{
		std::lock_guard lock(this->impl->mutex);
		this->impl->generation++;
		this->impl->events = std::move(events);
		this->impl->loop = loop;
	}

	this->impl->wake.notify_all();
}

void arena::MusicPlayer::stop()
{
	this->play({}, false);
}

void arena::MusicPlayer::setVolume(float volume)
{
	std::lock_guard lock(this->impl->mutex);
	this->impl->volume = std::clamp(volume, 0.0f, 1.0f);
}
