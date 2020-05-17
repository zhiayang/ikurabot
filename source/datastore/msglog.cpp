// msglog.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "serialise.h"

namespace ikura::db
{
	ikura::relative_str MessageDB::logMessageContents(ikura::str_view contents)
	{
		auto idx = this->rawData.size();
		this->rawData.append(contents.data(), contents.size());

		return ikura::relative_str(idx, contents.size());
	}

	void MessageDB::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->rawData);
	}

	std::optional<MessageDB> MessageDB::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		MessageDB ret;
		if(!rd.read(&ret.rawData))
			return { };

		return ret;
	}
}
