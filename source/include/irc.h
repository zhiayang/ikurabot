// irc.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include "irc.h"
#include "defs.h"
#include "types.h"
#include "network.h"

namespace ikura::config::irc
{
	struct Server;
	struct Channel;
}

namespace ikura::irc
{
	// parser stuff
	struct IRCMessage
	{
		// all of these reference the original message to keep this lightweight.
		ikura::str_view user;
		ikura::str_view nick;
		ikura::str_view host;
		ikura::str_view command;
		std::vector<ikura::str_view> params;
		ikura::string_map<std::string> tags;

		bool isCTCP = false;
		ikura::str_view ctcpCommand;
	};

	std::optional<IRCMessage> parseMessage(ikura::str_view);



	// irc backend stuff
	struct IRCServer;
	struct Channel : ikura::Channel
	{
		Channel() : name(""), lurk(false), respondToPings(false) { }
		Channel(IRCServer* srv, std::string n, bool l, bool p, bool si, bool mh, std::string cp)
			: name(std::move(n)), lurk(l), respondToPings(p), silentInterpErrors(si), runMessageHandlers(mh),
			  commandPrefix(std::move(cp)), server(srv) { }

		virtual std::string getName() const override;
		virtual std::string getUsername() const override;
		virtual std::string getCommandPrefix() const override;
		virtual bool shouldReplyMentions() const override;
		virtual bool shouldPrintInterpErrors() const override;
		virtual bool shouldRunMessageHandlers() const override;
		virtual Backend getBackend() const override { return Backend::IRC; }
		virtual bool checkUserPermissions(ikura::str_view userid, const PermissionSet& required) const override;

		virtual void sendMessage(const Message& msg) const override;

	private:
		std::string name;
		bool lurk = false;
		bool respondToPings = false;
		bool silentInterpErrors = false;
		bool runMessageHandlers = false;
		std::string commandPrefix;

		mutable std::string lastSentMessage;

		IRCServer* server = nullptr;

		friend struct IRCServer;
	};

	struct IRCServer
	{
		IRCServer(const config::irc::Server& server, std::chrono::nanoseconds timeout);

		void processMessage(ikura::str_view msg);
		void sendRawMessage(ikura::str_view msg);
		void sendMessage(ikura::str_view channel, ikura::str_view msg);

		void logMessage(uint64_t timestamp, ikura::str_view userid, Channel* chan, ikura::str_view message,
			const std::vector<ikura::relative_str>& emote_idxs, bool isCmd);


	private:
		Socket socket;
		bool is_connected = false;

		std::thread tx_thread;
		std::thread rx_thread;

		void connect();
		void disconnect();

		friend void send_worker();
		friend void recv_worker();

		friend void init();
		friend void shutdown();
	};

	void init();
}
