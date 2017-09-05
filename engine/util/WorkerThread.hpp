#pragma once
#ifndef DIAMOND_DOGS_WORKER_THREAD_HPP
#define DIAMOND_DOGS_WORKER_THREAD_HPP

#include "stdafx.h"
#include <queue>
namespace vulpes {

	namespace util {

		namespace detail {
			struct task {
				template<class F,
					class dF = std::decay_t<F>,
					class = decltype(std::declval<dF&>()())
				>
					task(F&& f) :
					ptr(
						new dF(std::forward<F>(f)),
						[](void* ptr) { delete static_cast<dF*>(ptr); }
					),
					invoke([](void*ptr) {
					(*static_cast<dF*>(ptr))();
				})
				{}
				void operator()()const {
					invoke(ptr.get());
				}
				task(task&&) = default;
				task&operator=(task&&) = default;
				task() = default;
				~task() = default;
				explicit operator bool()const { return static_cast<bool>(ptr); }
			private:
				std::unique_ptr<void, void(*)(void*)> ptr;
				void(*invoke)(void*) = nullptr;
			};
		}

		using task_t = detail::task;

		class WorkerThread {
		private:

			bool destroying{ false };
			std::mutex queueMutex;
			std::condition_variable cVar;
			std::queue<task_t> tasks;
			std::thread _thread;

			void queue_loop() {
				while (!tasks.empty()) {
					task_t task;
					std::future<void> future;

					{
						std::lock_guard<std::mutex> lock(queueMutex);
						task = std::move(tasks.front());
						tasks.pop();
					}

					{
						std::unique_lock<std::mutex> lock(queueMutex);
						cVar.wait(lock, [&]() { return task(); });
						cVar.notify_one();
					}
				}
			}

		public:

			WorkerThread() = default;

			~WorkerThread() {
				if (_thread.joinable()) {
					Wait();
					queueMutex.lock();
					destroying = true;
					cVar.notify_one();
					queueMutex.unlock();
					_thread.join();
				}
			}

			void Run() {
				_thread = std::thread(&WorkerThread::queue_loop, this);
			}

			void Wait() {
				std::unique_lock<std::mutex> lock(queueMutex);
				cVar.wait(lock, [this]() { return tasks.empty(); });
			}

			template<class Fn, class...Args>
			std::future<typename std::result_of<Fn(Args...)>::type> AddTask(Fn&& f, Args&&...args) {
				std::packaged_task<typename std::result_of<Fn(Args...)>::type()> task(std::bind(f, args...));
				auto result(task.get_future());
				static std::mutex task_queue_mutex;
				std::lock_guard<std::mutex> task_queue_lock(task_queue_mutex);
				tasks.emplace(std::move(task_t(task)));
				cVar.notify_one();
				return std::move(result);
			}

			template<class Fn, class...Args>
			std::future<typename std::result_of<typename std::decay<Fn>::type(typename std::decay<Args>::type...)>::type> add_task(Fn&& f, Args&&...args) {
				auto func = std::make_shared<std::packaged_task<typename std::result_of<typename std::decay<Fn>::type(typename std::decay<Args>::type...)>::type()>>(std::bind(f, args...));
				auto result(func->get_future());
			}

		};
		
	}

}

#endif // !DIAMOND_DOGS_WORKER_THREAD_HPP
