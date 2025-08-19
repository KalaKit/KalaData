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
#include "compress.hpp"

using KalaData::Core;
using KalaData::MessageType;
using KalaData::Compress;

using std::filesystem::path;
using std::filesystem::create_directories;
using std::filesystem::relative;
using std::filesystem::is_regular_file;
using std::filesystem::weakly_canonical;
using std::filesystem::file_size;
using std::filesystem::recursive_directory_iterator;
using std::ofstream;
using std::ifstream;
using std::ios;
using std::streamoff;
using std::streamsize;
using std::istreambuf_iterator;
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
using std::priority_queue;
using std::unique_ptr;
using std::move;
using std::make_unique;
using std::memcmp;

constexpr size_t MIN_MATCH = 3;

enum class ForceCloseType
{
	TYPE_COMPRESSION,
	TYPE_DECOMPRESSION,
	TYPE_COMPRESSION_BUFFER,
	TYPE_DECOMPRESSION_BUFFER,
	TYPE_HUFFMAN_ENCODE,
	TYPE_HUFFMAN_DECODE
};

struct Token
{
	bool isLiteral;
	uint8_t literal;
	uint32_t offset;
	uint8_t length;
};

//Huffman tree node
struct HuffNode
{
	uint8_t symbol;
	size_t freq;
	unique_ptr<HuffNode> left;
	unique_ptr<HuffNode> right;

	HuffNode(
		uint8_t s, 
		size_t f) :
		symbol(s),
		freq(f),
		left(nullptr),
		right(nullptr) {}

