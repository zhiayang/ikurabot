// synchro.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

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
		bool wait_pred(const T& x, Predicate p)
		{
			auto lk = std::unique_lock<std::mutex>(this->mtx);
			this->cv.wait(lk, p);
			return true;
		}

		// returns true only if the value was set; if we timed out, it returns false.
		template <typename Predicate>
		bool wait_pred(const T& x, std::chrono::nanoseconds timeout, Predicate p)
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
	};

	template <typename T, typename Lk>
	struct Synchronised
	{
	private:
		struct ReadLockedInstance;
		struct WriteLockedInstance;

		Lk lk;
		T value;

	public:
		~Synchronised() { }

		template <typename = std::enable_if_t<std::is_default_constructible_v<T>>>
		Synchronised() { }

		template <typename = std::enable_if_t<std::is_copy_constructible_v<T>>>
		Synchronised(const T& x) : value(x) { }

		template <typename = std::enable_if_t<std::is_move_constructible_v<T>>>
		Synchronised(T&& x) : value(std::move(x)) { }

		Synchronised(Synchronised&&) = delete;
		Synchronised(const Synchronised&) = delete;

		Synchronised& operator = (Synchronised&&) = delete;
		Synchronised& operator = (const Synchronised&) = delete;


		template <typename Functor>
		void perform_read(Functor&& fn)
		{
			std::shared_lock lk(this->lk);
			fn(this->value);
		}

		template <typename Functor>
		void perform_write(Functor&& fn)
		{
			std::unique_lock lk(this->lk);
			fn(this->value);
		}

		template <typename Functor>
		auto map_read(Functor&& fn) -> decltype(fn(this->value))
		{
			std::shared_lock lk(this->lk);
			return fn(this->value);
		}

		template <typename Functor>
		auto map_write(Functor&& fn) -> decltype(fn(this->value))
		{
			std::unique_lock lk(this->lk);
			return fn(this->value);
		}


		Lk& getLock()
		{
			return this->lk;
		}

		ReadLockedInstance rlock()
		{
			return ReadLockedInstance(*this);
		}

		WriteLockedInstance wlock()
		{
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
			ReadLockedInstance(Synchronised& sync) : sync(sync) { this->sync.lk.lock_shared(); }

			ReadLockedInstance(ReadLockedInstance&&) = delete;
			ReadLockedInstance(const ReadLockedInstance&) = delete;

			ReadLockedInstance& operator = (ReadLockedInstance&&) = delete;
			ReadLockedInstance& operator = (const ReadLockedInstance&) = delete;

			Synchronised& sync;

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
