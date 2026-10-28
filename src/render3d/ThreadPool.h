#pragma once

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace arena
{
	// Threads fixas que executam "uma faixa por thread": criar threads a cada etapa do
	// quadro custava mais que o próprio trabalho em resoluções altas.
	class ThreadPool
	{
	public:
		ThreadPool()
		{
			const int count = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 16);
			for (int i = 1; i < count; i++)
			{
				this->workers.emplace_back([this, i] { this->workerLoop(i); });
			}
		}

		~ThreadPool()
		{
			{
				std::lock_guard<std::mutex> lock(this->mutex);
				this->stopping = true;
				this->generation++;
			}
			this->wake.notify_all();
			for (std::thread &worker : this->workers) worker.join();
		}

		ThreadPool(const ThreadPool &) = delete;
		ThreadPool &operator=(const ThreadPool &) = delete;

		int size() const { return static_cast<int>(this->workers.size()) + 1; }

		// Executa job(índice) para índices 0..size()-1 em paralelo e espera todos terminarem.
		void run(const std::function<void(int)> &job)
		{
			{
				std::lock_guard<std::mutex> lock(this->mutex);
				this->current = &job;
				this->pending = static_cast<int>(this->workers.size());
				this->generation++;
			}
			this->wake.notify_all();
			job(0);

			std::unique_lock<std::mutex> lock(this->mutex);
			this->done.wait(lock, [this] { return this->pending == 0; });
			this->current = nullptr;
		}

	private:
		void workerLoop(int index)
		{
			unsigned seen = 0;
			for (;;)
			{
				const std::function<void(int)> *job = nullptr;
				{
					std::unique_lock<std::mutex> lock(this->mutex);
					this->wake.wait(lock, [this, seen] { return this->generation != seen; });
					seen = this->generation;
					if (this->stopping) return;
					job = this->current;
				}

				(*job)(index);
				{
					std::lock_guard<std::mutex> lock(this->mutex);
					if (--this->pending == 0) this->done.notify_one();
				}
			}
		}

		std::vector<std::thread> workers;
		std::mutex mutex;
		std::condition_variable wake, done;
		const std::function<void(int)> *current = nullptr;
		unsigned generation = 0;
		int pending = 0;
		bool stopping = false;
	};
}
