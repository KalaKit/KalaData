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
#include <map>

#include "core.hpp"
#include "command.hpp"
#include "compression.hpp"

using KalaData::Core::KalaDataCore;
using KalaData::Core::Command;
using KalaData::Core::MessageType;
using KalaData::Core::ForceCloseType;
using KalaData::Compression::Archive;
using KalaData::Compression::Token;
using KalaData::Compression::HuffNode;
using KalaData::Compression::HuffNode32;
using KalaData::Compression::NodeCompare;
using KalaData::Compression::NodeCompare32;

using KalaData::Compression::MIN_MATCH;

using std::filesystem::path;
using std::filesystem::relative;
using std::filesystem::is_regular_file;
using std::filesystem::file_size;
using std::filesystem::recursive_directory_iterator;
using std::ofstream;
using std::ifstream;
using std::ios;
using std::istreambuf_iterator;
using std::vector;
using std::ostringstream;
using std::string;
using std::chrono::high_resolution_clock;
using std::chrono::duration;
using std::chrono::seconds;
using std::fixed;
using std::setprecision;
using std::map;

//Generate tokens list from raw data
static vector<Token> CompressToTokens(
	const vector<uint8_t>,
	const string& origin);

//Wrap LZSS token output with Huffman
static vector<uint8_t> HuffmanEncodeTokens(
	const vector<Token>& tokens,
	const string& origin);

//Serializes a frequency table (literals or lengths) into the output stream (1 byte)
static void WriteTable(
	vector<uint8_t>& out,
	const size_t freq[],
	size_t count);

//Serializes a frequency table (offsets) into the output stream (4 bytes)
static void WriteTable32(
	vector<uint8_t>& out,
	const map<uint32_t, size_t>& offFreq);

//Utility class for bit-packing Huffman output stream
struct BitWriter
{
	uint8_t buffer = 0;
	int count = 0;
	vector<uint8_t> data;

	void WriteBit(int bit)
	{
		buffer <<= 1;
		if (bit) buffer |= 1;

		count++;
		if (count == 8)
		{
			data.push_back(buffer);
			buffer = 0;
			count = 0;
		}
	}

	void WriteCode(const string& code)
	{
		for (char c : code) { WriteBit(c == '1'); }
	}

	void Flush(vector<uint8_t>& out)
	{
		if (count > 0)
		{
			buffer <<= (8 - count);
			data.push_back(buffer);

			buffer = 0;
			count = 0;
		}

		out.insert(out.end(), data.begin(), data.end());
		data.clear();
	}
};

