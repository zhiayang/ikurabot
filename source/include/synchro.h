// synchro.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <deque>
#include <atomic>
#include <thread>
#include <chrono>
#include <shared_mutex>

namespace ikura
{
	template <typename T>
	struct condvar
	{
		condvar() : value() { }
		condvar(const T& x) : value(x) { }

		void set(const T& x)
		{
			this->set_quiet(x);
			this->notify_all();
		}

		void set_quiet(const T& x)
		{
			auto lk = std::lock_guard<std::mutex>(this->mtx);
			this->value = x;
		}

		T get()
		{
			return this->value;
		}

		bool wait(const T& x)
		{
			auto lk = std::unique_lock<std::mutex>(this->mtx);
			this->cv.wait(lk, [&]{ return this->value == x; });
			return true;
		}

		// returns true only if the value was set; if we timed out, it returns false.
		bool wait(const T& x, std::chrono::nanoseconds timeout)
		{
			auto lk = std::unique_lock<std::mutex>(this->mtx);
			return this->cv.wait_for(lk, timeout, [&]{ return this->value == x; });
		}

		template <typename Predicate>
		bool wait_pred(Predicate p)
		{
			auto lk = std::unique_lock<std::mutex>(this->mtx);
			this->cv.wait(lk, p);
			return true;
		}

		// returns true only if the value was set; if we timed out, it returns false.
		template <typename Predicate>
		bool wait_pred(std::chrono::nanoseconds timeout, Predicate p)
		{
			auto lk = std::unique_lock<std::mutex>(this->mtx);
			return this->cv.wait_for(lk, timeout, p);
		}

		void notify_one() { this->cv.notify_one(); }
		void notify_all() { this->cv.notify_all(); }

	private:
		T value;
		std::mutex mtx;
		std::condition_variable cv;

		friend struct semaphore;
	};

	struct semaphore
	{
		semaphore(int64_t x) : cv(x) { }

		void post(int64_t num = 1)
		{
			{
				auto lk = std::unique_lock<std::mutex>(this->cv.mtx);
				this->cv.value += num;
			}
			if(num > 1) this->cv.notify_all();
			else        this->cv.notify_one();
		}

		void wait()
		{
			this->cv.wait_pred([this]() -> bool { return this->cv.value != 0; });
			this->cv.value -= 1;
		}

		bool wait(std::chrono::nanoseconds timeout)
		{
			if(!this->cv.wait_pred(timeout, [this]() -> bool { return this->cv.value != 0; }))
				return false;

			this->cv.value -= 1;
			return true;
		}

	private:
		condvar<int64_t> cv;
	};

	template <typename T>
	struct wait_queue
	{
		wait_queue() : sem(0) { }

		void push(T x)
		{
			{
				auto lk = std::unique_lock<std::mutex>(this->mtx);
				this->queue.push_back(std::move(x));
			}
			this->sem.post();
		}

		template <typename... Args>
		void emplace(Args&&... xs)
		{
			{
				auto lk = std::unique_lock<std::mutex>(this->mtx);
				this->queue.emplace_back(std::forward<Args&&>(xs)...);
			}
			this->sem.post();
		}

		void push_quiet(T x)
		{
			{
				this->queue.push_back(std::move(x));
				auto lk = std::unique_lock<std::mutex>(this->mtx);
			}
			this->pending_notifies++;
		}

		template <typename... Args>
		void emplace_quiet(Args&&... xs)
		{
			{
				auto lk = std::unique_lock<std::mutex>(this->mtx);
				this->queue.emplace_back(std::forward<Args&&>(xs)...);
			}
			this->pending_notifies++;
		}

		void notify_pending()
		{
			auto tmp = this->pending_notifies.exchange(0);
			this->sem.post(tmp);
		}

		T pop()
		{
			this->sem.wait();

			{
				auto lk = std::unique_lock<std::mutex>(this->mtx);
				auto ret = std::move(this->queue.front());
				this->queue.pop_front();

				return ret;
			}
		}

