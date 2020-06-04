// db.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

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




	void DiscordDB::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->guilds);
	}

	std::optional<DiscordDB> DiscordDB::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		DiscordDB ret;

		if(!rd.read(&ret.guilds))
			return { };

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
	}

	std::optional<DiscordGuild> DiscordGuild::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		DiscordGuild ret;

		if(!rd.read(&ret.id))
			return { };

		if(!rd.read(&ret.name))
			return { };

		if(!rd.read(&ret.roles))
			return { };

		if(!rd.read(&ret.channels))
			return { };

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
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

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
		wr.write(this->name);
	}

	std::optional<DiscordUser> DiscordUser::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		DiscordUser ret;

		if(!rd.read(&ret.id))
			return { };

		if(!rd.read(&ret.name))
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
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		DiscordRole ret;

		if(!rd.read(&ret.id))
			return { };

		if(!rd.read(&ret.name))
			return { };

		if(!rd.read(&ret.discordPerms))
			return { };

		return ret;
	}


	void DiscordUserCredentials::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->permissions);
		wr.write(this->groups);
		wr.write(this->discordRoles);
	}

	std::optional<DiscordUserCredentials> DiscordUserCredentials::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		DiscordUserCredentials ret;

		if(!rd.read(&ret.permissions))
			return { };

		if(!rd.read(&ret.groups))
			return { };

		if(!rd.read(&ret.discordRoles))
			return { };

		return ret;
	}
}
