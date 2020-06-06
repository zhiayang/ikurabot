// async.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <thread>
#include <memory>
#include <functional>
#include <type_traits>

#include "synchro.h"
#include "function2.h"

namespace ikura
{
	template <typename T>
	struct future
	{
		template <typename F = T>
		std::enable_if_t<!std::is_same_v<void, F>, T>& get()
		{
			this->wait();
			return this->state->value;
		}

		template <typename F = T>
		void set(std::enable_if_t<!std::is_same_v<void, F>, T>&& x)
		{
			this->state->value = x;
			this->state->cv.set(true);
		}

		template <typename F = T>
		std::enable_if_t<std::is_same_v<void, F>, void> get()
		{
			this->wait();
		}

		template <typename F = T>
		std::enable_if_t<std::is_same_v<void, F>, void> set()
		{
			this->state->cv.set(true);
		}

		void wait() const
		{
			this->state->cv.wait(true);
		}

		void discard()
		{
			this->state->discard = true;
		}

		~future() { if(this->state && !this->state->discard) { this->state->cv.wait(true); } }

		future() { this->state = std::make_shared<internal_state<T>>(); this->state->cv.set(false); }

		template <typename F = T>
		future(std::enable_if_t<!std::is_same_v<void, F>, T>&& val)
		{
			this->state = std::make_shared<internal_state<T>>();
			this->state->value = val;
			this->state->cv.set(true);
		}

		future(future&& f)
		{
			this->state = f.state;
			f.state = nullptr;
		}

		future& operator = (future&& f)
		{
			if(this != &f)
			{
				this->state = f.state;
				f.state = nullptr;
			}

			return *this;
		}

		template <typename Fn>// , typename E = std::enable_if_t<!std::is_same_v<decltype(std::declval<Fn>()(std::declval<T>())), void>>>
		auto then(Fn&& fn) -> future<decltype(fn(std::declval<T>()))>;

		// template <typename Fn, typename E = std::enable_if_t<std::is_same_v<decltype(std::declval<Fn>()(std::declval<T>())), void>>>
		// void then(Fn&& fn);

		future(const future&) = delete;
		future& operator = (const future&) = delete;

	private:
		template<size_t> friend struct ThreadPool;

		template <typename E>
		struct internal_state
		{
			internal_state() { cv.set(false); }

			E value;
			condvar<bool> cv;
			bool discard = false;

			internal_state(internal_state&& f) = delete;
			internal_state& operator = (internal_state&& f) = delete;
		};

		template <>
		struct internal_state<void>
		{
			internal_state() { discard = false; cv.set(false); }

			condvar<bool> cv;
			bool discard;

			internal_state(internal_state&& f) = delete;
			internal_state& operator = (internal_state&& f) = delete;
		};


		future(std::shared_ptr<internal_state<T>> st) : state(st) { }
		future clone() { return future(this->state); }

		std::shared_ptr<internal_state<T>> state;
	};

	namespace futures
	{
		template <typename... Args>
		static void wait(future<Args>&... futures)
		{
			// i love c++17
			(futures.wait(), ...);
		}

		template <typename L>
		static void wait(const L& futures)
		{
			for(const auto& f : futures)
				f.wait();
		}
	}

	template <size_t N>
	struct ThreadPool
	{
		// template <typename Fn, typename... Args,
		// 	typename = std::enable_if_t<std::is_same_v<decltype(std::declval<Fn>()(std::declval<Args>()...)), void>>
		// >
		// void run(Fn&& fn, Args&&... args)
		// {
		// 	this->jobs.emplace([fn = std::move(fn), args...]() {
		// 		fn(args...);
		// 	});
		// }

		template <typename Fn, typename... Args> //, typename T = decltype(std::declval<Fn>()(std::declval<Args>()...))>
		auto run(Fn&& fn, Args&&... args) -> future<decltype(fn(std::forward<Args>(args)...))>
		{
			using T = decltype(fn(args...));

			auto fut = future<T>();
			this->jobs.emplace([fn = std::move(fn), args..., f1 = fut.clone()]() mutable {
				if constexpr (!std::is_same_v<T, void>)
				{
					f1.set(fn(args...));
				}
				else
				{
					fn(args...);
					f1.set();
				}
			});

			return fut;
		}

		ThreadPool()
		{
			for(size_t i = 0; i < N; i++)
			{
				this->threads[i] = std::thread([this]() {
					worker(this);
				});
			}
		}

		~ThreadPool()
		{
			// push a quit job
			this->jobs.push(Job::stop());
			for(auto& thr : this->threads)
				thr.join();
		}

		ThreadPool(ThreadPool&&) = delete;
		ThreadPool(const ThreadPool&) = delete;
		ThreadPool& operator = (ThreadPool&&) = delete;
		ThreadPool& operator = (const ThreadPool&) = delete;

	private:
		static void worker(ThreadPool* tp)
		{
			while(true)
			{
				auto job = tp->jobs.pop();
				if(job.should_stop)
				{
					tp->jobs.push(Job::stop());
					break;
				}

				job.func();
			}
		}

		struct Job
		{
			bool should_stop = false;
			fu2::unique_function<void (void)> func;

			Job() { }
			explicit Job(fu2::unique_function<void (void)>&& f) : func(std::move(f)) { }

			static inline Job stop() { Job j; j.should_stop = true; return j; }
		};

		std::thread threads[N];
		ikura::wait_queue<Job> jobs;
	};

	ThreadPool<4>& dispatcher();


	template <typename T>   // this is weird. i don't like it.
	template <typename Fn>//, typename E>
	auto future<T>::then(Fn&& fn) -> future<decltype(fn(std::declval<T>()))>
	{
		this->discard();
		return dispatcher().run([fn = std::move(fn), x = this->clone()]() mutable -> auto {
			return fn(x.get());
		});
	}

	// template <typename T>
	// template <typename Fn, typename E>
	// void future<T>::then(Fn&& fn)
	// {
	// 	this->discard();
	// 	dispatcher().run([fn = std::move(fn), x = this->clone()]() {
	// 		fn(x.get());
	// 	});
	// }
}
