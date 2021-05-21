// main.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <thread>
#include <chrono>

#include "db.h"
#include "zfu.h"
#include "irc.h"
#include "defs.h"
#include "async.h"
#include "config.h"

using namespace std::chrono_literals;

/*
	TODO (general):

	markov state browser/editor

	TODO (bot):

	add post-processing pass to discord messages right before sending, that looks for
	known emote strings in messages and replaces them appropriately.

	add blacklist for discord channels to not train markov
	possibly add blacklist for other stuff (eg. commands)

	add blacklist for bttv/ffz emotes.

	builtin function to reload bttv and ffz emotes
	console command to re-emotify all logged messages (and move this functionality out of markov)
	proper oauth flow for twitch using clientid+clientsecret

	still having an issue where twitch messages can get lost. no idea wtf is the issue, and this time
	we didn't even get the NOTICE about it. hard to replicate.


	...

	refactor/rewrite kissnet


	TODO (interpreter):

	pipelining needs to be smarter. we need to split into pipeline components immediately, and apply
	the arguments appropriately. probably should find a way to just pass the args directly instead
	of manipulating strings.

	performance is fucking trash, even when optimised.
	!asis |> cycle literally takes ~1s on -O2, and more than 5s on O0. main culprit is probably (hopefully)
	argument copying during all the recursion. i hope it's not Value spawn/destroy...

	type finnagling for math? ideally sqrt(-1) should directly return a complex number...
	maybe a proper generic solver one day. it works if we just hamfist it right now, so meh...
*/



namespace ikura
{
	static ThreadPool<4> pool;
	ThreadPool<4>& dispatcher()
	{
		return pool;
	}

	static std::chrono::system_clock::time_point start_time;
	std::chrono::system_clock::duration get_uptime()
	{
		return std::chrono::system_clock::now() - start_time;
	}
}

int main(int argc, char** argv)
{
	if(argc < 3)
	{
		zpr::println("usage: ./ikurabot <config.json> <database.db> [--create] [--readonly]");
		exit(1);
	}

	ikura::start_time = std::chrono::system_clock::now();

	auto opts = zfu::map(zfu::rangeOpen(1, argc), [&](int i) -> auto {
		return std::string(argv[i]);
	});

	ikura::lg::log("ikura", "starting...");
	if(!ikura::config::load(opts[0]))
		ikura::lg::fatal("cfg", "failed to load config file '{}'", opts[0]);

	if(!ikura::db::load(opts[1], zfu::contains(opts, "--create"), zfu::contains(opts, "--readonly")))
		ikura::lg::fatal("db", "failed to load database '{}'", opts[1]);

	if(ikura::config::haveTwitch())
	{
		ikura::twitch::init();
		ikura::twitch::initEmotes();
	}

	if(ikura::config::haveDiscord())
		ikura::discord::init();

	if(ikura::config::haveIRC())
		ikura::irc::init();

	ikura::markov::init();

	// when this returns, then the bot should shutdown.
	ikura::console::init();

	ikura::discord::shutdown();
	ikura::twitch::shutdown();
	ikura::markov::shutdown();
	ikura::irc::shutdown();

	ikura::database().rlock()->sync();
	return 0;
}



