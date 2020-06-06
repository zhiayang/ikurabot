// main.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <thread>
#include <chrono>

#include "db.h"
#include "defs.h"
#include "async.h"
#include "config.h"
#include "twitch.h"
#include "markov.h"
#include "discord.h"
#include "network.h"

using namespace std::chrono_literals;

/*
	TODO:

	retrain markov with bttv/ffz emote detection
	handle discord opcode7 reconnect
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

	// start twitch and discord simultaneously since they like to be slow at
	// network stuff.
	if(ikura::config::haveTwitch())
		ikura::twitch::init();

	// if(ikura::config::haveDiscord())
	// 	ikura::discord::init();

	// this just starts a worker thread that automatically refreshes the emotes
	ikura::twitch::initEmotes();

	// this just starts a worker thread to process input in the background.
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
