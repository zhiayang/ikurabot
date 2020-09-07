// irc.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include "irc.h"
#include "defs.h"
#include "types.h"
#include "network.h"
#include "msgqueue.h"

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


	constexpr const char* MAGIC_OWNER_USERID = "@@__owner__@@";

	// irc backend stuff
	struct IRCServer;
	struct Channel : ikura::Channel
	{
		Channel() : name(""), lurk(false), respondToPings(false) { }
		Channel(IRCServer* srv, std::string n, std::string nick, bool l, bool p, bool si, bool mh, std::string cp)
			: name(std::move(n)), nickname(std::move(nick)), lurk(l), respondToPings(p), silentInterpErrors(si),
			  runMessageHandlers(mh), commandPrefix(std::move(cp)), server(srv) { }

		virtual std::string getName() const override;
		virtual std::string getUsername() const override;
		virtual std::string getCommandPrefix() const override;
		virtual bool shouldReplyMentions() const override;
		virtual bool shouldPrintInterpErrors() const override;
		virtual bool shouldRunMessageHandlers() const override;
		virtual Backend getBackend() const override { return Backend::IRC; }
		virtual bool shouldLurk() const override;
		virtual bool checkUserPermissions(ikura::str_view userid, const PermissionSet& required) const override;

		virtual void sendMessage(const Message& msg) const override;

	private:
		std::string name;
		std::string nickname;
		bool lurk = false;
		bool respondToPings = false;
		bool silentInterpErrors = false;
		bool runMessageHandlers = false;
		std::string commandPrefix;

		IRCServer* server = nullptr;

		friend struct IRCServer;
	};

	struct QueuedMsg
	{
		std::string msg;
		bool disconnected;

		QueuedMsg(std::string msg) : msg(std::move(msg)), disconnected(false) { }

		static QueuedMsg disconnect() { return QueuedMsg(true); }

	private:
		QueuedMsg(bool x) : msg(), disconnected(x) { }
	};

	struct IRCServer
	{
		IRCServer(const config::irc::Server& server, std::chrono::nanoseconds timeout);
		~IRCServer();

		void processMessage(ikura::str_view msg);
		void sendRawMessage(ikura::str_view msg);
		void sendMessage(ikura::str_view channel, ikura::str_view msg);

		void logMessage(uint64_t timestamp, ikura::str_view user, ikura::str_view nick, Channel* chan, ikura::str_view message, bool isCmd);

		std::string name;
		std::string owner;
		std::string username;
		std::string nickname;
		ikura::string_set ignoredUsers;
		ikura::string_map<Channel> channels;

		MessageQueue<QueuedMsg> mqueue;

	private:
		Socket socket;
		bool is_connected = false;

		std::thread tx_thread;
		std::thread rx_thread;

		void connect();
		void disconnect();

		void send_worker();
		void recv_worker();

		friend void init();
		friend void shutdown();
	};

	void init();
	void shutdown();

	const Channel* getChannelFromServer(ikura::str_view server, ikura::str_view channel);

	// db stuff
	namespace db
	{
		struct IRCMessage : Serialisable
		{
			// in milliseconds, as usual.
			uint64_t timestamp = 0;

			std::string nickname;
			std::string username;

			std::string channel;
			std::string server;

			ikura::relative_str message;

			bool isCommand = false;

			virtual void serialise(Buffer& buf) const override;
			static std::optional<IRCMessage> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_IRC_LOG_MSG;
		};

		struct IRCMessageLog : Serialisable
		{
			std::vector<IRCMessage> messages;

			virtual void serialise(Buffer& buf) const override;
			static std::optional<IRCMessageLog> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_IRC_LOG;
		};

		struct IRCUser : Serialisable
		{
			std::string nickname;
			std::string username;

			uint64_t permissions;           // see defs.h/ikura::permissions

			std::vector<uint64_t> groups;

			virtual void serialise(Buffer& buf) const override;
			static std::optional<IRCUser> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_IRC_USER;
		};

		struct IRCChannel : Serialisable
		{
			std::string name;

			// map from username to user.
			ikura::string_map<IRCUser> knownUsers;

			// map from nickname to username
			ikura::string_map<std::string> usernameMapping;

			const IRCUser* getUser(ikura::str_view username) const;

			virtual void serialise(Buffer& buf) const override;
			static std::optional<IRCChannel> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_IRC_CHANNEL;
		};

		struct IRCServer : Serialisable
		{
			std::string name;
			std::string hostname;

			// map from username to user.
			ikura::string_map<IRCChannel> channels;

			const IRCChannel* getChannel(ikura::str_view name) const;

			virtual void serialise(Buffer& buf) const override;
			static std::optional<IRCServer> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_IRC_SERVER;
		};

		struct IrcDB : Serialisable
		{
			ikura::string_map<IRCServer> servers;

			IRCMessageLog messageLog;

			const IRCServer* getServer(ikura::str_view name) const;

			virtual void serialise(Buffer& buf) const override;
			static std::optional<IrcDB> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_IRC_DB;
		};
	}
}
