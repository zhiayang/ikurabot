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
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		SharedDB ret;

		if(!rd.read(&ret.groups))
			return { };

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
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

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
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

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
