//Copyright(C) 2025 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include <filesystem>
#include <fstream>
#include <vector>
#include <sstream>
#include <string>
#include <chrono>
#include <iomanip>
#include <queue>
#include <map>
#include <memory>

#include "core.hpp"
#include "command.hpp"
#include "compression.hpp"

using KalaData::Core::KalaDataCore;
using KalaData::Core::Command;
using KalaData::Core::ForceCloseType;
using KalaData::Compression::Archive;
using KalaData::Compression::Token;
using KalaData::Compression::HuffNode;
using KalaData::Compression::HuffNode32;
using KalaData::Compression::NodeCompare;
using KalaData::Compression::NodeCompare32;

using std::filesystem::path;
using std::filesystem::create_directories;
using std::filesystem::is_regular_file;
using std::filesystem::weakly_canonical;
using std::filesystem::file_size;
using std::filesystem::recursive_directory_iterator;
using std::ofstream;
using std::ifstream;
using std::ios;
using std::streamsize;
using std::vector;
using std::ostringstream;
using std::string;
using std::to_string;
using std::chrono::high_resolution_clock;
using std::chrono::duration;
using std::chrono::seconds;
using std::fixed;
using std::setprecision;
using std::map;
using std::exception;
using std::fill;
using std::memcpy;
using std::priority_queue;
using std::unique_ptr;
using std::make_unique;
using std::move;

//Rebuild raw data from tokens list
static vector<u8> DecompressFromTokens(
	const vector<Token>& tokens,
	size_t originalSize,
	const string& target);

//Unwrap Huffman stream into LZSS tokens
static vector<Token> HuffmanDecodeTokens(
	const vector<u8>& input,
	const string& origin);

//Build canonical Huffman codes (literals or lengths) (1 byte)
static map<u8, u16> CanonicalFromLengths(
	const u8 codeLen[],
	size_t count);

//Build canonical Huffman codes (offsets) (4 bytes)
static map<u32, u16> CanonicalFromLengths32(
	const u8 codeLen[],
	size_t count);

//Derialize a code-length table (literals or lengths) from the input stream (1 byte)
static void ReadCodeLengths(
	const u8*& ptr,
	u8 codeLen[],
	size_t count);

//Deserialize a frequency table (offsets) from the input stream (4 bytes)
static void ReadCodeLengths32(
	const u8*& ptr,
	map<u32, u8>& codeLen);

//Utility class for reading bit-packed Huffman input stream
struct BitReader
{
	const u8* data;
	size_t size;
	size_t pos = 0; //byte index
	int count = 0;  //remaining bits in buffer
	u8 buffer = 0;

	BitReader(
		const u8* d,
		size_t s) :
		data(d),
		size(s) {}

	//read a single bit
	int ReadBit()
	{
		if (count == 0)
		{
			if (pos >= size) return -1; //EOF
			buffer = data[pos++];
			count = 8;
		}

		int bit = (buffer & 0x80) ? 1 : 0; //read MSB first
		buffer <<= 1;
		count--;
		return bit;
	}

	//Read multiple bits into an integer (canonical Huffman)
	u32 ReadBits(int n)
	{
		u32 value = 0;
		for (int i = 0; i < n; i++)
		{
			int b = ReadBit();
			if (b == -1) break; //EOF
			value = (value << 1) | b;
		}
		return value;
	}
	

	bool EndOfStream() const
	{
		return (pos >= size
			&& count == 0);
	}
};

