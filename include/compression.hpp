//Copyright(C) 2025 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include <string>
#include <algorithm>
#include <memory>
#include <map>
#include <vector>

#include "KalaHeaders/core_types.hpp"
#include "KalaHeaders/logging.hpp"

namespace KalaData::Compression
{
	using KalaHeaders::LogType;

	using std::string;
	using std::clamp;
	using std::unique_ptr;
	using std::map;
	using std::vector;

	struct Token
	{
		bool isLiteral;
		u8 literal;
		u32 offset;
		u8 length;
	};

	//Huffman tree node (1 byte)
	struct HuffNode
	{
		u8 symbol;
		size_t freq;
		unique_ptr<HuffNode> left;
		unique_ptr<HuffNode> right;

		HuffNode(
			u8 s,
			size_t f) :
			symbol(s),
			freq(f),
			left(nullptr),
			right(nullptr) {
		}

		HuffNode(
			unique_ptr<HuffNode> l,
			unique_ptr<HuffNode> r) :
			symbol(0),
			freq(l->freq + r->freq),
			left(move(l)),
			right(move(r)) {
		}

		bool IsLeaf() const { return !left && !right; }
	};

	//Huffman tree node (4 bytes)
	struct HuffNode32
	{
		u32 symbol;
		size_t freq;
		unique_ptr<HuffNode32> left;
		unique_ptr<HuffNode32> right;

		HuffNode32(
			u32 s,
			size_t f) :
			symbol(s),
			freq(f),
			left(nullptr),
			right(nullptr) {
		}

		HuffNode32(
			unique_ptr<HuffNode32> l,
			unique_ptr<HuffNode32> r) :
			symbol(0),
			freq(l->freq + r->freq),
			left(move(l)),
			right(move(r)) {
		}

		bool IsLeaf() const { return !left && !right; }
	};

	struct NodeCompare
	{
		bool operator()(
			const unique_ptr<HuffNode>& a,
			const unique_ptr<HuffNode>& b) const
		{
			return a->freq > b->freq;
		}
	};

	struct NodeCompare32
	{
		bool operator()(
			const unique_ptr<HuffNode32>& a,
			const unique_ptr<HuffNode32>& b) const
		{
			return a->freq > b->freq;
		}
	};

	constexpr size_t WINDOW_SIZE_FASTEST  = static_cast<size_t>(4 * 1024);        //4KB
	constexpr size_t WINDOW_SIZE_FAST     = static_cast<size_t>(32 * 1024);       //32KB
	constexpr size_t WINDOW_SIZE_BALANCED = static_cast<size_t>(256 * 1024);      //256KB
	constexpr size_t WINDOW_SIZE_SLOW     = static_cast<size_t>(1 * 1024) * 1024; //1MB
	constexpr size_t WINDOW_SIZE_ARCHIVE  = static_cast<size_t>(8 * 1024) * 1024; //8MB

	constexpr size_t LOOKAHEAD_FASTEST  = 18;
	constexpr size_t LOOKAHEAD_FAST     = 32;
	constexpr size_t LOOKAHEAD_BALANCED = 64;
	constexpr size_t LOOKAHEAD_SLOW     = 128;
	constexpr size_t LOOKAHEAD_ARCHIVE  = 255;

	constexpr size_t MIN_MATCH = 3;

	class Archive
	{
	public:
		//Assign a new window size value.
		//Supported range 4KB-8MB
		static void SetWindowSize(size_t windowSizeValue)
		{
			if (windowSizeValue % 4 != 0
				|| windowSizeValue < WINDOW_SIZE_FASTEST
				|| windowSizeValue > WINDOW_SIZE_ARCHIVE)
			{
				WINDOW_SIZE = WINDOW_SIZE_FASTEST;
				return;
			}

			WINDOW_SIZE = windowSizeValue;
		};
		static size_t GetWindowSize() { return WINDOW_SIZE; }

		//Assign a new lookahead value.
		//Supported range 18-255
		static void SetLookAhead(size_t lookAheadValue)
		{
			size_t clamped = clamp(
				static_cast<int>(lookAheadValue),
				static_cast<int>(LOOKAHEAD_FASTEST),
				static_cast<int>(LOOKAHEAD_ARCHIVE));

			LOOKAHEAD = clamped;
		};
		static size_t GetLookAhead() { return LOOKAHEAD; }

		//Compresses selected folder straight to .kdat archive inside target folder,
		//skips all safety checks that are handled in the Command class for the Compress command
		static void Compress(
			const string& origin,
			const string& target);

		//Decompresses selected .kdat archive straight to selected target folder,
		//skips all safety checks that are handled in the Command class for the Decompress command
		static void Decompress(
			const string& origin,
			const string& target);

	private:
		//Sliding window
		static inline size_t WINDOW_SIZE = WINDOW_SIZE_FASTEST;

		//Max match length
		static inline size_t LOOKAHEAD = LOOKAHEAD_FASTEST;
	};
}