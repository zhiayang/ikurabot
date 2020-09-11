// perms.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <vector>

#include "types.h"

namespace ikura
{
	struct PermissionSet : Serialisable
	{
		uint64_t flags = 0;     // see defs.h/ikura::permissions
		std::vector<uint64_t> whitelist;
		std::vector<uint64_t> blacklist;

		std::vector<discord::Snowflake> role_whitelist;
		std::vector<discord::Snowflake> role_blacklist;

		bool check(uint64_t flags, const std::vector<uint64_t>& groups, const std::vector<discord::Snowflake>& discordRoles) const;

		static PermissionSet fromFlags(uint64_t f)
		{
			PermissionSet ret;
			ret.flags = f;

			return ret;
		}

		virtual void serialise(Buffer& buf) const override;
		static std::optional<PermissionSet> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_PERMISSION_SET;
	};

	namespace perms
	{
		Result<PermissionSet> parseGroups(const Channel* chan, ikura::str_view sv, PermissionSet orig);
		Result<PermissionSet> parse(const Channel* chan, ikura::str_view sv, PermissionSet orig);

		bool updateUserPermissions(const Channel* chan, ikura::str_view user, ikura::str_view perm_str);
		std::optional<std::string> printUserGroups(const Channel* chan, ikura::str_view user);

		std::string print(const Channel* chan, const PermissionSet& perms);
	}
}