	HuffNode(
		unique_ptr<HuffNode> l,
		unique_ptr<HuffNode> r) :
		symbol(0),
		freq(l->freq + r->freq),
		left(move(l)),
		right(move(r)) {}

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

static void ForceClose(
	const string& message,
	ForceCloseType type);

//Compress a single buffer into an already open stream
static vector<uint8_t> CompressBuffer(
	const vector<uint8_t>& input,
	const string& origin);

//Decompress from an already open stream into a buffer
static void DecompressBuffer(
	const vector<uint8_t>& lzssStream,
	vector<uint8_t>& out,
	size_t originalSize,
	const string& target);

//Recursively assign codes
static void BuildCodes(
	HuffNode* node,
	const string& prefix,
	map<uint8_t, string>& codes);

//Post-LZSS filter
static vector<uint8_t> HuffmanEncode(
	const vector<uint8_t>& input,
	const string& origin);

//Pre-LSZZ filter
static vector<uint8_t> HuffmanDecode(
	ifstream& in,
	size_t storedSize,
	const string& origin);

namespace KalaData
{
	void Compress::CompressToArchive(
		const string& origin,
		const string& target)
	{
		Command::SetCommandAllowState(false);

		Core::PrintMessage(
			"Starting to compress folder '" + origin + "' to archive '" + target + "'!\n");

		//start clock timer
		auto start = high_resolution_clock::now();

		ofstream out(target, ios::binary);
		if (!out.is_open())
		{
			ForceClose(
				"Failed to open target archive '" + target + "'!\n",
				ForceCloseType::TYPE_COMPRESSION);

			return;
		}

		//collect all files
		vector<path> files{};
		for (auto& p : recursive_directory_iterator(origin))
		{
			if (is_regular_file(p)) files.push_back(p.path());
		}

		if (files.empty())
		{
			ForceClose(
				"Origin folder '" + origin + "' contains no valid files to compress!\n",
				ForceCloseType::TYPE_COMPRESSION);

			return;
		}

		uint32_t compCount{};
		uint32_t rawCount{};
		uint32_t emptyCount{};

		const char magicVer[6] = { 'K', 'D', 'A', 'T', KALADATA_VERSION[9], KALADATA_VERSION[11] };
		out.write(magicVer, sizeof(magicVer));

		if (Core::IsVerboseLoggingEnabled())
		{
			ostringstream ss{};

			ss << "Window size is '" << WINDOW_SIZE << "'.\n"
				<< "Lookahead is '" << LOOKAHEAD << "'.\n"
				<< "Min match is '" << MIN_MATCH << "'.\n\n"
				<< "Archive '" + target + "' version will be '" + string(magicVer, 6) + "'.\n";

			Core::PrintMessage(ss.str());
		}

		uint32_t fileCount = (uint32_t)files.size();
		out.write((char*)&fileCount, sizeof(uint32_t));

		if (!out.good())
		{
			ForceClose(
				"Failed to write file header data while building archive '" + target + "'!\n",
				ForceCloseType::TYPE_COMPRESSION);

			return;
		}

		for (auto& file : files)
		{
			//relative path
			string relPath = relative(file, origin).string();
			uint32_t pathLen = (uint32_t)relPath.size();

			//read file into memory
			ifstream in(file, ios::binary);
			vector<uint8_t> raw((istreambuf_iterator<char>(in)), {});
			in.close();

			//compress directly into memory
			vector<uint8_t> lszzData = CompressBuffer(raw, relPath);

			//wrap LZSS output with Huffman
			vector<uint8_t> compData = HuffmanEncode(lszzData, origin);

			uint64_t originalSize = raw.size();
			uint64_t compressedSize = compData.size();

			//safeguard: if compression is bigger or equal than original then store raw instead
			bool useCompressed = compressedSize < originalSize;
			const vector<uint8_t>& finalData = useCompressed ? compData : raw;
			uint64_t finalSize = useCompressed ? compressedSize : originalSize;

			uint8_t method = useCompressed ? 1 : 0; //1 - LZSS, 0 = raw

			if (!useCompressed)
			{
				if (originalSize == 0)
				{
					emptyCount++;

					if (Core::IsVerboseLoggingEnabled())
					{
						Core::PrintMessage(
							"[EMPTY] '" + path(relPath).filename().string() + "'");
					}
				}
				else
				{
					rawCount++;

					if (Core::IsVerboseLoggingEnabled())
					{
						ostringstream ss{};

						ss << "[RAW] '" << path(relPath).filename().string()
							<< "' - '" << compressedSize << " bytes' "
							<< ">= '" << originalSize << " bytes'";

						Core::PrintMessage(ss.str());
					}
				}
			}
			else
			{
				compCount++;

				if (Core::IsVerboseLoggingEnabled())
				{
					ostringstream ss{};

					ss << "[COMPRESS] '" << path(relPath).filename().string()
						<< "' - '" << compressedSize << " bytes' "
						<< "< '" << originalSize << " bytes'";

					Core::PrintMessage(ss.str());
				}
			}

			//write metadata
			out.write((char*)&pathLen, sizeof(uint32_t));
			out.write(relPath.data(), pathLen);
			out.write((char*)&method, sizeof(uint8_t));
			out.write((char*)&originalSize, sizeof(uint64_t));
			out.write((char*)&finalSize, sizeof(uint64_t));

			if (!out.good())
			{
				ForceClose(
					"Failed to write metadata for file '" + relPath + "' while building archive '" + target + "'!\n",
					ForceCloseType::TYPE_COMPRESSION);

				return;
			}

			//write compressed data if it is more than 0 bytes
			if (finalSize > 0)
			{
				out.write((char*)finalData.data(), finalData.size());
				if (!out.good())
				{
					ForceClose(
						"Failed to write final data for file '" + relPath + "' while building archive '" + target + "'!\n",
						ForceCloseType::TYPE_COMPRESSION);

					return;
				}
			}
		}

		//finished writing
		out.close();

		//end timer
		auto end = high_resolution_clock::now();
		auto durationSec = duration<double>(end - start).count();

		uint64_t folderSize{};
		for (auto& p : recursive_directory_iterator(origin))
		{
			if (is_regular_file(p)) folderSize += file_size(p);
		}

		auto archiveSize = file_size(target);
		auto mbps = static_cast<double>(folderSize) / (1024.0 * 1024.0) / durationSec;

		auto ratio = (static_cast<double>(archiveSize) / folderSize) * 100.0;
		auto factor = static_cast<double>(folderSize) / archiveSize;
		auto saved = 100.0 - ratio;

		ostringstream finishComp{};

		if (Core::IsVerboseLoggingEnabled())
		{
			finishComp 
				<< "Finished compressing folder '" << origin << "' to archive '" << target << "'!\n"
				<< "  - origin folder size: " << folderSize << " bytes\n"
				<< "  - target archive size: " << archiveSize << " bytes\n"
				<< "  - compression ratio: " << fixed << setprecision(2) << ratio << "%\n"
				<< "  - space saved: " << fixed << setprecision(2) << saved << "%\n"
				<< "  - compression factor: " << fixed << setprecision(2) << factor << "x\n"
				<< "  - throughput: " << fixed << setprecision(2) << mbps << " MB/s\n"
				<< "  - total files: " << fileCount << "\n"
				<< "  - compressed: " << compCount << "\n"
				<< "  - stored raw: " << rawCount << "\n"
				<< "  - empty: " << emptyCount << "\n"
				<< "  - duration: " << fixed << setprecision(2) << durationSec << " seconds\n";
		}
		else
		{
			finishComp
				<< "Finished compressing folder '" << path(origin).filename().string()
				<< "' to archive '" << path(target).filename().string() << "'!\n"
				<< "  - origin folder size: " << folderSize << " bytes\n"
				<< "  - target archive size: " << archiveSize << " bytes\n"
				<< "  - space saved: " << fixed << setprecision(2) << saved << "%\n"
				<< "  - throughput: " << fixed << setprecision(2) << mbps << " MB/s\n"
				<< "  - duration: " << fixed << setprecision(2) << durationSec << " seconds\n";
		}

		Core::PrintMessage(
			finishComp.str(),
			MessageType::MESSAGETYPE_SUCCESS);

		Command::SetCommandAllowState(true);
	}

