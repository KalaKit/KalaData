//Copyright(C) 2025 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include <string>
#include <algorithm>

namespace KalaData
{
	using std::string;
	using std::clamp;

	enum class ForceCloseType
	{
		TYPE_COMPRESSION,
		TYPE_DECOMPRESSION,
		TYPE_COMPRESSION_BUFFER,
		TYPE_DECOMPRESSION_BUFFER,
		TYPE_HUFFMAN_ENCODE,
		TYPE_HUFFMAN_DECODE
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

	class Compression
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

		static void ForceClose(
			const string& message,
			ForceCloseType type);

		//Compresses selected folder straight to .kdat archive inside target folder,
		//skips all safety checks that are handled in the Command class for the Compress command
		static void CompressToArchive(
			const string& origin,
			const string& target);

		//Decompresses selected .kdat archive straight to selected target folder,
		//skips all safety checks that are handled in the Command class for the Decompress command
		static void DecompressToFolder(
			const string& origin,
			const string& target);
	private:
		//Sliding window
		static inline size_t WINDOW_SIZE = WINDOW_SIZE_FASTEST;

		//Max match length
		static inline size_t LOOKAHEAD = LOOKAHEAD_FASTEST;
	};
}