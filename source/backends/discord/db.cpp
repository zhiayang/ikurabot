// db.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "defs.h"
#include "discord.h"
#include "serialise.h"

namespace ikura::discord
{
	Snowflake::Snowflake(const std::string& s) : Snowflake(ikura::str_view(s)) { }

	Snowflake::Snowflake(ikura::str_view sv)
	{
		auto x = util::stou(sv);
		if(x.has_value())
		{
			this->value = x.value();
		}
		else
		{
			lg::error("discord", "invalid snowflake '%s'", sv);
			this->value = 0;
		}
	}

	void Snowflake::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.write(this->value);
	}

	std::optional<Snowflake> Snowflake::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);

		Snowflake ret;
		if(!rd.read(&ret.value))
			return { };

		return ret;
	}


	const DiscordRole* DiscordGuild::getRole(ikura::str_view name) const
	{
		if(auto it = this->roleNames.find(name); it != this->roleNames.end())
			return &this->roles.at(it->second);

		return nullptr;
	}


	const DiscordUser* DiscordGuild::getUser(Snowflake id) const
	{
		if(auto it = this->knownUsers.find(id); it != this->knownUsers.end())
			return &it.value();

		return nullptr;
	}


	DiscordRole* DiscordGuild::getRole(ikura::str_view name)
	{
		return const_cast<DiscordRole*>(static_cast<const DiscordGuild*>(this)->getRole(name));
	}

	DiscordUser* DiscordGuild::getUser(Snowflake id)
	{
		return const_cast<DiscordUser*>(static_cast<const DiscordGuild*>(this)->getUser(id));
	}





	void DiscordDB::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->guilds);
		wr.write(this->messageLog);

		wr.write(this->lastSequence);
		wr.write(this->lastSession);
	}

	std::optional<DiscordDB> DiscordDB::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		DiscordDB ret;

		if(!rd.read(&ret.guilds))
			return { };

		if(db::getVersion() >= 22)
		{
			if(!rd.read(&ret.messageLog))
				return { };
		}

		if(db::getVersion() >= 24)
		{
			if(!rd.read(&ret.lastSequence))
				return { };

			if(!rd.read(&ret.lastSession))
				return { };
		}

		return ret;
	}




	void DiscordGuild::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->id);
		wr.write(this->name);
		wr.write(this->roles);
		wr.write(this->channels);
		wr.write(this->knownUsers);
		wr.write(this->emotes);
	}

	std::optional<DiscordGuild> DiscordGuild::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		DiscordGuild ret;

		if(!rd.read(&ret.id))
			return { };

		if(!rd.read(&ret.name))
			return { };

		if(!rd.read(&ret.roles))
			return { };

		if(!rd.read(&ret.channels))
			return { };

		if(!rd.read(&ret.knownUsers))
			return { };

		if(db::getVersion() >= 27)
		{
			if(!rd.read(&ret.emotes))
				return { };
		}
		else
		{
			ikura::string_map<std::pair<Snowflake, bool>> emotes;
			if(!rd.read(&emotes))
				return { };

			for(const auto& [ k, v ] : emotes)
				ret.emotes[k] = { v.first, v.second ? EmoteFlags::IS_ANIMATED : 0 };
		}


		for(auto it = ret.roles.begin(); it != ret.roles.end(); ++it)
			ret.roleNames[it->second.name] = it->second.id;

		for(auto it = ret.knownUsers.begin(); it != ret.knownUsers.end(); ++it)
			ret.usernameMap[it->second.username] = it->second.id;

		for(auto it = ret.knownUsers.begin(); it != ret.knownUsers.end(); ++it)
			ret.nicknameMap[it->second.nickname] = it->second.id;

		return ret;
	}





	void DiscordChannel::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->id);
		wr.write(this->name);
	}

	std::optional<DiscordChannel> DiscordChannel::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		DiscordChannel ret;

		if(!rd.read(&ret.id))
			return { };

		if(!rd.read(&ret.name))
			return { };

		return ret;
	}



	void DiscordUser::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->id);
		wr.write(this->username);
		wr.write(this->nickname);

		wr.write(this->permissions);
		wr.write(this->groups);
		wr.write(this->discordRoles);
	}

	std::optional<DiscordUser> DiscordUser::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		DiscordUser ret;

		if(!rd.read(&ret.id))
			return { };

		if(!rd.read(&ret.username))
			return { };

		if(!rd.read(&ret.nickname))
			return { };

		if(!rd.read(&ret.permissions))
			return { };

		if(!rd.read(&ret.groups))
			return { };

		if(!rd.read(&ret.discordRoles))
			return { };

		return ret;
	}




	void DiscordRole::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->id);
		wr.write(this->name);
		wr.write(this->discordPerms);
	}

	std::optional<DiscordRole> DiscordRole::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		DiscordRole ret;

		if(!rd.read(&ret.id))
			return { };

		if(!rd.read(&ret.name))
			return { };

		if(!rd.read(&ret.discordPerms))
			return { };

		return ret;
	}
}
