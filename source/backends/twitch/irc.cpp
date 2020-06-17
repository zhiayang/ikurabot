// irc.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "defs.h"
#include "irc.h"
#include "types.h"

namespace ikura::irc
{
	static void parse_tags(IRCMessage& msg, ikura::str_view tags)
	{
		/*
			<message>       ::= ['@' <tags> <SPACE>] ...
			<tags>          ::= <tag> [';' <tag>]*
			<tag>           ::= <key> ['=' <escaped_value>]
			<key>           ::= [ <client_prefix> ] [ <vendor> '/' ] <key_name>
			<client_prefix> ::= '+'
			<key_name>      ::= <non-empty sequence of ascii letters, digits, hyphens ('-')>
			<escaped_value> ::= <sequence of zero or more utf8 characters except NUL, CR, LF, semicolon (`;`) and SPACE>
			<vendor>        ::= <host>
		*/

		// this expects only the tags part -- from '@' inclusive to 'SPACE' exclusive.
		assert(tags[0] == '@');
		tags.remove_prefix(1);

		while(tags.size() > 0)
		{
			std::string key = tags.take(tags.find_first_of("=;")).str();
			tags.remove_prefix(key.size());

			// set it first.
			msg.tags[key] = "";

			// perfectly valid to just have a tag without a value.
			if(tags.empty() || tags[0] == ';')
				break;

			// this should be an '=' now.
			assert(tags[0] == '=');
			tags.remove_prefix(1);

			// somehow, also perfectly valid to have '=' and no value
			if(tags.empty())
				break;

			// parse the value.
			auto tmp = tags.take(tags.find(';'));
			std::string value; value.reserve(tmp.size());

			// unescape it.
			size_t k = 0; // extra offset -- in case of escapes.
			for(size_t i = 0; i < tmp.size(); i++)
			{
				if(tmp[i] == '\\')
				{
					if(i + 1 < tmp.size())
					{
						auto c = tmp[i + 1];
						if(c == ':')        value += ';';
						else if(c == 's')   value += ' ';
						else if(c == '\\')  value += '\\';
						else if(c == 'r')   value += '\r';
						else if(c == 'n')   value += '\n';
						else                value += c;

						// skip the next one.
						k += 1;
					}
				}
				else
				{
					value += tmp[i];
				}
			}

			msg.tags[key] = value;

			tags.remove_prefix(tmp.size());
			if(tags.empty())
				break;

			assert(tags[0] == ';');
			tags.remove_prefix(1);
		}
	}

	static void parse_prefix(IRCMessage& msg, ikura::str_view prefix)
	{
		/*
			<message>       ::= ... [':' <prefix> <SPACE> ] ...
			<prefix>        ::= <servername> | <nick> [ '!' <user> ] [ '@' <host> ]
		*/

		// similarly, this only expects the prefix part -- from ':' inclusive to 'SPACE' exclusive.
		assert(prefix[0] == ':');
		prefix.remove_prefix(1);

		auto nick = prefix.take(prefix.find_first_of("!@"));
		prefix.remove_prefix(nick.size());

		msg.nick = nick;

		// no username or host.
		if(prefix.empty())
			return;

		if(prefix[0] == '!')
		{
			prefix.remove_prefix(1);

			// username.
			auto user = prefix.take(prefix.find('@'));
			prefix.remove_prefix(user.size());

			msg.user = user;
		}

		if(prefix.empty())
			return;

		if(prefix[0] == '@')
			msg.host = prefix.drop(1);
	}

	std::optional<IRCMessage> parseMessage(ikura::str_view input)
	{
		IRCMessage msg;

		ikura::str_view x;
		ikura::str_view xs;

		std::tie(x, xs) = util::bisect(input, ' ');
		if(x.empty()) return { };

		/*
			<message>       ::= ['@' <tags> <SPACE>] [':' <prefix> <SPACE> ] <command> <params> <crlf>

			<prefix>        ::= <servername> | <nick> [ '!' <user> ] [ '@' <host> ]
			<command>       ::= <letter> { <letter> } | <number> <number> <number>
			<SPACE>         ::= ' ' { ' ' }
			<params>        ::= <SPACE> [ ':' <trailing> | <middle> <params> ]

			<middle>        ::= <Any *non-empty* sequence of octets not including SPACE
			               		or NUL or CR or LF, the first of which may not be ':'>
			<trailing>      ::= <Any, possibly *empty*, sequence of octets not including
			                 	NUL or CR or LF>
			<crlf>          ::= CR LF
		*/

		// :<user>!<user>@<user>.tmi.twitch.tv PRIVMSG #<channel> :This is a sample message

		if(x[0] == '@')
		{
			parse_tags(msg, x);
			std::tie(x, xs) = util::bisect(xs, ' ');
		}

		if(x[0] == ':')
		{
			parse_prefix(msg, x);
			std::tie(x, xs) = util::bisect(xs, ' ');
		}

		// no command is not ok.
		if(x.empty())
			return { };

		// next comes the command.
		msg.command = x;

		// no params is ok.
		if(xs.empty())
			return msg;

		do {
			if(xs[0] == ':')
			{
				xs.remove_prefix(1);

				if(msg.command == "PRIVMSG" || msg.command == "NOTICE")
				{
					if(xs.find('\x01') == 0)
					{
						xs.remove_prefix(1);

						// invalid! CTCP must begin and end with \x01
						if(xs.back() != '\x01')
							return { };

						xs.remove_suffix(1);
						msg.isCTCP = true;

						std::tie(x, xs) = util::bisect(xs, ' ');
						msg.ctcpCommand = x;
					}
				}

				// trailing parameter -- the rest is the last param.
				msg.params.push_back(xs);
				break;
			}
			else
			{
				// split again
				std::tie(x, xs) = util::bisect(xs, ' ');
				msg.params.push_back(x);

				if(xs.empty())
					break;
			}
		} while(true);

		return msg;
	}
}
