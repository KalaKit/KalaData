//Copyright(C) 2025 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#elif __linux__
//TODO: ADD LINUX EQUIVALENT
#endif
#include <iostream>
#include <sstream>
#include <iterator>
#include <vector>

#include "core.hpp"
#include "command.hpp"

using KalaHeaders::Log;
using KalaHeaders::TimeFormat;
using KalaHeaders::DateFormat;

using std::istringstream;
using std::istream_iterator;
using std::vector;
using std::getline;
using std::cin;

static bool isRunning = false;

namespace KalaData::Core
{
	void KalaDataCore::Update()
	{
		isRunning = true;

		while (isRunning)
		{
			Log::Print("KalaData > ");

			string input;
			getline(cin, input);

			istringstream iss(input);
			vector<string> tokens
			{
				istream_iterator<string>{iss},
				istream_iterator<string>{}
			};
			tokens.insert(tokens.begin(), "KalaData.exe");

			Command::HandleCommand(tokens);
		}
	}

	void KalaDataCore::PrintMessage(
		const string& message,
		const string& originStamp,
		LogType type)
	{
		if (isVerboseLoggingEnabled)
		{
			//always force HH:MM:SS:MS time stamp if verbose logging is enabled
			if (Log::GetDefaultTimeFormat() != TimeFormat::TIME_HMS_MS)
			{
				Log::SetDefaultTimeFormat(TimeFormat::TIME_HMS_MS);
			}
		}
		else
		{
			//always force no time stamp if verbose logging is disabled
			if (Log::GetDefaultTimeFormat() != TimeFormat::TIME_NONE)
			{
				Log::SetDefaultTimeFormat(TimeFormat::TIME_NONE);
			}
		}

		//always force no date stamp
		if (Log::GetDefaultDateFormat() != DateFormat::DATE_NONE)
		{
			Log::SetDefaultDateFormat(DateFormat::DATE_NONE);
		}

		if (originStamp.empty()) Log::Print(message);
		else                     Log::Print(message, originStamp, type);
	}

	void KalaDataCore::ForceClose(const string& title, const string& message)
	{
		string shutdownType = "CORE";

		if (title.find("Compression error") != string::npos) shutdownType == "COMPRESS";
		else if (title.find("Decompression error") != string::npos) shutdownType == "DECOMPRESS";

		else if (title.find("Compression buffer error") != string::npos) shutdownType == "COMPRESS_BUFFER";
		else if (title.find("Decompression buffer error") != string::npos) shutdownType == "DECOMPRESS_BUFFER";

		else if (title.find("Huffman encode error") != string::npos) shutdownType == "HUFFMAN_ENCODE";
		else if (title.find("Huffman decode error") != string::npos) shutdownType == "HUFFMAN_DECODE";

		PrintMessage(
			message,
			shutdownType,
			LogType::LOG_ERROR);

#ifdef _WIN32
		int flags =
			MB_OK
			| MB_ICONERROR;
#elif __linux__
		//TODO: ADD LINUX EQUIVALENT
#endif

		if (MessageBox(
			nullptr,
			message.c_str(),
			title.c_str(),
			flags) == 1)
		{
			Shutdown(ShutdownState::SHUTDOWN_CRITICAL);
		}
	}

	void KalaDataCore::ForceCloseByType(
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

		ForceClose(title, message);
	}

	void KalaDataCore::Shutdown(ShutdownState state)
	{
		if (state == ShutdownState::SHUTDOWN_CRITICAL)
		{
			PrintMessage(
				"Critical KalaData shutdown!\n",
				"CORE",
				LogType::LOG_WARNING);

			quick_exit(EXIT_FAILURE);
			return;
		}

		PrintMessage(
			"KalaData has shut down normally.\n",
			"CORE",
			LogType::LOG_DEBUG);

		exit(0);
	}
}