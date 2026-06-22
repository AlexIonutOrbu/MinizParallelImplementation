#pragma once

#include <vector>
#include <deque>
#include <future>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <algorithm>

#include <taskFactory.hpp>

class threadPool {
#if !defined(__cpp_lib_move_only_function) || __cpp_lib_move_only_function < 202110
	using Task = std::function<void()>;
#else
	using Task = std::move_only_function<void()>;
#endif	

	std::vector<std::jthread> workers_;
    std::mutex m_;
    std::condition_variable cv_;
    bool done_ = false;
	std::deque<Task> q_;
	
    void worker_loop() {
        for(;;) {
			Task task;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [&]{ return done_ || !q_.empty(); });
                if (done_ && q_.empty()) return;
                task = std::move(q_.front());
                q_.pop_front();
            }
            task();
        }
    }
public:
    explicit threadPool(unsigned n = std::max(1u, std::thread::hardware_concurrency())) {
		if (n==0) throw std::invalid_argument("threadPool: n must be >0");
		workers_.reserve(n);
        for (unsigned i=0; i<n; ++i)
			workers_.emplace_back([this]{ worker_loop(); });
    }
	threadPool(const threadPool&)            = delete;
	threadPool& operator=(const threadPool&) = delete;
	threadPool(threadPool&&)                 = delete;
	threadPool& operator=(threadPool&&)      = delete;
	
    ~threadPool() {
        {
            std::lock_guard<std::mutex> lk(m_);
            done_ = true;
        }
        cv_.notify_all();

		// wait for the termination af all workers_
		for (auto& thread : workers_)
			thread.join();
    }

    template<class F, class... Args>
    auto submit(F&& f, Args&&... args)
		-> std::future<std::invoke_result_t<F, Args...>> {
		static_assert(std::is_invocable_v<F, Args...>, "F(Args...) not callable");

		auto [task,future] = make_task(std::forward<F>(f),std::forward<Args>(args)...);
        {
            std::lock_guard<std::mutex> lk(m_);
			if (done_)
				throw std::runtime_error("threadPool: submit() after object destruction\n");
            q_.emplace_back(std::move(task));
        }
        cv_.notify_one();
        return future;
    }
};
