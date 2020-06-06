// perms.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "zfu.h"
#include "perms.h"
#include "config.h"
#include "twitch.h"
#include "discord.h"
#include "serialise.h"

namespace ikura
{
	namespace perms
	{
		template <typename T>
		static void add_to_list(std::vector<T>& list, T elm)
		{
			if(auto it = std::find(list.begin(), list.end(), elm); it == list.end())
				list.push_back(elm);
		}

		template <typename T>
		static void remove_from_list(std::vector<T>& list, T elm)
		{
			if(auto it = std::find(list.begin(), list.end(), elm); it != list.end())
				list.erase(it);
		}



		// just +group1-group2*group3
		Result<PermissionSet> parseGroups(const Channel* chan, ikura::str_view sv, PermissionSet perms)
		{
			while(sv[0] == '+' || sv[0] == '-' || sv[0] == '*')
			{
				int mode = (sv[0] == '+') ? 0 : (sv[0] == '-') ? 1 : 2;
				sv.remove_prefix(1);

				if(sv.empty())
					return zpr::sprint("unexpected end of input");

				bool discord = (sv[0] == '%');
				if(discord) sv.remove_prefix(1);

				std::string name;
				while(sv.size() > 0)
				{
					if(sv[0] == '\\')
						sv.remove_prefix(1), name += sv[0];

					else if(sv[0] == '+' || sv[0] == '-' || sv[0] == '*')
						break;

					else
						name += sv[0];

					sv.remove_prefix(1);
				}

				auto foozle = [&mode](auto& wl, auto& bl, auto e) {
					if(mode == 0)
					{
						add_to_list(wl, e);         // add to whitelist
						remove_from_list(bl, e);    // remove from blacklist
					}
					else if(mode == 1)
					{
						add_to_list(bl, e);         // add to blacklist
						remove_from_list(wl, e);    // remove from whitelist
					}
					else
					{
						// remove from both
						remove_from_list(bl, e);
						remove_from_list(wl, e);
					}
				};

				if(discord)
				{
					auto dchan = dynamic_cast<const discord::Channel*>(chan);
					if(chan->getBackend() != Backend::Discord || !dchan)
						return zpr::sprint("cannot access roles while not in a discord channel");

					auto guild = dchan->getGuild();
					auto role = guild->getRole(name);

					if(role == nullptr)
						return zpr::sprint("nonexistent role '%s'", name);

					foozle(perms.role_whitelist, perms.role_blacklist, role->id);
				}
				else
				{
					auto grp = database().rlock()->sharedData.getGroup(name);
					if(grp == nullptr)
						return zpr::sprint("nonexistent group '%s'", name);

					foozle(perms.whitelist, perms.blacklist, grp->id);
				}
			}

			if(!sv.empty())
				return zpr::sprint("junk at end of permissions (%s)", sv);

			return perms;
		}

		// +3a+group+group+@discord role-@blacklist role+group with \+ plus
		Result<PermissionSet> parse(const Channel* chan, ikura::str_view sv, PermissionSet orig)
		{
			uint64_t flag = 0;

			bool merge = false;
			if(!sv.empty() && sv[0] == '+')
				sv.remove_prefix(1), merge = true;

			while(sv.size() > 0)
			{
				if('0' <= sv[0] && sv[0] <= '9')
					flag = (16 * flag) + (sv[0] - '0');

				else if('a' <= sv[0] && sv[0] <= 'f')
					flag = (16 * flag) + (10 + sv[0] - 'a');

				else if('A' <= sv[0] && sv[0] <= 'F')
					flag = (16 * flag) + (10 + sv[0] - 'A');

				else
					break;

				sv.remove_prefix(1);
			}

			auto newperms = orig;
			newperms.flags = (flag | (merge ? orig.flags : 0));

			if(sv.empty())
				return newperms;

			return parseGroups(chan, sv, newperms);
		}