		size_t size() const
		{
			return this->queue.size();
		}

	private:
		std::atomic<int64_t> pending_notifies = 0;
		std::deque<T> queue;
		std::mutex mtx;     // mtx is for protecting the queue during push/pop
		semaphore sem;      // sem is for signalling when the queue has stuff (or not)
	};










	template <typename T>
	struct Synchronised
	{
	private:
		struct ReadLockedInstance;
		struct WriteLockedInstance;

		using Lk = std::shared_mutex;

		T value;
		mutable Lk lk;
		std::function<void ()> write_lock_callback = { };

	public:
		Synchronised() { }
		~Synchronised() { }

		Synchronised(const T& x) : value(x) { }
		Synchronised(T&& x) : value(std::move(x)) { }

		template <typename... Args>
		Synchronised(Args&&... xs) : value(std::forward<Args&&>(xs)...) { }

		Synchronised(Synchronised&&) = delete;
		Synchronised(const Synchronised&) = delete;

		Synchronised& operator = (Synchronised&&) = delete;
		Synchronised& operator = (const Synchronised&) = delete;

		void on_write_lock(std::function<void ()> fn)
		{
			this->write_lock_callback = std::move(fn);
		}

		template <typename Functor>
		void perform_read(Functor&& fn) const
		{
			std::shared_lock lk(this->lk);
			fn(this->value);
		}

		template <typename Functor>
		void perform_write(Functor&& fn)
		{
			if(this->write_lock_callback)
				this->write_lock_callback();

			std::unique_lock lk(this->lk);
			fn(this->value);
		}

		template <typename Functor>
		auto map_read(Functor&& fn) const -> decltype(fn(this->value))
		{
			std::shared_lock lk(this->lk);
			return fn(this->value);
		}

		template <typename Functor>
		auto map_write(Functor&& fn) -> decltype(fn(this->value))
		{
			if(this->write_lock_callback)
				this->write_lock_callback();

			std::unique_lock lk(this->lk);
			return fn(this->value);
		}


		Lk& getLock()
		{
			return this->lk;
		}

		ReadLockedInstance rlock() const
		{
			return ReadLockedInstance(*this);
		}

		WriteLockedInstance wlock()
		{
			if(this->write_lock_callback)
				this->write_lock_callback();

			return WriteLockedInstance(*this);
		}

	private:

		// static Lk& assert_not_held(Lk& lk) { if(lk.held()) assert(!"cannot move held Synchronised"); return lk; }

		struct ReadLockedInstance
		{
			const T* operator -> () { return &this->sync.value; }
			const T* get() { return &this->sync.value; }
			~ReadLockedInstance() { this->sync.lk.unlock_shared(); }

		private:
			ReadLockedInstance(const Synchronised& sync) : sync(sync) { this->sync.lk.lock_shared(); }

			ReadLockedInstance(ReadLockedInstance&&) = delete;
			ReadLockedInstance(const ReadLockedInstance&) = delete;

			ReadLockedInstance& operator = (ReadLockedInstance&&) = delete;
			ReadLockedInstance& operator = (const ReadLockedInstance&) = delete;

			const Synchronised& sync;

			friend struct Synchronised;
		};

		struct WriteLockedInstance
		{
			T* operator -> () { return &this->sync.value; }
			T* get() { return &this->sync.value; }
			~WriteLockedInstance() { this->sync.lk.unlock(); }

		private:
			WriteLockedInstance(Synchronised& sync) : sync(sync) { this->sync.lk.lock(); }

			WriteLockedInstance(WriteLockedInstance&&) = delete;
			WriteLockedInstance(const WriteLockedInstance&) = delete;

			WriteLockedInstance& operator = (WriteLockedInstance&&) = delete;
			WriteLockedInstance& operator = (const WriteLockedInstance&) = delete;

			Synchronised& sync;

			friend struct Synchronised;
		};
	};
}
