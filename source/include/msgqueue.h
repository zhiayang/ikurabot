// msgqueue.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include "synchro.h"

namespace ikura
{
	template <typename RxMsg, typename TxMsg = RxMsg>
	struct MessageQueue
	{
		MessageQueue() { }

		RxMsg pop_receive()                 { return this->rxqueue.pop(); }
		void notify_pending_receives()      { this->rxqueue.notify_pending(); }
		void push_receive(RxMsg x)          { this->rxqueue.push(std::move(x)); }
		void push_receive_quiet(RxMsg x)    { this->rxqueue.push_quiet(std::move(x)); }
		template <typename... Args> void emplace_receive(Args&&... xs)       { this->rxqueue.emplace(std::forward<Args&&>(xs)...); }
		template <typename... Args> void emplace_receive_quiet(Args&&... xs) { this->rxqueue.emplace_quiet(std::forward<Args&&>(xs)...); }


		TxMsg pop_send()                    { return this->txqueue.pop(); }
		void notify_pending_sends()         { this->txqueue.notify_pending(); }
		void push_send(TxMsg x)             { this->txqueue.push(std::move(x)); }
		void push_send_quiet(TxMsg x)       { this->txqueue.push_quiet(std::move(x)); }
		template <typename... Args> void emplace_send(Args&&... xs)          { this->txqueue.emplace(std::forward<Args&&>(xs)...); }
		template <typename... Args> void emplace_send_quiet(Args&&... xs)    { this->txqueue.emplace_quiet(std::forward<Args&&>(xs)...); }


	private:
		// wait_queues are internally synchronised, so we should not use external locking here.
		// also we *can't* actually use external locking, because we must not be holding the lock
		// while doing pop() which blocks. if we hold the lock and block, then nobody else can
		// acquire the lock to put things in the queue, which deadlocks us.
		ikura::wait_queue<RxMsg> rxqueue;
		ikura::wait_queue<TxMsg> txqueue;
	};
}