namespace KalaData::Compression
{
	void Archive::Decompress(
		const string& origin,
		const string& target)
	{
		Command::SetCommandAllowState(false);

		KalaDataCore::PrintMessage(
			"Starting to decompress archive '" + origin + "' to folder '" + target + "'!\n",
			"DECOMPRESS",
			LogType::LOG_INFO);

		//start clock timer
		auto start = high_resolution_clock::now();

		ifstream in(origin, ios::binary);
		if (!in.is_open())
		{
			KalaDataCore::ForceCloseByType(
				"Failed to open origin archive '" + origin + "'!\n",
				ForceCloseType::TYPE_DECOMPRESSION);

			return;
		}

		u32 compCount{};
		u32 rawCount{};
		u32 emptyCount{};

		//read magic number
		char magicVer[6]{};
		in.read(magicVer, sizeof(magicVer));

		//check magic
		if (memcmp(magicVer, "KDAT", 4) != 0)
		{
			KalaDataCore::ForceCloseByType(
				"Invalid magic value in archive '" + origin + "'!\n",
				ForceCloseType::TYPE_DECOMPRESSION);

			return;
		}

		//check version range
		int version = 0;

		try
		{
			version = stoi(string(magicVer + 4, 2));
		}
		catch (const exception& e)
		{
			KalaDataCore::ForceCloseByType(
				"Failed to get version from archive '" + origin + "'! Reason: " + e.what() + "\n",
				ForceCloseType::TYPE_DECOMPRESSION);

			return;
		}

		if (version < 1
			|| version > 99)
		{
			KalaDataCore::ForceCloseByType(
				"Out of range version '" + to_string(version) + "' in archive '" + origin + "'!\n",
				ForceCloseType::TYPE_DECOMPRESSION);

			return;
		}

		//skip original incompatible version
		if (version == 1)
		{
			KalaDataCore::ForceCloseByType(
				"Outdated version '01' in archive '" + origin + "' is no longer supported! Use KalaData 0.2 or newer to decompress this '.kdat' archive.\n",
				ForceCloseType::TYPE_DECOMPRESSION);

			return;
		}

		if (KalaDataCore::IsVerboseLoggingEnabled())
		{
			ostringstream ss{};

			ss << "Archive '" + target + "' version is '" + string(magicVer, 6) + "'.\n\n"
				<< "Window size is '" << Archive::GetWindowSize() << "'.\n"
				<< "Lookahead is '" << Archive::GetLookAhead() << "'.\n"
				<< "Min match is '" << MIN_MATCH << "'.\n";

			KalaDataCore::PrintMessage(
				ss.str(),
				"DECOMPRESS",
				LogType::LOG_INFO);
		}

		u32 fileCount{};
		in.read((char*)&fileCount, sizeof(u32));
		if (fileCount > 100000)
		{
			KalaDataCore::ForceCloseByType(
				"Archive '" + origin + "' reports an absurd file count (corrupted?)!\n",
				ForceCloseType::TYPE_DECOMPRESSION);

			return;
		}

		if (fileCount == 0)
		{
			KalaDataCore::ForceCloseByType(
				"Archive '" + origin + "' contains no valid files to decompress!\n",
				ForceCloseType::TYPE_DECOMPRESSION);

			return;
		}

		if (!in.good())
		{
			KalaDataCore::ForceCloseByType(
				"Unexpected EOF while reading header data in archive '" + origin + "'!\n",
				ForceCloseType::TYPE_DECOMPRESSION);

			return;
		}

		for (u32 i = 0; i < fileCount; i++)
		{
			u32 pathLen{};
			in.read((char*)&pathLen, sizeof(u32));

			string relPath(pathLen, '\0');
			in.read(relPath.data(), pathLen);

			u8 method{};
			in.read((char*)&method, sizeof(u8));

			uint64_t originalSize{};
			in.read((char*)&originalSize, sizeof(uint64_t));

			uint64_t storedSize{};
			in.read((char*)&storedSize, sizeof(uint64_t));

			if (!in.good())
			{
				KalaDataCore::ForceCloseByType(
					"Unexpected EOF while reading metadata in archive '" + origin + "'!\n",
					ForceCloseType::TYPE_DECOMPRESSION);

				return;
			}

			if (method == 0)
			{
				if (storedSize != originalSize)
				{
					ostringstream ss{};

					ss << "Stored size '" << storedSize << "' for raw file '" << relPath << "' "
						<< "is not the same as original size '" << originalSize << "' "
						<< "in archive '" << target << "' (corruption suspected)!\n";

					KalaDataCore::ForceCloseByType(
						ss.str(),
						ForceCloseType::TYPE_DECOMPRESSION);

					return;
				}
			}
			else if (method == 1)
			{
				if (storedSize >= originalSize)
				{
					ostringstream ss{};

					ss << "Stored size '" << storedSize << "' for compressed file '" << relPath << "' "
						<< "is the same or bigger than the original size '" << originalSize << "' "
						<< "in archive '" << target << "' (corruption suspected)!\n";

					KalaDataCore::ForceCloseByType(
						ss.str(),
						ForceCloseType::TYPE_DECOMPRESSION);

					return;
				}
			}
			else
			{
				KalaDataCore::ForceCloseByType(
					"Unknown method storage flag '" + to_string(method) + "' in archive '" + origin + "'!\n",
					ForceCloseType::TYPE_DECOMPRESSION);

				return;
			}

			if (originalSize == 0) emptyCount++;
			else if (storedSize < originalSize) compCount++;
			else rawCount++;

			path outPath = path(target) / relPath;
			create_directories(outPath.parent_path());

			//path traversal check
			auto absTarget = weakly_canonical(target);
			auto absOut = weakly_canonical(outPath);

			if (absOut.string().find(absTarget.string()) != 0)
			{
				KalaDataCore::ForceCloseByType(
					"Archive '" + origin + "' contains invalid path '" + relPath + "' (path traveral attempt)!",
					ForceCloseType::TYPE_DECOMPRESSION);

				return;
			}

			//prepare output buffer
			vector<u8> data{};
			auto storedStart = in.tellg();

			//raw: copy exactly storedSize bytes
			if (method == 0)
			{
				if (storedSize == 0
					&& KalaDataCore::IsVerboseLoggingEnabled())
				{
					KalaDataCore::PrintMessage(
						"[EMPTY] '" + path(relPath).filename().string() + "'",
						"");
				}
				else
				{
					if (KalaDataCore::IsVerboseLoggingEnabled())
					{
						ostringstream ss{};

						ss << "[RAW] '" << path(relPath).filename().string()
							<< "' - '" << storedSize << " bytes' "
							<< ">= '" << originalSize << " bytes'";

						KalaDataCore::PrintMessage(
							ss.str(),
							"");
					}

					data.resize(static_cast<size_t>(storedSize));
					if (!in.read((char*)data.data(), static_cast<streamsize>(storedSize)))
					{
						KalaDataCore::ForceCloseByType(
							"Unexpected end of archive while reading raw data for '" + relPath + "' in archive '" + origin + "'!\n",
							ForceCloseType::TYPE_DECOMPRESSION);

						return;
					}
				}
			}
			//LZSS: decompress storedSize to originalSize
			else if (method == 1)
			{
				if (KalaDataCore::IsVerboseLoggingEnabled())
				{
					ostringstream ss{};

					ss << "[DECOMPRESS] '" << path(relPath).filename().string()
						<< "' - '" << storedSize << " bytes' "
						<< "< '" << originalSize << " bytes'";

					KalaDataCore::PrintMessage(
						ss.str(),
						"");
				}

				vector<u8> compressedBytes(storedSize);
				in.read(reinterpret_cast<char*>(compressedBytes.data()), storedSize);

				vector<Token> tokens = HuffmanDecodeTokens(
					compressedBytes,
					origin);

				vector<u8> data = DecompressFromTokens(
					tokens,
					static_cast<size_t>(originalSize),
					origin);
			}

			//sanity check
			if (data.size() != originalSize)
			{
				ostringstream ss{};

				ss << "Decompressed archive file '" << target << "' size '" << data.size()
					<< "does not match original size '" << originalSize << "'!\n";

				KalaDataCore::ForceCloseByType(
					ss.str(),
					ForceCloseType::TYPE_DECOMPRESSION);

				return;
			}

			//write file
			ofstream outFile(outPath, ios::binary);
			outFile.write((char*)data.data(), data.size());
			if (!outFile.good())
			{
				KalaDataCore::ForceCloseByType(
					"Failed to extract file '" + relPath + "' from archive '" + origin + "' into target folder '" + target + "'!\n",
					ForceCloseType::TYPE_DECOMPRESSION);

				return;
			}

			//done writing
			outFile.close();
		}

		//end timer
		auto end = high_resolution_clock::now();
		auto durationSec = duration<f64>(end - start).count();

		uint64_t folderSize{};
		for (auto& p : recursive_directory_iterator(target))
		{
			if (is_regular_file(p)) folderSize += file_size(p);
		}

		auto archiveSize = file_size(origin);
		auto mbps = static_cast<f64>(archiveSize) / (1024.0 * 1024.0) / durationSec;

		auto ratio = (static_cast<f64>(folderSize) / archiveSize) * 100.0;
		auto factor = static_cast<f64>(folderSize) / archiveSize;
		auto saved = 100.0 - ratio;

		ostringstream finishDecomp{};

		if (KalaDataCore::IsVerboseLoggingEnabled())
		{
			finishDecomp
				<< "Finished decompressing archive '" << origin << "' to folder '" << target << "'!\n"
				<< "  - origin archive size: " << archiveSize << " bytes\n"
				<< "  - target folder size: " << folderSize << " bytes\n"
				<< "  - expansion ratio: " << fixed << setprecision(2) << ratio << "%\n"
				<< "  - expansion factor: " << fixed << setprecision(2) << factor << "x\n"
				<< "  - throughput: " << fixed << setprecision(2) << mbps << " MB/s\n"
				<< "  - total files: " << fileCount << "\n"
				<< "  - decompressed: " << compCount << "\n"
				<< "  - unpacked raw: " << rawCount << "\n"
				<< "  - empty: " << emptyCount << "\n"
				<< "  - duration: " << fixed << setprecision(2) << durationSec << " seconds\n";
		}
		else
		{
			finishDecomp
				<< "Finished decompressing archive '" << path(origin).filename().string()
				<< "' to folder '" << path(target).filename().string() << "'!\n"
				<< "  - origin archive size: " << archiveSize << " bytes\n"
				<< "  - target folder size: " << folderSize << " bytes\n"
				<< "  - throughput: " << fixed << setprecision(2) << mbps << " MB/s\n"
				<< "  - duration: " << fixed << setprecision(2) << durationSec << " seconds\n";
		}

		KalaDataCore::PrintMessage(
			finishDecomp.str(),
			"DECOMPRESS",
			LogType::LOG_SUCCESS);

		Command::SetCommandAllowState(true);
	}
}

