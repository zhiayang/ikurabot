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

	builtin function to reload bttv and ffz emotes
	console command to re-emotify all logged messages (and move this functionality out of markov)
	start logging discord messages
	proper oauth flow for twitch using clientid+clientsecret

	...

	refactor/rewrite kissnet
*/

int main(int argc, char** argv)
{
	if(argc < 3)
	{
		zpr::println("usage: ./ikurabot <config.json> <database.db> [--create] [--readonly]");
		exit(1);
	}

	auto opts = zfu::map(zfu::rangeOpen(1, argc), [&](int i) -> auto {
		return std::string(argv[i]);
	});

	ikura::lg::log("ikura", "starting...");
	if(!ikura::config::load(opts[0]))
		ikura::lg::fatal("cfg", "failed to load config file '%s'", opts[0]);

	if(!ikura::db::load(opts[1], zfu::contains(opts, "--create"), zfu::contains(opts, "--readonly")))
		ikura::lg::fatal("db", "failed to load database '%s'", opts[1]);

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
