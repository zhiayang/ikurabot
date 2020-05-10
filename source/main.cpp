// main.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <thread>
#include <chrono>

#include "db.h"
#include "defs.h"
#include "network.h"

using namespace std::chrono_literals;

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

	std::this_thread::sleep_for(std::chrono::seconds::max());

	ikura::twitch::shutdown();
}