vector<u8> DecompressFromTokens(
	const vector<Token>& tokens,
	size_t originalSize,
	const string& target)
{
	vector<u8> output{};
	output.reserve(originalSize);

	for (const auto& t : tokens)
	{
		if (t.isLiteral)
		{
			//just push the byte
			output.push_back(t.literal);
		}
		else
		{
			//ensure offset/length are valid
			if (t.offset == 0
				|| t.offset > output.size())
			{
				KalaDataCore::ForceCloseByType(
					"Invalid offset while decompressing file '" + target + "'!\n",
					ForceCloseType::TYPE_DECOMPRESSION_BUFFER);

				return {};
			}

			if (t.length == 0)
			{
				KalaDataCore::ForceCloseByType(
					"Zero-length match while decompressing file '" + target + "'!\n",
					ForceCloseType::TYPE_DECOMPRESSION_BUFFER);

				return {};
			}

			//copy match from already written data

			size_t start = output.size() - t.offset;
			for (size_t i = 0; i < t.length; i++)
			{
				if (output.size() >= originalSize) break; //safety
				output.push_back(output[start + i]);
			}
		}
	}

	if (output.size() != originalSize)
	{
		KalaDataCore::ForceCloseByType(
			"Size mismatch while decompressing file '" + target + "'!\n",
			ForceCloseType::TYPE_DECOMPRESSION_BUFFER);

		return {};
	}

	return output;
}

