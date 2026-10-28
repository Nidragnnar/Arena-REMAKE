#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "../core/Bytes.h"

namespace arena
{
	// Amostras mono normalizadas (-1..1).
	struct SoundBuffer
	{
		int sampleRate = 11025;
		std::vector<float> samples;
	};

	// .VOC (Creative Voice File): blocos tipo 1 = PCM de 8 bits sem sinal.
	std::optional<SoundBuffer> loadVoc(const ByteBuffer &data);

	// Mixer de efeitos sonoros: o jogo enfileira sons; a thread de áudio do SDL
	// chama mix() para preencher o buffer estéreo em float.
	class AudioMixer
	{
	public:
		void play(std::shared_ptr<const SoundBuffer> sound, float volume, float pan);
		void mix(float *stereoOut, int frames, int deviceRate);
		void setMasterVolume(float volume) { this->master.store(volume); }
		void stopAll();

	private:
		struct Voice
		{
			std::shared_ptr<const SoundBuffer> sound;
			double position = 0.0;
			float left = 1.0f, right = 1.0f;
		};

		std::mutex mutex;
		std::vector<Voice> voices;
		std::atomic<float> master{ 0.8f };
	};

	// Música .XMI (MIDI estendido da Miles AIL) tocada pelo sintetizador MIDI do sistema.
	struct MidiEvent
	{
		uint32_t tick;      // 120 ticks por segundo
		uint32_t message;   // mensagem curta (status | data1 << 8 | data2 << 16)
	};

	std::vector<MidiEvent> loadXmi(const ByteBuffer &data);

	class MusicPlayer
	{
	public:
		MusicPlayer();
		~MusicPlayer();
		MusicPlayer(const MusicPlayer &) = delete;
		MusicPlayer &operator=(const MusicPlayer &) = delete;

		bool isAvailable() const;
		void play(std::vector<MidiEvent> events, bool loop);
		void stop();
		void setVolume(float volume);

	private:
		struct Impl;
		std::unique_ptr<Impl> impl;
	};
}
