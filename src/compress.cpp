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
using KalaData::Core::MessageType;
using KalaData::Core::ForceCloseType;
using KalaData::Compression::Archive;
using KalaData::Compression::HuffNode;
using KalaData::Compression::NodeCompare;

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
using std::priority_queue;
using std::unique_ptr;
using std::move;
using std::make_unique;
using std::memcmp;
using std::exception;

//Compress a single buffer into an already open stream
static vector<uint8_t> CompressBuffer(
	const vector<uint8_t>& input,
	const string& origin);

//Recursively assign codes
static void BuildCodes(
	HuffNode* node,
	const string& prefix,
	map<uint8_t, string>& codes);

//Post-LZSS filter
static vector<uint8_t> HuffmanEncode(
	const vector<uint8_t>& input,
	const string& origin);

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

vector<uint8_t> CompressBuffer(
	const vector<uint8_t>& input,
	const string& origin)
{
	size_t windowSize = Archive::GetWindowSize();
	size_t lookAhead = Archive::GetLookAhead();

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
				KalaDataCore::ForceCloseByType(
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
				KalaDataCore::ForceCloseByType(
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
		KalaDataCore::ForceCloseByType(
			"Compression produced empty output for file '" + origin + "' (unexpected)!\n",
			ForceCloseType::TYPE_COMPRESSION_BUFFER);
	}

	return output;
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
		KalaDataCore::ForceCloseByType(
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
		KalaDataCore::ForceCloseByType(
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
			KalaDataCore::ForceCloseByType(
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
		KalaDataCore::ForceCloseByType(
			"HuffmanEncode produced empty output for '" + origin + "'!\n",
			ForceCloseType::TYPE_HUFFMAN_ENCODE);

		return {};
	}

	return output;
}