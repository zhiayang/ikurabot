// main.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <thread>
#include <chrono>

#include "db.h"
#include "ast.h"
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


	// ikura::cmd::interpreter().wlock()->addGlobal("asdf", ikura::cmd::interp::Value::of_integer(77));

	auto expr = ikura::cmd::ast::parse("$asdf ^= 2");
	ikura::cmd::CmdContext cs;
	expr->evaluate(ikura::cmd::interpreter().wlock().get(), cs);

	expr = ikura::cmd::ast::parse("$asdf");
	auto ret = expr->evaluate(ikura::cmd::interpreter().wlock().get(), cs);
	zpr::println("> %s", ret->str());

	ikura::database().rlock()->sync();
	return 0;





	if(ikura::config::haveTwitch())
		ikura::twitch::init();

	auto thr = std::thread([]() {
		while(true)
		{
			auto ch = fgetc(stdin);
			if(ch == '\n' || ch == '\r')
				continue;

			if(ch == 'q')
			{
				ikura::lg::log("ikura", "stopping...");
				break;
			}
		}
	});

	thr.join();
	ikura::twitch::shutdown();

	std::this_thread::sleep_for(1s);
	ikura::database().rlock()->sync();
}

