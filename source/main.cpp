// main.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <thread>
#include <chrono>

#include "db.h"
#include "zfu.h"
#include "defs.h"
#include "async.h"
#include "config.h"

using namespace std::chrono_literals;

/*
	TODO:

	onClose callbacks for both Socket and WebSocket
	handle discord opcode7 reconnect
	builtin function to reload bttv and ffz emotes
	console command to re-emotify all logged messages (and move this functionality out of markov)
	start logging discord messages
	proper oauth flow for twitch using clientid+clientsecret
	see if zero-width spaces are preserved by twitch
	make socket bind not abort() the program on error

	...

	refactor/rewrite kissnet
*/

int main(int argc, char** argv)
{
	if(argc < 3)
	{
		zpr::println("usage: ./ikurabot <config.json> <database.db> [--create]");
		exit(1);
	}

	ikura::lg::log("ikura", "starting...");
	if(!ikura::config::load(argv[1]))
		ikura::lg::fatal("cfg", "failed to load config file '%s'", argv[1]);

	if(!ikura::db::load(argv[2], (argc > 3 && std::string(argv[3]) == "--create")))
		ikura::lg::fatal("db", "failed to load database '%s'", argv[2]);

	if(ikura::config::haveTwitch())
		ikura::twitch::init();

	if(ikura::config::haveDiscord())
		ikura::discord::init();

	ikura::twitch::initEmotes();

	ikura::markov::init();

	// when this returns, then the bot should shutdown.
	ikura::console::init();

	ikura::discord::shutdown();
	ikura::twitch::shutdown();
	ikura::markov::shutdown();

	ikura::database().rlock()->sync();
}




namespace ikura
{
	static ThreadPool<4> pool;
	ThreadPool<4>& dispatcher()
	{
		return pool;
	}
}
