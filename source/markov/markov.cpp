// markov.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <thread>
#include <random>

#include "zfu.h"
#include "markov.h"
#include "synchro.h"
#include "serialise.h"

#include "utf8proc/utf8proc.h"

// fucking nonsense.
namespace {

	template <typename T>
	struct equal_span
	{
		using is_transparent = void;
		bool operator () (const std::vector<T>& a, ikura::span<T> b) const        { return b == a; }
		bool operator () (ikura::span<T> a, const std::vector<T>& b) const        { return a == b; }
		bool operator () (const std::vector<T>& a, const std::vector<T>& b) const { return a == b; }
	};

	template <typename T>
	struct hash_span
	{
		size_t operator () (const std::vector<T>& x) const { return std::hash<std::vector<T>>()(x); }
		size_t operator () (ikura::span<T> x) const        { return std::hash<ikura::span<T>>()(x); }
	};
}

namespace ikura::markov
{
	struct Word : Serialisable
	{
		Word() { }
		Word(uint64_t w, uint64_t freq) : index(w), frequency(freq) { }

		uint64_t index = 0;
		uint64_t frequency = 0;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<Word> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_MARKOV_WORD;
	};

	struct WordList : Serialisable
	{
		uint64_t totalFrequency = 0;
		std::vector<Word> words;

		// map from the global wordlist index, to the index in the words array.
		tsl::robin_map<uint64_t, uint64_t> globalIndexMap;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<WordList> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_MARKOV_WORD_LIST;
	};

	struct DBWord : Serialisable
	{
		DBWord() { }
		DBWord(ikura::str_view sv) : word(sv.str()) { }
		DBWord(ikura::str_view sv, uint64_t f) : word(sv.str()), flags(f) { }

		std::string word;   // TODO: find some way to make this a relative_str
		uint64_t flags = 0;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<DBWord> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_MARKOV_STORED_WORD;
	};

	struct MarkovModel
	{
		// map from list of words (current state) to list of possible output words.
		tsl::robin_map<std::vector<uint64_t>, WordList, hash_span<uint64_t>, equal_span<uint64_t>> table;

		// the map from words to indices in the word list.
		ikura::string_map<uint64_t> wordIndices;

		// the list of words. either way we'll need two sets, because we need to go from
		// index -> word and word -> index.
		std::vector<DBWord> wordList;
	};

	static constexpr size_t MIN_INPUT_LENGTH        = 2;
	static constexpr size_t GOOD_INPUT_LENGTH       = 6;
	static constexpr size_t DISCARD_CHANCE_PERCENT  = 60;

	static constexpr size_t MAX_PREFIX_LENGTH       = 6;

	static constexpr size_t IDX_START_MARKER        = 0;
	static constexpr size_t IDX_END_MARKER          = 1;

	static constexpr uint64_t WORD_FLAG_EMOTE           = 0x1;
	static constexpr uint64_t WORD_FLAG_SENTENCE_START  = 0x2;
	static constexpr uint64_t WORD_FLAG_SENTENCE_END    = 0x4;
}

namespace ikura::markov
{
	static struct {
		std::thread worker;
		wait_queue<std::pair<std::string, std::vector<ikura::relative_str>>> queue;
	} State;

	static Synchronised<MarkovModel> theMarkovModel;
	Synchronised<MarkovModel>& markovModel() { return theMarkovModel; }


	static void process_one(ikura::str_view input, std::vector<ikura::relative_str> emote_idxs);
	static void worker_thread()
	{
		while(true)
		{
			auto input = State.queue.pop();
			if(input.first.empty())
				break;

			process_one(ikura::str_view(input.first), std::move(input.second));
		}

		lg::log("markov", "worker thread exited");
	}

	void init()
	{
		State.worker = std::thread(worker_thread);
	}

	void shutdown()
	{
		// push an empty string to terminate.
		State.queue.emplace(std::string(), std::vector<ikura::relative_str>());
		State.worker.join();
	}

	void process(ikura::str_view input, const std::vector<ikura::relative_str>& emote_idxs)
	{
		State.queue.emplace(input.str(), emote_idxs);
	}

	static bool should_split(char c)
	{
		return zfu::match(c, '.', ',', '!', '?');
	}

