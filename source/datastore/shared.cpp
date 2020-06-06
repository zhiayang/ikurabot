// shared.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "serialise.h"

namespace ikura::db
{
	void SharedDB::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->groups);
	}

	std::optional<SharedDB> SharedDB::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		SharedDB ret;

		if(!rd.read(&ret.groups))
			return { };

		for(auto it = ret.groups.begin(); it != ret.groups.end(); ++it)
			ret.groupIds[it->second.id] = it->second.name;

		return ret;
	}

	Group* SharedDB::getGroup(ikura::str_view name)
	{
		if(auto it = this->groups.find(name); it != this->groups.end())
			return &it.value();

		return nullptr;
	}

	const Group* SharedDB::getGroup(ikura::str_view name) const
	{
		return const_cast<SharedDB*>(this)->getGroup(name);
	}

	Group* SharedDB::getGroup(uint64_t id)
	{
		if(auto it = this->groupIds.find(id); it != this->groupIds.end())
			return &this->groups[it->second];

		return nullptr;
	}

	const Group* SharedDB::getGroup(uint64_t id) const
	{
		return const_cast<SharedDB*>(this)->getGroup(id);
	}

	bool SharedDB::addGroup(ikura::str_view name)
	{
		if(auto it = this->groups.find(name); it != this->groups.end())
			return false;

		Group g;
		g.id = this->groups.size();
		g.name = name;

		this->groups[name] = g;
		this->groupIds[g.id] = name;

		return true;
	}

	bool SharedDB::removeGroup(ikura::str_view name)
	{
		if(auto it = this->groups.find(name); it != this->groups.end())
		{
			return false;
		}
		else
		{
			auto id = it->second.id;
			this->groups.erase(it);
			this->groupIds.erase(id);
			return true;
		}
	}





	void Group::addUser(const std::string& userid, Backend backend)
	{
		auto it = std::find_if(this->members.begin(), this->members.end(), [&](const auto& g) -> bool {
			return g.id == userid && g.backend == backend;
		});

		if(it == this->members.end())
			this->members.emplace_back(userid, backend);
	}

	void Group::removeUser(const std::string& userid, Backend backend)
	{
		auto it = std::find_if(this->members.begin(), this->members.end(), [&](const auto& g) -> bool {
			return g.id == userid && g.backend == backend;
		});

		if(it != this->members.end())
			this->members.erase(it);
	}




	void Group::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->id);
		wr.write(this->name);
		wr.write(this->members);
	}

	std::optional<Group> Group::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		Group ret;

		if(!rd.read(&ret.id))
			return { };

		if(!rd.read(&ret.name))
			return { };

		if(!rd.read(&ret.members))
			return { };

		return ret;
	}



	void GenericUser::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->id);
		wr.write((uint64_t) this->backend);
	}

	std::optional<GenericUser> GenericUser::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		GenericUser ret;

		if(!rd.read(&ret.id))
			return { };

		uint64_t tmp = 0;
		if(!rd.read(&tmp))
			return { };

		ret.backend = (Backend) tmp;
		return ret;
	}
}
