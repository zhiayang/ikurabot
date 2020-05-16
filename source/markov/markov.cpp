// markov.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <thread>

#include "zfu.h"
#include "markov.h"
#include "synchro.h"
#include "serialise.h"

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

	struct MarkovModel
	{
		// map from list of words (current state) to list of possible output words.
		tsl::robin_map<std::vector<uint64_t>, WordList, hash_span<uint64_t>, equal_span<uint64_t>> table;

		// the map from words to indices in the word list.
		ikura::string_map<uint64_t> wordIndices;

		// the list of words. either way we'll need two sets, because we need to go from
		// index -> word and word -> index.
		std::vector<std::string> wordList;

		// how long (at most) the prefix should be. the longer this is, the longer it will
		// take to both train and use the model.
		static constexpr size_t MAX_LENGTH = 5;
	};
}

namespace ikura::markov
{
	static struct {
		std::thread worker;
		wait_queue<std::string> queue;
	} State;

	static Synchronised<MarkovModel> theMarkovModel;
	Synchronised<MarkovModel>& markovModel() { return theMarkovModel; }


	static void process_one(ikura::str_view input);
	static void worker_thread()
	{
		while(true)
		{
			auto input = State.queue.pop();
			if(input.empty())
				break;

			process_one(ikura::str_view(input));
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
		State.queue.push("");
		State.worker.join();
	}

	void process(ikura::str_view input)
	{
		State.queue.push(input.str());
	}

	static bool is_punctuation(char c)
	{
		return zfu::match(c, '.', ',', '!', '?', ';', ':', '(', ')');
	}

	static uint64_t get_word_index(MarkovModel* markov, ikura::str_view word)
	{
		if(auto it = markov->wordIndices.find(word); it != markov->wordIndices.end())
			return it->second;

		auto idx = (uint64_t) markov->wordList.size();
		markov->wordList.push_back(word.str());
		markov->wordIndices[word] = idx;

		return idx;
	}

	static void process_one(ikura::str_view input)
	{
		// first split by words and punctuation. consecutive puncutation is lumped together.
		std::vector<uint64_t> word_arr;
		{
			// fuck it, just use a big lock.
			markovModel().perform_write([&](auto& markov) {
				input = input.trim();
				size_t end = 0;
				while(end < input.size())
				{
					if(auto c = input[end]; c == ' ' || c == '\t')
					{
						word_arr.push_back(get_word_index(&markov, input.take(end)));
						input.remove_prefix(end);

						input = input.trim_front();
						end = 0;
					}
					else if(is_punctuation(c))
					{
						word_arr.push_back(get_word_index(&markov, input.take(end)));
						input.remove_prefix(end);

						input = input.trim_front();
						end = 0;

						while(end < input.size() && is_punctuation(input[end]))
							end++;

						word_arr.push_back(get_word_index(&markov, input.take(end)));
						input.remove_prefix(end);

						input = input.trim_front();
						end = 0;
					}
					else
					{
						end++;
					}
				}

				if(end > 0)
					word_arr.push_back(get_word_index(&markov, input.take(end)));
			});
		}

		// ok, words are now split.
		auto words = ikura::span(word_arr);
		for(size_t i = 0; i + 1 < words.size(); i++)
		{
			for(size_t k = 1; k <= MarkovModel::MAX_LENGTH && i + k < words.size(); k++)
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

	static ikura::str_view generate_one(ikura::span<uint64_t> prefix)
	{
		if(prefix.empty())
			return "";

		prefix = prefix.take_last(random::get<size_t>(1, MarkovModel::MAX_LENGTH));
		return markovModel().map_read([&](auto& markov) -> ikura::str_view {
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
							return ikura::str_view(markov.wordList[word.index]);

						selection -= word.frequency;
					}
				}

				// try a shorter prefix.
				prefix.remove_prefix(1);
			}

			// ran out.
			return "";
		});
	}

	std::string generate(ikura::str_view seed)
	{
		if(seed.empty())
		{
			seed = markovModel().map_read([](auto& markov) -> ikura::str_view {
				if(markov.wordList.empty())
					return "";

				return markov.wordList[random::get<size_t>(0, markov.wordList.size() - 1)];
			});

			if(seed.empty())
			{
				lg::warn("markov", "failed to find seed");
				return "";
			}
		}

		size_t totallen = 0;
		size_t max_length = 25;
		std::vector<ikura::str_view> output;
		std::vector<uint64_t> output_words;

		output.push_back(seed);
		output_words.push_back(get_word_index(markovModel().wlock().get(), seed));

		while(output.size() < max_length)
		{
			auto word = generate_one(ikura::span(output_words));
			if(word.empty())
				break;

			totallen += word.size();
			output.push_back(word);
			output_words.push_back(get_word_index(markovModel().wlock().get(), word));
		}

		std::string ret;
		ret.reserve(totallen);

		for(size_t i = 0; i < output.size(); i++)
		{
			if(i != 0 && (output[i].find_first_of(".,?!") != 0))
				ret += ' ';

			ret += output[i].str();
		}

		return ret;
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

	void MarkovDB::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		markovModel().perform_read([&wr](auto& markov) {
			wr.write(markov.table);
			wr.write(markov.wordIndices);
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

		if(!rd.read(&ret.wordIndices))
			return { };

		if(!rd.read(&ret.wordList))
			return { };

		*markovModel().wlock().get() = ret;

		return MarkovDB();
	}
}