	static uint64_t get_word_index(MarkovModel* markov, ikura::str_view sv, bool is_emote)
	{
		// this is a little hacky, but... we know that words cannot contain spaces (because we
		// always strip them) -- so, we mark emotes by a leading space. this way, they won't
		// show up in the wordIndices table like normal words.
		auto word = sv.str();
		if(is_emote)
			word = " " + word;

		if(auto it = markov->wordIndices.find(word); it != markov->wordIndices.end())
			return it->second;

		auto idx = (uint64_t) markov->wordList.size();

		// use the original (without-space) thing here, and flag appropriately.
		markov->wordList.emplace_back(sv.str(), is_emote ? WORD_FLAG_EMOTE : 0);

		// but for the wordIndices, use the spaced one.
		markov->wordIndices[word] = idx;

		return idx;
	}

	static size_t is_ignored_sequence(ikura::str_view str)
	{
		return unicode::is_category(str, {
			UTF8PROC_CATEGORY_CN, UTF8PROC_CATEGORY_MN, UTF8PROC_CATEGORY_MC, UTF8PROC_CATEGORY_ME, UTF8PROC_CATEGORY_ZL,
			UTF8PROC_CATEGORY_ZP, UTF8PROC_CATEGORY_CC, UTF8PROC_CATEGORY_CF, UTF8PROC_CATEGORY_CS, UTF8PROC_CATEGORY_CO,
			UTF8PROC_CATEGORY_SO
		});
	}

	static void process_one(ikura::str_view input, std::vector<ikura::relative_str> _emote_idxs)
	{
		if(input.empty())
			return;

		auto emote_idxs = ikura::span(_emote_idxs);

		// first split by words and punctuation. consecutive puncutation is lumped together.
		std::vector<std::pair<ikura::str_view, bool>> word_arr;
		{
			assert(input[0] != ' ' && input[0] != '\t');

			size_t end = 0;
			size_t cur_idx = 0;
			bool is_emote = false;

			auto advance = [&]() {
				input.remove_prefix(end);

				auto tmp = input;
				input = input.trim_front();
				cur_idx += (tmp.size() - input.size());
				end = 0;
			};

			while(end < input.size())
			{
				if(auto c = input[end]; !is_emote && (c == ' ' || c == '\t'))
				{
					word_arr.emplace_back(input.take(end), false);
					advance();
				}
				// this weird condition is to not split constructs like "a?b" and "a.b.c", to handle URLs properly.
				else if(should_split(c) && ((c != '.' && c != '?') || (end + 1 == input.size() || input[end + 1] == ' ')))
				{
					word_arr.emplace_back(input.take(end), false);
					advance();

					while(end < input.size() && should_split(input[end]))
						end++, cur_idx++;

					word_arr.emplace_back(input.take(end), false);
					advance();
				}
				else if(auto k = is_ignored_sequence(input); k > 0)
				{
					input.remove_prefix(k);
					cur_idx += k;
				}
				else
				{
					if(emote_idxs.size() > 0)
					{
						if(is_emote && emote_idxs[0].end_excl() == cur_idx)
						{
							emote_idxs.remove_prefix(1);

							// forcefully add a word
							word_arr.emplace_back(input.take(end), true);
							advance();

							is_emote = false;
							continue;
						}
						else
						{
							if(emote_idxs[0].start() == cur_idx)
								is_emote = true;

							else if(!is_emote && emote_idxs[0].start() < cur_idx)
								emote_idxs.remove_prefix(1);
						}
					}

					end++;
					cur_idx++;
				}
			}

			if(end > 0)
			{
				word_arr.emplace_back(input.take(end), is_emote);
			}
		}


		// filter out most of the shorter responses.
		if(word_arr.size() < MIN_INPUT_LENGTH)
			return;

		else if(word_arr.size() < GOOD_INPUT_LENGTH)
		{
			auto x = random::get<uint64_t>(0, 100);
			if(x <= DISCARD_CHANCE_PERCENT)
				return;
		}



		auto word_indices = markovModel().map_write([&](auto& markov) -> std::vector<uint64_t> {
			std::vector<uint64_t> ret;
			ret.push_back(IDX_START_MARKER);

			for(const auto& [ w, e ] : word_arr)
				ret.push_back(get_word_index(&markov, w, e));

			ret.push_back(IDX_END_MARKER);
			return ret;
		});

		// ok, words are now split.
		auto words = ikura::span(word_indices);
		for(size_t i = 0; i + 1 < words.size(); i++)
		{
			for(size_t k = 1; k <= MAX_PREFIX_LENGTH && i + k < words.size(); k++)
			{
				// TODO: might want to make this case insensitive?
				auto the_word = words[i + k];
				auto prefix = words.drop(i).take(k);

				// just take a big lock...
				markovModel().perform_write([&](auto& markov) -> decltype(auto) {

					WordList* wordlist = nullptr;

					if(auto it = markov.table.find(prefix); it == markov.table.end())
						wordlist = &markov.table.emplace(prefix.vec(), WordList()).first.value();

					else
						wordlist = &it.value();

					wordlist->totalFrequency += 1;
					if(auto it = wordlist->globalIndexMap.find(the_word); it != wordlist->globalIndexMap.end())
					{
						wordlist->words[it->second].frequency++;
					}
					else
					{
						auto idx = wordlist->words.size();
						wordlist->words.emplace_back(the_word, 1);
						wordlist->globalIndexMap.emplace(the_word, idx);
					}
				});
			}
		}
	}