vector<Token> HuffmanDecodeTokens(
	const vector<u8>& input,
	const string& origin)
{
	vector<Token> tokens{};
	if (input.empty()) return tokens;

	const u8* ptr = input.data();

	//read frequency tables

	size_t litFreq[256]{};
	size_t lenFreq[256]{};
	map<u32, size_t> offFreq{};

	//ReadTable(ptr, litFreq, 256);
	//ReadTable(ptr, lenFreq, 256);
	//ReadTable32(ptr, offFreq);

	//rebuild Huffman trees

	//auto litRoot = BuildTree(litFreq, 256);
	//auto lenRoot = BuildTree(lenFreq, 256);
	//auto offRoot = BuildTree32(offFreq);

	BitReader br(ptr, input.size());

	while (!br.EndOfStream())
	{
		int flag = br.ReadBit();
		if (flag == 1) //literal
		{
			HuffNode* node{};// = litRoot.get();

			while (!node->IsLeaf())
			{
				int bit = br.ReadBit();
				node = (bit == 0) ? node->left.get() : node->right.get();

				if (br.EndOfStream())
				{
					KalaDataCore::ForceCloseByType(
						"Unexpected end of stream while decompressing literal in '" + origin + "'!\n",
						ForceCloseType::TYPE_DECOMPRESSION_BUFFER);

					return {};
				}
			}

			tokens.push_back(
			{
				true,
				node->symbol, //decoded literal directly
				0,
				0
			});
		}
		else //match
		{
			u32 offset = 0;
			{
				HuffNode32* node{};// = litRoot.get();

				while (!node->IsLeaf())
				{
					int bit = br.ReadBit();
					node = (bit == 0) ? node->left.get() : node->right.get();
				}
				offset = node->symbol;
			}

			u8 length = 0;
			{
				HuffNode* node{};// = litRoot.get();

				while (!node->IsLeaf())
				{
					int bit = br.ReadBit();
					node = (bit == 0) ? node->left.get() : node->right.get();
				}
				length = node->symbol;
			}

			tokens.push_back(
			{
				false,
				0,
				offset,
				length
			});
		}
	}

	return tokens;
}

