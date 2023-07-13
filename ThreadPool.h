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
	// ��������
	std::vector<std::thread> workers;
	// �������
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
				// ������������
				this->condition.wait(
					lock, [this] { 
						return this->stop || !this->tasks.empty(); 
					});
				// ������Ϊ�ڴ��ֹͣ��������ٻ���(�������Ϊ��) ֱ�� return
				if (this->stop && this->tasks.empty())
					return;
				// ������
				task = std::move(this->tasks.front());
				this->tasks.pop();
			}

			task();
		}
			});
}

// ��������
template <class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args &&...args)
-> std::future<typename std::invoke_result<F>::type> {
	// std::invoke_result �����Ƶ��ɵ��ö���ķŻ�ֵ����
	using return_type = typename std::invoke_result<F>::type;
	// ����ȡ packaged_task
	auto task = std::make_shared<std::packaged_task<return_type()>>(
		std::bind(std::forward<F>(f), std::forward<Args>(args)...));

	std::future<return_type> res = task->get_future();
	{
		std::unique_lock<std::mutex> lock(queue_mutex);

		// ���������Ӷ���֮��ֹͣ�̳߳�
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