namespace KalaData::Compression
{
	void Archive::Compress(
		const string& origin,
		const string& target)
	{
		Command::SetCommandAllowState(false);

		KalaDataCore::PrintMessage(
			"Starting to compress folder '" + origin + "' to archive '" + target + "'!\n");

		//start clock timer
		auto start = high_resolution_clock::now();

		ofstream out(target, ios::binary);
		if (!out.is_open())
		{
			KalaDataCore::ForceCloseByType(
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
			KalaDataCore::ForceCloseByType(
				"Origin folder '" + origin + "' contains no valid files to compress!\n",
				ForceCloseType::TYPE_COMPRESSION);

			return;
		}

		uint32_t compCount{};
		uint32_t rawCount{};
		uint32_t emptyCount{};

		const char magicVer[6] = { 'K', 'D', 'A', 'T', KALADATA_VERSION[9], KALADATA_VERSION[11] };
		out.write(magicVer, sizeof(magicVer));

		if (KalaDataCore::IsVerboseLoggingEnabled())
		{
			ostringstream ss{};

			ss << "Archive '" + target + "' version will be '" + string(magicVer, 6) + "'.\n\n"
				<< "Window size is '" << WINDOW_SIZE << "'.\n"
				<< "Lookahead is '" << LOOKAHEAD << "'.\n"
				<< "Min match is '" << MIN_MATCH << "'.\n";

			KalaDataCore::PrintMessage(ss.str());
		}

		uint32_t fileCount = (uint32_t)files.size();
		out.write((char*)&fileCount, sizeof(uint32_t));

		if (!out.good())
		{
			KalaDataCore::ForceCloseByType(
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

			vector<Token> tokens = CompressToTokens(raw, relPath);

			vector<uint8_t> compData = HuffmanEncodeTokens(tokens, origin);

			uint64_t originalSize = raw.size();
			uint64_t compressedSize = compData.size();

			//safeguard: if compression is bigger or equal than original then store raw instead
			bool useCompressed = compressedSize < originalSize;
			const vector<uint8_t>& finalData = useCompressed ? compData : raw;
			uint64_t finalSize = useCompressed ? compressedSize : originalSize;

			uint8_t method = useCompressed ? 1 : 0; //0 - raw, 1 - compressed

			if (!useCompressed)
			{
				if (originalSize == 0)
				{
					emptyCount++;

					if (KalaDataCore::IsVerboseLoggingEnabled())
					{
						KalaDataCore::PrintMessage(
							"[EMPTY] '" + path(relPath).filename().string() + "'");
					}
				}
				else
				{
					rawCount++;

					if (KalaDataCore::IsVerboseLoggingEnabled())
					{
						ostringstream ss{};

						ss << "[RAW] '" << path(relPath).filename().string()
							<< "' - '" << compressedSize << " bytes' "
							<< ">= '" << originalSize << " bytes'";

						KalaDataCore::PrintMessage(ss.str());
					}
				}
			}
			else
			{
				compCount++;

				if (KalaDataCore::IsVerboseLoggingEnabled())
				{
					ostringstream ss{};

					ss << "[COMPRESS] '" << path(relPath).filename().string()
						<< "' - '" << compressedSize << " bytes' "
						<< "< '" << originalSize << " bytes'";

					KalaDataCore::PrintMessage(ss.str());
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
				KalaDataCore::ForceCloseByType(
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
					KalaDataCore::ForceCloseByType(
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

		if (KalaDataCore::IsVerboseLoggingEnabled())
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

		KalaDataCore::PrintMessage(
			finishComp.str(),
			MessageType::MESSAGETYPE_SUCCESS);

		Command::SetCommandAllowState(true);
	}
}

vector<Token> CompressToTokens(
	const vector<uint8_t> input,
	const string& origin)
{
	vector<Token> tokens{};
	if (input.empty()) return tokens;

	size_t windowSize = Archive::GetWindowSize();
	size_t lookAhead = Archive::GetLookAhead();

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
			if (bestLength > UINT8_MAX)
			{
				KalaDataCore::ForceCloseByType(
					"Match length too large for file '" + origin + "' during compressing (overflow)!\n",
					ForceCloseType::TYPE_COMPRESSION_BUFFER);

				return {};
			}

			if (bestOffset >= UINT32_MAX)
			{
				KalaDataCore::ForceCloseByType(
					"Offset too large for file '" + origin + "' during compressing (data window exceeded)!\n",
					ForceCloseType::TYPE_COMPRESSION_BUFFER);

				return {};
			}

			tokens.push_back(
				{
					false,
					0,
					(uint32_t)bestOffset,
					(uint8_t)bestLength
				});

			pos += bestLength;
		}
		else
		{
			tokens.push_back(
				{
					true,
					input[pos],
					0,
					0
				});

			pos++;
		}
	}

	return tokens;
}

vector<uint8_t> HuffmanEncodeTokens(
	const vector<Token>& tokens,
	const string& origin)
{
	if (tokens.empty()) return {};

	//build frequency tables
	size_t litFreq[256]{};           //literals
	map<uint32_t, size_t> offFreq{}; //offsets (variable size)
	size_t lenFreq[256]{};           //lengths
	size_t flagFreq[2]{};            //flags

	for (auto& t : tokens)
	{
		if (t.isLiteral)
		{
			litFreq[t.literal]++;
			flagFreq[1]++;
		}
		else
		{
			offFreq[t.offset]++;
			lenFreq[t.length]++;
			flagFreq[0]++;
		}
	}

	//build Huffman codes separately
	auto litCodes = Archive::BuildHuffman(litFreq, 256);
	auto lenCodes = Archive::BuildHuffman(lenFreq, 256);
	auto offCodes = Archive::BuildHuffmanMap(offFreq);

	//flags can just be bitpacked, so no Huffman needed

	//serialize header
	vector<uint8_t> output{};
	WriteTable(output, litFreq, 256);
	WriteTable(output, lenFreq, 256);
	WriteTable32(output, offFreq);

	//encode tokens
	BitWriter bw{};
	for (auto& t : tokens)
	{
		if (t.isLiteral)
		{
			bw.WriteBit(1); //flag
			bw.WriteCode(litCodes[t.literal]);
		}
		else
		{
			bw.WriteBit(0); //flag
			bw.WriteCode(offCodes[t.offset]);
			bw.WriteCode(lenCodes[t.length]);
		}
	}
	bw.Flush(output);

	return output;
}

void WriteTable(
	vector<uint8_t>& out,
	const size_t freq[],
	size_t count)
{
	uint16_t nonZero = 0;
	for (size_t i = 0; i < count; i++)
	{
		if (freq[i] > 0) nonZero++;
	}

	size_t denseSize = count * sizeof(uint32_t);
	size_t sparseSize = sizeof(uint16_t) + nonZero * (1 + sizeof(uint32_t));
	bool useSparse = (sparseSize < denseSize);

	out.push_back(useSparse ? 1 : 0);

	if (useSparse)
	{
		out.insert(out.end(),
			reinterpret_cast<const uint8_t*>(&nonZero),
			reinterpret_cast<const uint8_t*>(&nonZero) + sizeof(uint16_t));

		for (size_t i = 0; i < count; i++)
		{
			if (freq[i] > 0)
			{
				uint8_t symbol = (uint8_t)i;
				uint32_t f = (uint32_t)freq[i];

				out.push_back(symbol);
				out.insert(out.end(),
					reinterpret_cast<const uint8_t*>(&f),
					reinterpret_cast<const uint8_t*>(&f) + sizeof(uint32_t));
			}
		}
	}
	else
	{
		for (size_t i = 0; i < count; i++)
		{
			if (freq[i] > 0)
			{
				uint32_t f = (uint32_t)freq[i];
				out.insert(out.end(),
					reinterpret_cast<const uint8_t*>(&f),
					reinterpret_cast<const uint8_t*>(&f) + sizeof(uint32_t));
			}
		}
	}
}

void WriteTable32(
	vector<uint8_t>& out,
	const map<uint32_t, size_t>& offFreq)
{
	uint32_t nonZero = (uint32_t)offFreq.size();

	//write non-zero count (32-bit, offsets can be large)
	out.insert(out.end(),
		reinterpret_cast<const uint8_t*>(&nonZero),
		reinterpret_cast<const uint8_t*>(&nonZero) + sizeof(uint32_t));

	//write each (offset, freq) pair
	for (auto& [sym, f] : offFreq)
	{
		uint32_t symbol = sym;
		uint32_t freq = (uint32_t)f;

		out.insert(out.end(),
			reinterpret_cast<const uint8_t*>(&symbol),
			reinterpret_cast<const uint8_t*>(&symbol) + sizeof(uint32_t));

		out.insert(out.end(),
			reinterpret_cast<const uint8_t*>(&freq),
			reinterpret_cast<const uint8_t*>(&freq) + sizeof(uint32_t));
	}
}