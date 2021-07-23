#ifndef BLOCKQ_H
#define BLOCKQ_H


#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <iostream>
template <typename T>
class BlockingConcurrentQueue
{
public:

	T pop()
	{
		std::unique_lock<std::mutex> mlock(mutex_);
		while (queue_.empty())
		{
			cond_.wait(mlock);
		}
		auto item = queue_.front();
		queue_.pop();
		return item;
	}

	void wait_dequeue(T& item)
	{
		std::unique_lock<std::mutex> mlock(mutex_);
		while (queue_.empty())
		{
			cond_.wait(mlock);
		}
		item = queue_.front();
		size.fetch_sub(1);
		queue_.pop();
	}

    bool wait_dequeue_for(T& item, const std::chrono::milliseconds time)
    {
        std::unique_lock<std::mutex> mlock(mutex_);
		cond_.wait_for(mlock,time); //tzw spurious wakeup nie jest w tym przypadku problemem
		if (queue_.empty()) {
            return false;
		}
		item = queue_.front();
		size.fetch_sub(1);
		queue_.pop();
		return true;
    }

	void enqueue(T item)
	{
		std::unique_lock<std::mutex> mlock(mutex_);
		queue_.push(item);
		mlock.unlock();
		size.fetch_add(1);
		cond_.notify_one();
	}

	bool try_dequeue(T &item) {
		if (size.load() == 0)
			return false;
		std::unique_lock<std::mutex> mlock(mutex_);
		if (!queue_.empty()) {
			item = queue_.front();
			size.fetch_sub(1);
			queue_.pop();
			return true;
		}
		else
			return false;
	}

private:
	std::atomic<int> size{ 0 };
	std::queue<T> queue_;
	std::mutex mutex_;
	std::condition_variable cond_;
};

#endif