map<u8, u16> CanonicalFromLengths(
	const u8 codeLen[],
	size_t count)
{
	//count number of codes for each length

	u16 bl_count[33]{}; //support lengths up to 32
	for (size_t i = 0; i < count; i++)
	{
		if (codeLen[i] > 0) bl_count[codeLen[i]]++;
	}

	//compute first code for each length

	u16 next_code[33]{};
	u16 code = 0;
	for (u8 bits = 1; bits <= 32; bits++)
	{
		code = (code + bl_count[bits - 1]) << 1;
		next_code[bits] = code;
	}

	//assign codes

	map<u8, u16> table{};
}

map<u32, u16> CanonicalFromLengths32(
	const u8 codeLen[],
	size_t count)
{

}

void ReadCodeLengths(
	const u8*& ptr,
	u8 codeLen[],
	size_t count)
{
	//clear output lengths
	memset(codeLen, 0, count * sizeof(u8));

	//read mode

	u8 node = *ptr++;
	if (node == 1)
	{
		//sparse

		u16 nonZero = 0;
		memcpy(&nonZero, ptr, sizeof(u16));
		ptr += sizeof(u16);

		for (u16 i = 0; i < nonZero; i++)
		{
			u8 symbol = *ptr++;
			u8 len = *ptr++;
			codeLen[symbol] = len;
		}
	}
	else
	{
		//dense
		for (size_t i = 0; i < count; i++)
		{
			codeLen[i] = *ptr++;
		}
	}
}

void ReadCodeLengths32(
	const u8*& ptr,
	map<u32, u8>& codeLen)
{
	codeLen.clear();

	//read number of non-zero code lengths
	u32 nonZero = 0;
	memcpy(&nonZero, ptr, sizeof(u32));
	ptr += sizeof(u32);

	for (u32 i = 0; i < nonZero; i++)
	{
		u32 symbol = 0;
		u8 len = 0;

		memcpy(&symbol, ptr, sizeof(u32));
		ptr += sizeof(u32);

		len = *ptr++;

		if (len > 0) codeLen[symbol] = len;
	}
}