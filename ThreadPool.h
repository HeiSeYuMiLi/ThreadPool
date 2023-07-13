#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>
class ThreadPool {
public:
	ThreadPool(size_t);
	template <class F, class... Args>
	auto enqueue(F&& f, Args &&...args)
		->std::future<typename std::invoke_result<F>::type>;
	~ThreadPool();

private:
	// 工作队列
	std::vector<std::thread> workers;
	// 任务队列
	std::queue<std::function<void()>> tasks;

	std::mutex queue_mutex;
	std::condition_variable condition;
	bool stop;
};

inline ThreadPool::ThreadPool(size_t threads) : stop(false) {
	for (size_t i = 0; i < threads; ++i)
		workers.emplace_back([this] {
		for (;;) {
			std::function<void()> task;

			{
				std::unique_lock<std::mutex> lock(this->queue_mutex);
				// 等生产者生产
				this->condition.wait(
					lock, [this] { 
						return this->stop || !this->tasks.empty(); 
					});
				// 若是因为内存池停止，或是虚假唤醒(任务队列为空) 直接 return
				if (this->stop && this->tasks.empty())
					return;
				// 拿任务
				task = std::move(this->tasks.front());
				this->tasks.pop();
			}

			task();
		}
			});
}

// 添加任务
template <class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args &&...args)
-> std::future<typename std::invoke_result<F>::type> {
	// std::invoke_result 用于推导可调用对象的放回值类型
	using return_type = typename std::invoke_result<F>::type;
	// 将获取 packaged_task
	auto task = std::make_shared<std::packaged_task<return_type()>>(
		std::bind(std::forward<F>(f), std::forward<Args>(args)...));

	std::future<return_type> res = task->get_future();
	{
		std::unique_lock<std::mutex> lock(queue_mutex);

		// 不允许添加队列之后停止线程池
		if (stop)
			throw std::runtime_error("enqueue on stopped ThreadPool");

		tasks.emplace([task]() { (*task)(); });
	}
	condition.notify_one();
	return res;
}

inline ThreadPool::~ThreadPool() {
	{
		std::unique_lock<std::mutex> lock(queue_mutex);
		stop = true;
	}
	condition.notify_all();
	for (std::thread& worker : workers)
		worker.join();
}