	template <typename T> struct rd_state_t { rd_state_t() : mersenne(std::random_device()()) { } std::mt19937 mersenne; };
	template <typename T> static rd_state_t<T> rd_state;

	static uint64_t generate_one(ikura::span<uint64_t> prefix)
	{
		if(prefix.empty())
			return IDX_END_MARKER;

		// auto prb = std::uniform_real_distribution<>(0.2, 0.57)(rd_state<double>.mersenne);
		auto pfl = (size_t) (1); // + prb * (MAX_PREFIX_LENGTH - 1));
		prefix = prefix.take_last(pfl);

		// lg::log("markov", "prefix len = %.3f / %zu", prb, pfl);

		return markovModel().map_read([&](auto& markov) -> uint64_t {
			while(!prefix.empty())
			{
				// get the frequency
				if(auto it = markov.table.find(prefix); it != markov.table.end())
				{
					WordList& wl = it.value();
					auto selection = random::get<size_t>(0, wl.totalFrequency - 1);

					// find the word.
					for(const auto& word : wl.words)
					{
						if(word.frequency > selection)
							return word.index;

						selection -= word.frequency;
					}
				}

				// try a shorter prefix.
				prefix.remove_prefix(1);
			}

			// ran out.
			return IDX_END_MARKER;
		});
	}

	Message generateMessage()
	{
		size_t max_length = 50;

		std::vector<uint64_t> output;
		output.push_back(IDX_START_MARKER);

		while(output.size() < max_length)
		{
			auto word = generate_one(ikura::span(output));
			if(word == IDX_END_MARKER)
				break;

			output.push_back(word);
		}

		Message msg;

		markovModel().perform_read([&output, &msg](auto& markov) {
			for(size_t i = 0; i < output.size(); i++)
			{
				auto& [ word, em ] = markov.wordList[output[i]];
				if(word.empty())
					continue;

				if(em)
				{
					msg.add(Emote(word));
				}
				else
				{
					if(word.find_first_of(".,?!") == 0)
						msg.addNoSpace(word);

					else
						msg.add(word);
				}
			}
		});

		return msg;
	}







	void Word::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->index);
		wr.write(this->frequency);
	}

	std::optional<Word> Word::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		Word ret;
		if(!rd.read(&ret.index))
			return { };

		if(!rd.read(&ret.frequency))
			return { };

		return ret;
	}
	void WordList::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->totalFrequency);
		wr.write(this->words);
		wr.write(this->globalIndexMap);
	}

	std::optional<WordList> WordList::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		WordList ret;
		if(!rd.read(&ret.totalFrequency))
			return { };

		if(!rd.read(&ret.words))
			return { };

		if(!rd.read(&ret.globalIndexMap))
			return { };

		return ret;
	}

	void DBWord::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->word);
		wr.write(this->flags);
	}

	std::optional<DBWord> DBWord::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		DBWord ret;
		if(!rd.read(&ret.word))
			return { };

		if(!rd.read(&ret.flags))
			return { };

		return ret;
	}

	void MarkovDB::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		markovModel().perform_read([&wr](auto& markov) {
			wr.write(markov.table);
			wr.write(markov.wordList);
		});
	}

	std::optional<MarkovDB> MarkovDB::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		MarkovModel ret;
		if(!rd.read(&ret.table))
			return { };

		if(!rd.read(&ret.wordList))
			return { };

		// seed the thing -- these two are always first. but of course, only if
		// the existing list is totally empty.
		if(ret.wordList.empty())
		{
			ret.wordList.emplace_back("", WORD_FLAG_SENTENCE_START);
			ret.wordList.emplace_back("", WORD_FLAG_SENTENCE_END);
		}

		*markovModel().wlock().get() = ret;

		// populate the wordIndices table, instead of reading from disk, because that's dumb
		// and we end up storing each word twice.
		markovModel().perform_write([](auto& markov) {
			for(size_t i = IDX_END_MARKER + 1; i < markov.wordList.size(); i++)
				markov.wordIndices[markov.wordList[i].word] = i;
		});

		return MarkovDB();
	}
}