		std::string print(const Channel* chan, const PermissionSet& perms)
		{
			auto get_grp_name = [](uint64_t id) -> std::string {
				if(auto g = database().rlock()->sharedData.getGroup(id); g != nullptr)
					return g->name;

				return "??";
			};

			auto out = zpr::sprint("flags: %x, w: %s, b: %s",
				perms.flags,
				zfu::listToString(perms.whitelist, get_grp_name),
				zfu::listToString(perms.blacklist, get_grp_name)
			);

			if(chan->getBackend() == Backend::Discord)
			{
				auto dchan = dynamic_cast<const discord::Channel*>(chan);
				assert(dchan);

				auto get_role_name = [dchan](discord::Snowflake id) -> std::string {
					if(auto it = dchan->getGuild()->roles.find(id); it != dchan->getGuild()->roles.end())
						return it->second.name;

					return "??";
				};

				out += zpr::sprint(", dw: %s, db: %s",
					zfu::listToString(perms.role_whitelist, get_role_name),
					zfu::listToString(perms.role_blacklist, get_role_name)
				);
			}

			return out;
		}



		bool updateUserPermissions(const Channel* chan, ikura::str_view user, ikura::str_view perm_str)
		{
			// we're treating this like a whitelist/blacklist input (ie. +group-group), but
			// we re-interpret it as add and remove.
			auto res = perms::parseGroups(chan, perm_str, { });
			if(!res.has_value())
			{
				chan->sendMessage(Message(res.error()));
				return false;
			}

			if(!res->role_whitelist.empty() || !res->role_blacklist.empty())
			{
				chan->sendMessage(Message(zpr::sprint("cannot modify discord roles")));
				return false;
			}

			auto update_groups = [](auto& db, const PermissionSet& ps, const auto& userid, auto& usergroups, Backend backend) {
				for(auto x : ps.whitelist)
				{
					add_to_list(usergroups, x);

					auto g = db.sharedData.getGroup(x);
					assert(g);

					g->addUser(userid, backend);
				}

				for(auto x : ps.blacklist)
				{
					remove_from_list(usergroups, x);

					auto g = db.sharedData.getGroup(x);
					assert(g);

					g->removeUser(userid, backend);
				}
			};

			// handle for twitch and discord separately -- but we need to somehow update in both.
			// TODO: figure out a way to update both twitch and discord here?
			if(chan->getBackend() == Backend::Twitch)
			{
				return database().map_write([&](auto& db) -> bool {
					auto& twch = db.twitchData.channels[chan->getName()];
					auto userid = twch.usernameMapping[user];
					if(userid.empty())
					{
						fail:
						chan->sendMessage(Message(zpr::sprint("unknown user '%s'", user)));
						return false;
					}

					auto twusr = twch.getUser(userid);
					if(twusr == nullptr) goto fail;

					update_groups(db, res.unwrap(), twusr->id, twusr->groups, Backend::Twitch);
					return true;
				});
			}
			else if(chan->getBackend() == Backend::Discord)
			{
				auto dchan = dynamic_cast<const discord::Channel*>(chan);
				assert(dchan);

				auto guild = dchan->getGuild();
				assert(guild);

				discord::Snowflake userid;
				if(auto tmp = discord::parseMention(user, nullptr); tmp.has_value())
				{
					userid = tmp.value();
				}
				else
				{
					if(auto it = guild->usernameMap.find(user); it != guild->usernameMap.end())
						userid = it->second;

					else if(auto it = guild->nicknameMap.find(user); it != guild->nicknameMap.end())
						userid = it->second;
				}

				auto no_user = [&]() -> bool {
					chan->sendMessage(Message(zpr::sprint("unknown user '%s'", user)));
					return false;
				};

				if(userid.empty())
				{
					return no_user();
				}
				else if(userid == config::discord::getUserId())
				{
					chan->sendMessage(Message(zpr::sprint("cannot usermod the bot")));
					return false;
				}

				return database().map_write([&](auto& db) {
					auto g = const_cast<discord::DiscordGuild*>(guild);
					auto usr = g->getUser(userid);
					if(!usr) return no_user();

					update_groups(db, res.unwrap(), usr->id.str(), usr->groups, Backend::Discord);
					return true;
				});
			}
			else
			{
				return false;
			}
		}
	}




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
