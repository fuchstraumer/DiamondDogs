#pragma once
#ifndef VULPES_UTIL_THREAD_WRAPPER_H
#define VULPES_UTIL_THREAD_WRAPPER_H

#include "stdafx.h"

namespace vulpes {

		class ThreadWrapped {
		private:
			bool destroying = false;
			std::thread worker;
			std::queue<std::function<void()>> TaskQueue;
			std::mutex queueMutex;
			std::condition_variable condition;

			void jobLoop() {
				while (true) {
					std::function<void()> job;
					{
						std::unique_lock<std::mutex> lock(queueMutex);
						condition.wait(lock, [this] { return !TaskQueue.empty() || destroying; });
						if (destroying) {
							break;
						}
						job = TaskQueue.front();
					}
					job();
					{
						std::lock_guard<std::mutex> lock(queueMutex);
						TaskQueue.pop();
						condition.notify_one();
					}
				}
			}

		public:
			ThreadWrapped() {
				worker = std::thread(&ThreadWrapped::jobLoop, this);
			}

			~ThreadWrapped() {
				if (worker.joinable()) {
					Wait();
					queueMutex.lock();
					destroying = true;
					condition.notify_one();
					queueMutex.unlock();
					worker.join();
				}
			}

			// Add a new job to the thread's queue
			void AddTask(std::function<void()> function) {
				std::lock_guard<std::mutex> lock(queueMutex);
				TaskQueue.push(std::move(function));
				condition.notify_one();
			}

			// Wait until all work items have been finished
			void Wait() {
				std::unique_lock<std::mutex> lock(queueMutex);
				condition.wait(lock, [this]() { return TaskQueue.empty(); });
			}
		};


}

#endif // !VULPES_UTIL_THREAD_WRAPPER_H