	void Compress::DecompressToFolder(
		const string& origin,
		const string& target)
	{
		Command::SetCommandAllowState(false);

		Core::PrintMessage(
			"Starting to decompress archive '" + origin + "' to folder '" + target + "'!\n");

		//start clock timer
		auto start = high_resolution_clock::now();

		ifstream in(origin, ios::binary);
		if (!in.is_open())
		{
			ForceClose(
				"Failed to open origin archive '" + origin + "'!\n",
				ForceCloseType::TYPE_DECOMPRESSION);

			return;
		}

		uint32_t compCount{};
		uint32_t rawCount{};
		uint32_t emptyCount{};

		//read magic number
		char magicVer[6]{};
		in.read(magicVer, sizeof(magicVer));

		//check magic
		if (memcmp(magicVer, "KDAT", 4) != 0)
		{
			ForceClose(
				"Invalid magic value in archive '" + origin + "'!\n",
				ForceCloseType::TYPE_DECOMPRESSION);

			return;
		}

		//check version range
		int version = stoi(string(magicVer + 4, 2));
		if (version < 1
			|| version > 99)
		{
			ForceClose(
				"Out of range version '" + to_string(version) + "' in archive '" + origin + "'!\n",
				ForceCloseType::TYPE_DECOMPRESSION);

			return;
		}

		//skip original incompatible version
		if (version == 1)
		{
			ForceClose(
				"Outdated version '0.1' in archive '" + origin + "' is no longer supported! Use KalaData 0.2 or newer to decompress this '.kdat' archive.\n",
				ForceCloseType::TYPE_DECOMPRESSION);

			return;
		}

		if (Core::IsVerboseLoggingEnabled())
		{
			ostringstream ss{};

			ss << "Window size is '" << WINDOW_SIZE << "'.\n"
				<< "Lookahead is '" << LOOKAHEAD << "'.\n"
				<< "Min match is '" << MIN_MATCH << "'.\n\n"
				<< "Archive '" + target + "' version is '" + string(magicVer, 6) + "'.\n";

			Core::PrintMessage(ss.str());
		}

		uint32_t fileCount{};
		in.read((char*)&fileCount, sizeof(uint32_t));
		if (fileCount > 100000)
		{
			ForceClose(
				"Archive '" + origin + "' reports an absurd file count (corrupted?)!\n",
				ForceCloseType::TYPE_DECOMPRESSION);

			return;
		}

		if (fileCount == 0)
		{
			ForceClose(
				"Archive '" + origin + "' contains no valid files to decompress!\n",
				ForceCloseType::TYPE_DECOMPRESSION);

			return;
		}

		if (!in.good())
		{
			ForceClose(
				"Unexpected EOF while reading header data in archive '" + origin + "'!\n",
				ForceCloseType::TYPE_DECOMPRESSION);

			return;
		}

		for (uint32_t i = 0; i < fileCount; i++)
		{
			uint32_t pathLen{};
			in.read((char*)&pathLen, sizeof(uint32_t));

			string relPath(pathLen, '\0');
			in.read(relPath.data(), pathLen);

			uint8_t method{};
			in.read((char*)&method, sizeof(uint8_t));

			uint64_t originalSize{};
			in.read((char*)&originalSize, sizeof(uint64_t));

			uint64_t storedSize{};
			in.read((char*)&storedSize, sizeof(uint64_t));

			if (!in.good())
			{
				ForceClose(
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

					ForceClose(
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

					ForceClose(
						ss.str(),
						ForceCloseType::TYPE_DECOMPRESSION);

					return;
				}
			}
			else
			{
				ForceClose(
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
				ForceClose(
					"Archive '" + origin + "' contains invalid path '" + relPath + "' (path traveral attempt)!",
					ForceCloseType::TYPE_DECOMPRESSION);

				return;
			}

			//prepare output buffer
			vector<uint8_t> data{};
			auto storedStart = in.tellg();

			//raw: copy exactly storedSize bytes
			if (method == 0)
			{
				if (storedSize == 0
					&& Core::IsVerboseLoggingEnabled())
				{
					Core::PrintMessage(
						"[EMPTY] '" + path(relPath).filename().string() + "'");
				}
				else
				{
					if (Core::IsVerboseLoggingEnabled())
					{
						ostringstream ss{};

						ss << "[RAW] '" << path(relPath).filename().string()
							<< "' - '" << storedSize << " bytes' "
							<< ">= '" << originalSize << " bytes'";

						Core::PrintMessage(ss.str());
					}

					data.resize(static_cast<size_t>(storedSize));
					if (!in.read((char*)data.data(), static_cast<streamsize>(storedSize)))
					{
						ForceClose(
							"Unexpected end of archive while reading raw data for '" + relPath + "' in archive '" + origin + "'!\n",
							ForceCloseType::TYPE_DECOMPRESSION);

						return;
					}
				}
			}
			//LZSS: decompress storedSize to originalSize
			else if (method == 1)
			{
				if (Core::IsVerboseLoggingEnabled())
				{
					ostringstream ss{};

					ss << "[DECOMPRESS] '" << path(relPath).filename().string()
						<< "' - '" << storedSize << " bytes' "
						<< "< '" << originalSize << " bytes'";

					Core::PrintMessage(ss.str());
				}

				vector<uint8_t> lzssStream = HuffmanDecode(
					in,
					static_cast<size_t>(storedSize),
					origin);

				//decompress
				DecompressBuffer(
					lzssStream,
					data,
					static_cast<size_t>(originalSize),
					origin);
			}

			//sanity check
			if (data.size() != originalSize)
			{
				ostringstream ss{};

				ss << "Decompressed archive file '" << target << "' size '" << data.size()
					<< "does not match original size '" << originalSize << "'!\n";

				ForceClose(
					ss.str(),
					ForceCloseType::TYPE_DECOMPRESSION);

				return;
			}

			//write file
			ofstream outFile(outPath, ios::binary);
			outFile.write((char*)data.data(), data.size());
			if (!outFile.good())
			{
				ForceClose(
					"Failed to extract file '" + relPath + "' from archive '" + origin + "' into target folder '" + target + "'!\n",
					ForceCloseType::TYPE_DECOMPRESSION);
				return;
			}

			//done writing
			outFile.close();
		}

		//end timer
		auto end = high_resolution_clock::now();
		auto durationSec = duration<double>(end - start).count();

		uint64_t folderSize{};
		for (auto& p : recursive_directory_iterator(target))
		{
			if (is_regular_file(p)) folderSize += file_size(p);
		}

		auto archiveSize = file_size(origin);
		auto mbps = static_cast<double>(archiveSize) / (1024.0 * 1024.0) / durationSec;

		auto ratio = (static_cast<double>(folderSize) / archiveSize) * 100.0;
		auto factor = static_cast<double>(folderSize) / archiveSize;
		auto saved = 100.0 - ratio;

		ostringstream finishDecomp{};

		if (Core::IsVerboseLoggingEnabled())
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

		Core::PrintMessage(
			finishDecomp.str(),
			MessageType::MESSAGETYPE_SUCCESS);

		Command::SetCommandAllowState(true);
	}
}

void ForceClose(
	const string& message,
	ForceCloseType type)
{
	string title{};

	switch (type)
	{
	case ForceCloseType::TYPE_COMPRESSION:
		title = "Compression error";
		break;
	case ForceCloseType::TYPE_DECOMPRESSION:
		title = "Decompression error";
		break;
	case ForceCloseType::TYPE_COMPRESSION_BUFFER:
		title = "Compression buffer error";
		break;
	case ForceCloseType::TYPE_DECOMPRESSION_BUFFER:
		title = "Decompression buffer error";
		break;
	case ForceCloseType::TYPE_HUFFMAN_ENCODE:
		title = "Huffman encode error";
		break;
	case ForceCloseType::TYPE_HUFFMAN_DECODE:
		title = "Huffman decode error";
		break;
	}

	Core::ForceClose(title, message);
}

vector<uint8_t> CompressBuffer(
	const vector<uint8_t>& input,
	const string& origin)
{
	size_t windowSize = Compress::GetWindowSize();
	size_t lookAhead = Compress::GetLookAhead();

	vector<uint8_t> output{};

	if (input.empty()) return output;

	size_t pos = 0;

	while (pos < input.size())
	{
		size_t bestLength = 0;
		size_t bestOffset = 0;
		size_t start = (pos > windowSize) ? (pos - windowSize) : 0;

		//search backwards in window
		for (size_t i = start; i < pos; i++)
		{
			size_t length = 0;

			while (length < lookAhead
				&& pos + length < input.size()
				&& input[i + length] == input[pos + length])
			{
				length++;
			}

			if (length > bestLength
				&& length >= MIN_MATCH)
			{
				bestLength = length;
				bestOffset = pos - i;
			}
		}

		if (bestLength >= MIN_MATCH)
		{
			if (bestOffset >= UINT32_MAX)
			{
				ForceClose(
					"Offset too large for file '" + origin + "' during compressing (data window exceeded)!\n",
					ForceCloseType::TYPE_COMPRESSION_BUFFER);

				return {};
			}

			uint8_t flag = 0;
			output.push_back(flag);

			uint32_t offset = (uint32_t)bestOffset;
			output.insert(output.end(),
				reinterpret_cast<uint8_t*>(&offset),
				reinterpret_cast<uint8_t*>(&offset) + sizeof(uint32_t));

			if (bestLength > UINT8_MAX)
			{
				ForceClose(
					"Match length too large for file '" + origin + "' during compressing (overflow)!\n",
					ForceCloseType::TYPE_COMPRESSION_BUFFER);

				return {};
			}

			uint8_t len8 = (uint8_t)bestLength;
			output.push_back(len8);

			pos += bestLength;
		}
		else
		{
			uint8_t flag = 1;
			output.push_back(flag);
			output.push_back(input[pos]);
			pos++;
		}
	}

	if (output.empty())
	{
		ForceClose(
			"Compression produced empty output for file '" + origin + "' (unexpected)!\n",
			ForceCloseType::TYPE_COMPRESSION_BUFFER);
	}

	return output;
}

void DecompressBuffer(
	const vector<uint8_t>& lzssStream,
	vector<uint8_t>& out,
	size_t originalSize,
	const string& target)
{
	//skip decompressing empty file
	if (originalSize == 0)
	{
		out.clear();
		return;
	}

	vector<uint8_t> buffer{};
	buffer.reserve(originalSize);

	size_t pos = 0;

	while (pos < lzssStream.size())
	{
		uint8_t flag = lzssStream[pos++];

		if (flag == 1) //literal
		{
			if (pos >= lzssStream.size())
			{
				ForceClose(
					"Unexpected end of LZSS stream while reading literal in '" + target + "'!\n",
					ForceCloseType::TYPE_DECOMPRESSION_BUFFER);

				return;
			}

			uint8_t c = lzssStream[pos++];
			buffer.push_back(c);
		}
		else //reference
		{
			if (pos + sizeof(uint16_t) + sizeof(uint8_t) > lzssStream.size())
			{
				ForceClose(
					"Unexpected end of LZSS stream while reading reference in '" + target + "'!\n",
					ForceCloseType::TYPE_DECOMPRESSION_BUFFER);

				return;
			}

			uint32_t offset = *reinterpret_cast<const uint32_t*>(&lzssStream[pos]);
			pos += sizeof(uint32_t);

			uint8_t length = lzssStream[pos++];

			if (offset == 0)
			{
				ostringstream ss{};

				ss << "Offset size is '0' in LZSS stream for archive '" << target << "' (corruption suspected)!\n";

				ForceClose(
					ss.str(),
					ForceCloseType::TYPE_DECOMPRESSION_BUFFER);

				return;
			}
			if (offset > buffer.size())
			{
				ostringstream ss{};

				ss << "Offset size '" << offset << "' is bigger than buffer size '"
					<< buffer.size() << "' in LZSS stream for archive '" << target << "' (corruption suspected)!\n";

				ForceClose(
					ss.str(),
					ForceCloseType::TYPE_DECOMPRESSION_BUFFER);

				return;
			}

			size_t start = buffer.size() - offset;
			for (size_t i = 0; i < length; i++)
			{
				if (buffer.size() >= originalSize)
				{
					ostringstream ss{};

					ss << "Decompressed size '" << buffer.size() << "' "
						<< "exceeds expected size '" << originalSize << "' "
						<< "while reading archive '" << target << "'!\n";

					ForceClose(
						ss.str(),
						ForceCloseType::TYPE_DECOMPRESSION_BUFFER);

					return;
				}
				buffer.push_back(buffer[start + i]);
			}
		}
	}

	if (buffer.size() != originalSize) 
	{
		ostringstream ss{};

		ss << "Decompressed size '" << buffer.size()
			<< "' does not match expected size '" << originalSize
			<< "' for archive '" << target << "' (possible corruption)!\n";

		ForceClose(
			ss.str(),
			ForceCloseType::TYPE_DECOMPRESSION_BUFFER);

		return;
	}

	//hand decompressed data back to caller
	out = move(buffer);
}

void BuildCodes(
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

vector<uint8_t> HuffmanEncode(
	const vector<uint8_t>& input,
	const string& origin)
{
	if (input.empty()) return {};

	size_t freq[256]{};
	for (auto b : input) freq[b]++;

	//build priority queue
	priority_queue<unique_ptr<HuffNode>, vector<unique_ptr<HuffNode>>, NodeCompare> pq{};
	for (int i = 0; i < 256; i++)
	{
		if (freq[i] > 0) pq.push(make_unique<HuffNode>((uint8_t)i, freq[i]));
	}
	if (pq.empty())
	{
		ForceClose(
			"HuffmanEncode found no symbols in '" + origin + "'",
			ForceCloseType::TYPE_HUFFMAN_ENCODE);

		return {};
	}
	if (pq.size() == 1) pq.push(make_unique<HuffNode>(0, 1));

	while (pq.size() > 1)
	{
		auto ExtractTop = [&](auto& q)
			{
				unique_ptr<HuffNode> node = move(const_cast<unique_ptr<HuffNode>&>(q.top()));
				q.pop();
				return node;
			};

		auto left = ExtractTop(pq);
		auto right = ExtractTop(pq);

		auto merged = make_unique<HuffNode>(move(left), move(right));
		pq.push(move(merged));
	}
	unique_ptr<HuffNode> root = move(const_cast<unique_ptr<HuffNode>&>(pq.top()));

	//build codes
	map<uint8_t, string> codes{};
	BuildCodes(root.get(), "", codes);

	//serialize frequency table
	uint16_t nonZero = 0;
	for (int i = 0; i < 256; i++)
	{
		if (freq[i] > 0) nonZero++;
	}

	size_t denseSize = 256 * sizeof(uint32_t);                //always 1024

	constexpr size_t entrySize = 5;
	if (nonZero > (SIZE_MAX - sizeof(uint16_t)) / entrySize)
	{
		ForceClose(
			"Sparse size overflow in '" + origin + "'!\n",
			ForceCloseType::TYPE_HUFFMAN_ENCODE);

		return {};
	}
	size_t sparseSize = sizeof(uint16_t) + nonZero * entrySize; //count + (sym + freq)


	bool useSparse = (sparseSize < denseSize);

	vector<uint8_t> output{};
	uint8_t mode = useSparse ? 1 : 0;
	output.push_back(mode);

	if (useSparse)
	{
		//write non-zero count
		output.insert(
			output.end(),
			reinterpret_cast<uint8_t*>(&nonZero),
			reinterpret_cast<uint8_t*>(&nonZero) + sizeof(uint16_t));

		//write each symbol + frequency
		for (int i = 0; i < 256; i++)
		{
			if (freq[i] > 0)
			{
				uint8_t symbol = (uint8_t)i;
				uint32_t f = (uint32_t)freq[i];
				output.push_back(symbol);
				output.insert(
					output.end(),
					reinterpret_cast<uint8_t*>(&f),
					reinterpret_cast<uint8_t*>(&f) + sizeof(uint32_t));
			}
		}
	}
	else
	{
		//write dense table
		for (int i = 0; i < 256; i++)
		{
			uint32_t f = (uint32_t)freq[i];
			output.insert(
				output.end(),
				reinterpret_cast<uint8_t*>(&f),
				reinterpret_cast<uint8_t*>(&f) + sizeof(uint32_t));
		}
	}

	//bit-pack data
	uint8_t bitbuf = 0;
	int bitcount = 0;
	vector<uint8_t> dataBits{};

	for (auto b : input)
	{
		const auto it = codes.find(b);
		if (it == codes.end())
		{
			ForceClose(
				"HuffmanEncode missing code for symbol in '" + origin + "'!\n",
				ForceCloseType::TYPE_HUFFMAN_ENCODE);

			return {};
		}

		for (char c : it->second)
		{
			bitbuf <<= 1;
			if (c == '1') bitbuf |= 1;
			bitcount++;
			if (bitcount == 8)
			{
				dataBits.push_back(bitbuf);
				bitbuf = 0;
				bitcount = 0;
			}
		}
	}
	if (bitcount > 0)
	{
		bitbuf <<= (8 - bitcount);
		dataBits.push_back(bitbuf);
	}

	output.insert(
		output.end(),
		dataBits.begin(),
		dataBits.end());

	if (output.empty())
	{
		ForceClose(
			"HuffmanEncode produced empty output for '" + origin + "'!\n",
			ForceCloseType::TYPE_HUFFMAN_ENCODE);

		return {};
	}

	return output;
}

vector<uint8_t> HuffmanDecode(
	ifstream& in,
	size_t storedSize,
	const string& origin)
{
	vector<uint8_t> out{};

	if (storedSize < 2)
	{
		ForceClose(
			"Stored size is too small in '" + origin + "'!\n",
			ForceCloseType::TYPE_HUFFMAN_DECODE);

		return {};
	}

	//read storage mode flag
	uint8_t mode{};
	if (!in.read((char*)&mode, sizeof(uint8_t)))
	{
		ForceClose(
			"Unexpected EOF while reading Huffman storage mode in '" + origin + "'!\n",
			ForceCloseType::TYPE_HUFFMAN_DECODE);

		return {};
	}

	size_t freq[256]{};
	uint16_t nonZero = 0;

	if (mode == 1)
	{
		//read nonZero count
		
		if (!in.read((char*)&nonZero, sizeof(uint16_t)))
		{
			ForceClose(
				"Unexpected EOF while reading Huffman table size in '" + origin + "'!\n",
				ForceCloseType::TYPE_HUFFMAN_DECODE);

			return {};
		}

		//read each (symbol, freq)
		for (uint16_t i = 0; i < nonZero; i++)
		{
			uint8_t symbol{};
			uint32_t f{};
			if (!in.read((char*)&symbol, sizeof(uint8_t))
				|| !in.read((char*)&f, sizeof(uint32_t)))
			{
				ForceClose(
					"Unexpected EOF while reading Huffman sparse table entry in '" + origin + "'!\n",
					ForceCloseType::TYPE_HUFFMAN_DECODE);

				return {};
			}
			freq[symbol] = f;
		}
	}
	else
	{
		//dense table
		for (int i = 0; i < 256; i++)
		{
			uint32_t f{};
			if (!in.read((char*)&f, sizeof(uint32_t)))
			{
				ForceClose(
					"Unexpected EOF while reading Huffman dense table entry in '" + origin + "'!\n",
					ForceCloseType::TYPE_HUFFMAN_DECODE);

				return {};
			}
			freq[i] = f;
		}
	}

	//rebuild tree
	priority_queue<unique_ptr<HuffNode>, vector<unique_ptr<HuffNode>>, NodeCompare> pq{};
	size_t totalSymbols{};
	for (int i = 0; i < 256; i++)
	{
		if (freq[i] > 0)
		{
			pq.push(make_unique<HuffNode>((uint8_t)i, freq[i]));
			totalSymbols += freq[i];
		}
	}
	if (pq.empty())
	{
		ForceClose(
			"Found empty frequency table in '" + origin + "'!\n",
			ForceCloseType::TYPE_HUFFMAN_DECODE);

		return {};
	}
	if (pq.size() == 1) pq.push(make_unique<HuffNode>(0, 1));

	while (pq.size() > 1)
	{
		auto ExtractTop = [&](auto& q)
			{
				unique_ptr<HuffNode> node = move(const_cast<unique_ptr<HuffNode>&>(q.top()));
				q.pop();
				return node;
			};

		auto left = ExtractTop(pq);
		auto right = ExtractTop(pq);

		auto merged = make_unique<HuffNode>(move(left), move(right));
		pq.push(move(merged));
	}
	unique_ptr<HuffNode> root = move(const_cast<unique_ptr<HuffNode>&>(pq.top()));
	pq.pop();

	//read remaining bitstream
	size_t headerBytes = 0;
	if (mode == 1)
	{
		//sparse
		headerBytes = sizeof(uint8_t) + sizeof(uint16_t) + nonZero * (sizeof(uint8_t) + sizeof(uint32_t));
	}
	else
	{
		//dense
		headerBytes = sizeof(uint8_t) + 256 * sizeof(uint32_t);
	}

	size_t remaining = storedSize - headerBytes;

	vector<uint8_t> bitstream(remaining);
	if (!in.read((char*)bitstream.data(), remaining))
	{
		ForceClose(
			"Unexpected EOF while reading Huffman bitstream in '" + origin + "'!\n",
			ForceCloseType::TYPE_HUFFMAN_DECODE);

		return {};
	}

	//decode
	HuffNode* node = root.get();
	for (size_t i = 0; i < bitstream.size(); i++)
	{
		uint8_t byte = bitstream[i];
		for (int b = 7; b >= 0; b--)
		{
			int bit = (byte >> b) & 1;
			node = (bit == 0) ? node->left.get() : node->right.get();

			if (!node->left
				&& !node->right)
			{
				out.push_back(node->symbol);
				node = root.get();

				if (out.size() == totalSymbols) return out;
			}
		}
	}

	if (out.size() != totalSymbols)
	{
		ForceClose(
			"Output size mismatch in '" + origin + "'!\n",
			ForceCloseType::TYPE_HUFFMAN_DECODE);

		return {};
	}

	return out;
}