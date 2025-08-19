//Copyright(C) 2025 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include <queue>

#include "compression.hpp"

using std::priority_queue;
using std::make_unique;

namespace KalaData::Compression
{
	void Archive::BuildCodes(
		HuffNode* node,
		const string& prefix,
		map<uint8_t, string>& codes)
	{
		if (!node->left
			&& !node->right)
		{
			codes[node->symbol] = prefix.empty() ? "0" : prefix;
		}

		if (node->left) BuildCodes(node->left.get(), prefix + "0", codes);
		if (node->right) BuildCodes(node->right.get(), prefix + "1", codes);
	}

	void Archive::BuildCodes32(
		HuffNode32* node,
		const string& prefix,
		map<uint32_t, string>& codes)
	{
		if (!node->left
			&& !node->right)
		{
			codes[node->symbol] = prefix.empty() ? "0" : prefix;
		}

		if (node->left) BuildCodes32(node->left.get(), prefix + "0", codes);
		if (node->right) BuildCodes32(node->right.get(), prefix + "1", codes);
	}

	map<uint8_t, string> Archive::BuildHuffman(
		const size_t freq[],
		size_t count)
	{
		//priority queue
		priority_queue<unique_ptr<HuffNode>, vector<unique_ptr<HuffNode>>, NodeCompare> pq{};

		for (size_t i = 0; i < count; i++)
		{
			if (freq[i] > 0)
			{
				pq.push(make_unique<HuffNode>((uint8_t)i, freq[i]));
			}
		}

		if (pq.empty()) return {}; //no symbols

		if (pq.size() == 1)
		{
			//ensure 2 nodes minimum
			pq.push(make_unique<HuffNode>(0, 1));
		}

		//build tree
		while (pq.size() > 1)
		{
			unique_ptr<HuffNode> left = move(const_cast<unique_ptr<HuffNode>&>(pq.top())); pq.pop();
			unique_ptr<HuffNode> right = move(const_cast<unique_ptr<HuffNode>&>(pq.top())); pq.pop();
			auto merged = make_unique<HuffNode>(move(left), move(right));
			pq.push(move(merged));
		}
		unique_ptr<HuffNode> root = move(const_cast<unique_ptr<HuffNode>&>(pq.top()));

		//build codes reqursively
		map<uint8_t, string> codes{};
		BuildCodes(root.get(), "", codes);
		return codes;
	}

	map<uint32_t, string> Archive::BuildHuffmanMap(
		const map<uint32_t, size_t>& freqMap)
	{
		//priority queue
		priority_queue<unique_ptr<HuffNode32>, vector<unique_ptr<HuffNode32>>, NodeCompare32> pq{};

		for (auto& [sym, f] : freqMap)
		{
			pq.push(make_unique<HuffNode32>(sym, f));
		}

		if (pq.empty()) return{}; //no symbols

		if (pq.size() == 1)
		{
			//ensure 2 nodes minimum
			pq.push(make_unique<HuffNode32>(0, 1));
		}

		//build tree
		while (pq.size() > 1)
		{
			unique_ptr<HuffNode32> left = move(const_cast<unique_ptr<HuffNode32>&>(pq.top())); pq.pop();
			unique_ptr<HuffNode32> right = move(const_cast<unique_ptr<HuffNode32>&>(pq.top())); pq.pop();
			auto merged = make_unique<HuffNode32>(move(left), move(right));
			pq.push(move(merged));
		}
		unique_ptr<HuffNode32> root = move(const_cast<unique_ptr<HuffNode32>&>(pq.top()));

		//build codes reqursively
		map<uint32_t, string> codes{};
		BuildCodes32(root.get(), "", codes);
		return codes;
	}
}