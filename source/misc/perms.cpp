// perms.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "defs.h"
#include "discord.h"
#include "serialise.h"

namespace ikura
{
	bool PermissionSet::check(uint64_t given, const std::vector<uint64_t>& groups,
		const std::vector<discord::Snowflake>& discordRoles) const
	{
		auto check_flags = [&]() -> bool {
			bool is_owner = (given & permissions::OWNER);

			// if the required permissions are 0, then by default it is owner only.
			// otherwise, it is just a simple & of the perms. this does mean that you
			// can have commands that can only be executed by subscribers but not moderators, for example.
			if(this->flags == 0) return is_owner;
			else                 return is_owner || ((this->flags & given) != 0);
		};

		auto check_list = [&](const std::vector<uint64_t>& list) -> bool {
			for(auto x : list)
				if(std::find(groups.begin(), groups.end(), x) != groups.end())
					return true;

			return false;
		};

		auto flag_ok = check_flags();
		if(flag_ok)
		{
			// if we're ok already, just make sure we're not blacklisted.
			return !check_list(this->blacklist);
		}
		else
		{
			// else, check if we're in the whitelist.
			return check_list(this->whitelist);
		}
	}


	void PermissionSet::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->flags);
		wr.write(this->whitelist);
		wr.write(this->blacklist);
		wr.write(this->role_whitelist);
		wr.write(this->role_blacklist);
	}

	std::optional<PermissionSet> PermissionSet::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);

		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		PermissionSet ret;
		if(!rd.read(&ret.flags))
			return { };

		if(!rd.read(&ret.whitelist))
			return { };

		if(!rd.read(&ret.blacklist))
			return { };

		if(!rd.read(&ret.role_whitelist))
			return { };

		if(!rd.read(&ret.role_blacklist))
			return { };

		return ret;
	}

}
