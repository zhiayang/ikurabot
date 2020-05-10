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


	// auto tw = ikura::db::TwitchUser();
	// tw.id = "asdf";
	// tw.username = "zhiayang";
	// tw.displayname = "zhiayang";

	// ikura::database().twitchData.knownTwitchUsers.insert({ "zhiayang", tw });
	// ikura::database().sync();

	zpr::println("%s -> %s", "zhiayang", ikura::database().twitchData.knownTwitchUsers["zhiayang"].id);


	// auto ws = ikura::WebSocket(ikura::URL("wss://irc-ws.chat.twitch.tv"), 500ms);

	// ikura::condvar<bool> cv;
	// ws.onReceiveText([&](bool, std::string_view sv) {
	// 	zpr::println("%s", sv);
	// 	// cv.set(true);
	// });

	// ws.connect();

	// // cv.wait(true, 3s);
	// std::this_thread::sleep_for(5s);
	// ws.disconnect();
}

