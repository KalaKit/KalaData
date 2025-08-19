//Copyright(C) 2025 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include <sstream>
#include <string>
#include <filesystem>
#include <iomanip>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <ranges>
#include <cctype>
#include <algorithm>

#include "core.hpp"
#include "command.hpp"
#include "compression.hpp"

using KalaData::Core::KalaDataCore;
using KalaData::Core::MessageType;
using KalaData::Compression::Archive;

using KalaData::Compression::WINDOW_SIZE_FASTEST;
using KalaData::Compression::WINDOW_SIZE_FAST;
using KalaData::Compression::WINDOW_SIZE_BALANCED;
using KalaData::Compression::WINDOW_SIZE_SLOW;
using KalaData::Compression::WINDOW_SIZE_ARCHIVE;

using KalaData::Compression::LOOKAHEAD_FASTEST;
using KalaData::Compression::LOOKAHEAD_FAST;
using KalaData::Compression::LOOKAHEAD_BALANCED;
using KalaData::Compression::LOOKAHEAD_SLOW;
using KalaData::Compression::LOOKAHEAD_ARCHIVE;

using KalaData::Compression::MIN_MATCH;

using std::ostringstream;
using std::string;
using std::to_string;
using std::filesystem::path;
using std::filesystem::exists;
using std::filesystem::remove;
using std::filesystem::is_regular_file;
using std::filesystem::is_directory;
using std::filesystem::file_size;
using std::filesystem::is_empty;
using std::filesystem::weakly_canonical;
using std::filesystem::current_path;
using std::filesystem::create_directories;
using std::filesystem::remove;
using std::filesystem::remove_all;
using std::filesystem::directory_iterator;
using std::filesystem::recursive_directory_iterator;
using std::fixed;
using std::setprecision;
using std::ofstream;
using std::ios;
using std::unordered_map;
using std::vector;
using std::exception;
using std::cin;
using std::toupper;
using std::ranges::any_of;
using std::equal;

static uint64_t GetFolderSize(const string& folderPath);

static bool CanWriteToFolder(const string& folderPath);

static string ConvertSizeToString(uint64_t size);

static string ResolvePath(
	const string& origin,
	bool checkExistence = false);

struct Preset
{
	size_t window;
	size_t lookahead;
};

static const unordered_map<string, Preset> presets =
{
	{ "fastest",  { WINDOW_SIZE_FASTEST,  LOOKAHEAD_FASTEST  } },
	{ "fast",     { WINDOW_SIZE_FAST,     LOOKAHEAD_FAST     } },
	{ "balanced", { WINDOW_SIZE_BALANCED, LOOKAHEAD_BALANCED } },
	{ "slow",     { WINDOW_SIZE_SLOW,     LOOKAHEAD_SLOW     } },
	{ "archive",  { WINDOW_SIZE_ARCHIVE,  LOOKAHEAD_ARCHIVE  } }
};

static const vector<string> restrictedFileNames
{
	"CON",
	"PRN",
	"AUX",
	"NUL",

	"COM1",
	"COM2",
	"COM3",
	"COM4",
	"COM5",
	"COM6",
	"COM7",
	"COM8",
	"COM9",

	"LPT1",
	"LPT2",
	"LPT3",
	"LPT4",
	"LPT5",
	"LPT6",
	"LPT7",
	"LPT8",
	"LPT9",
};

//5GB max file size
constexpr uint64_t maxFolderSize = 5ull * 1024 * 1024 * 1024;

//where user has navigated with --go command
static string currentPath{};

namespace KalaData::Core
{
	void Command::HandleCommand(vector<string> parameters)
	{
		if (!canAllowCommands
			|| parameters.size() <= 1)
		{
			return;
		}

		//remove 'KalaData> ' at the front of the parameters
		vector<string> cleanedParameters = parameters;
		cleanedParameters.erase(cleanedParameters.begin());

		if (parameters.size() == 2
			&& parameters[1] == "--v")
		{
			Command_Version();
			return;
		}

		else if (parameters.size() == 2
			&& parameters[1] == "--about")
		{
			Command_About();
			return;
		}

		else if (parameters.size() == 2
			&& parameters[1] == "--help")
		{
			Command_Help();
			return;
		}

		else if (parameters.size() == 3
			&& parameters[1] == "--help")
		{
			Command_Help_Command(parameters[2]);
			return;
		}

		else if (parameters.size() == 3
			&& parameters[1] == "--go")
		{
			Command_Go(parameters[2]);
			return;
		}

		else if (parameters.size() == 2
			&& parameters[1] == "--root")
		{
			Command_Root();
			return;
		}

		else if (parameters.size() == 2
			&& parameters[1] == "--home")
		{
			Command_Home();
			return;
		}

		else if (parameters.size() == 2
			&& parameters[1] == "--where")
		{
			Command_Where();
			return;
		}

		else if (parameters.size() == 2
			&& parameters[1] == "--list")
		{
			Command_List();
			return;
		}

		else if (parameters.size() == 3
			&& parameters[1] == "--create")
		{
			Command_Create(parameters[2]);
			return;
		}

		else if (parameters.size() == 3
			&& parameters[1] == "--delete")
		{
			Command_Delete(parameters[2]);
			return;
		}

		else if (parameters.size() == 3
			&& parameters[1] == "--sm")
		{
			Command_SetCompressionMode(parameters[2]);
			return;
		}

		else if (parameters.size() == 2
			&& parameters[1] == "--tvb")
		{
			Command_ToggleCompressionVerbosity();
			return;
		}

		else if (parameters.size() == 4
			&& parameters[1] == "--c")
		{
			Command_Compress(parameters[2], parameters[3]);
			return;
		}

		else if (parameters.size() == 4
			&& parameters[1] == "--dc")
		{
			Command_Decompress(parameters[2], parameters[3]);
			return;
		}

		else if (parameters.size() == 2
			&& parameters[1] == "--exit")
		{
			Command_Exit();
			return;
		}

		string command{};

		for (const auto& par : parameters)
		{
			if (par == parameters[0])
			{
				command = command + par;
			}
			else  command = command + " " + par;
		}

		string target = "KalaData.exe ";
		size_t pos = command.find(target);
		if (pos != string::npos)
		{
			command.erase(pos, target.length());
		}

		ostringstream ss{};

		ss << "Unsupported command '" + command + "'! Type --help to list all commands.\n";

		KalaDataCore::PrintMessage(
			ss.str(),
			MessageType::MESSAGETYPE_ERROR);
	}

	void Command::Command_Version()
	{
		KalaDataCore::PrintMessage(KALADATA_VERSION "\n");
	}

	void Command::Command_About()
	{
		ostringstream ss{};

		ss << "KalaData is a custom compression and decompression tool written in C++20, "
			<< "built entirely from scratch without external dependencies.\n"
			<< "It uses a hybrid LZSS + Huffman pipeline to compress data efficiently, "
			<< "while falling back to raw or empty storage when appropriate.\n"
			<< "All data is stored in a dedicated archival format with the '.kdat' extension.\n\n"

			<< "KalaData was created by and is maintained by KalaKit, an organization owned by Lost Empire Entertainment.\n"
			<< "Official repository: 'https://github.com/KalaKit/KalaData'\n";

		KalaDataCore::PrintMessage(ss.str());
	}

	void Command::Command_Help()
	{
		ostringstream ss{};

		ss << "====================\n\n"

			<< "Notes:\n"
			<< "  - KalaData accepts relative paths to current directory (or directory set with --go) or absolute paths.\n"
			<< "  - the command '-help command' expects a valid command, like '--help c'.\n"
			<< "  - the commands '--go' and '--delete' expect a valid file or directory path in your device\n"
			<< "  - the command '--create' expects a directory that does not exist\n"
			<< "  - the command '--sm mode' expects a valid mode, like '--sm balanced'\n\n"

			<< "Commands:\n"
			<< "  --v\n"
			<< "  --about\n"
			<< "  --help\n"
			<< "  --help command\n"
			<< "  --go path\n"
			<< "  --root\n"
			<< "  --home\n"
			<< "  --where\n"
			<< "  --list\n"
			<< "  --create path\n"
			<< "  --delete path\n"
			<< "  --sm mode\n"
			<< "  --tvb\n"
			<< "  --c\n"
			<< "  --dc\n"
			<< "  --exit\n\n"

			<< "====================\n";

		KalaDataCore::PrintMessage(ss.str());
	}

	void Command::Command_Help_Command(const string& commandName)
	{
		if (commandName == "v"
			|| commandName == "--v")
		{
			KalaDataCore::PrintMessage("Prints the KalaData version\n");

			return;
		}

		else if (commandName == "about"
			|| commandName == "--about")
		{
			KalaDataCore::PrintMessage("Prints the KalaData description\n");
		
			return;
		}

		else if (commandName == "help"
			|| commandName == "--help")
		{
			KalaDataCore::PrintMessage("Lists all commands\n");
		
			return;
		}

		else if (commandName == "go"
			|| commandName == "--go")
		{
			KalaDataCore::PrintMessage("Go to a directory on your device to be able to compress/decompress relative to that directory\n");
		
			return;
		}

		else if (commandName == "root"
			|| commandName == "--root")
		{
			KalaDataCore::PrintMessage("Navigate to system root directory\n");

			return;
		}

		else if (commandName == "home"
			|| commandName == "--home")
		{
			KalaDataCore::PrintMessage("Navigate to KalaData root directory\n");

			return;
		}

		else if (commandName == "where"
			|| commandName == "--where")
		{
			KalaDataCore::PrintMessage("Prints your current path (program default or the one set with --go)\n");
		
			return;
		}

		else if (commandName == "list"
			|| commandName == "--list")
		{
			KalaDataCore::PrintMessage("Lists all files and directories in your current path (program default or the one set with --go)\n");
		
			return;
		}

		else if (commandName == "create"
			|| commandName == "--create")
		{
			KalaDataCore::PrintMessage("Creates a new directory in your chosen path\n");

			return;
		}

		else if (commandName == "delete"
			|| commandName == "--delete")
		{
			ostringstream ss{};

			ss << "Deletes the file or directory at your chosen path, asks for permission first. "
				<< "Warning: the file or directory is unrecoverable after deletion!\n";

			KalaDataCore::PrintMessage(ss.str());

			return;
		}

		else if (commandName == "sm"
			|| commandName == "--sm")
		{
			ostringstream ss{};

			ss << "Sets the compression/decompression mode.\n"
				<< "Note: All modes share the same min_match value '3'.\n\n"

				<< "Available modes:\n"

				<< "- fastest\n"
				<< "  - best for temporary files\n"
				<< "  - window size: " << WINDOW_SIZE_FASTEST << " bytes\n"
				<< "  - lookahead: " << LOOKAHEAD_FASTEST << "\n\n"
				
				<< "- fast\n"
				<< "  - best for quick backups\n"
				<< "  - window size: " << WINDOW_SIZE_FAST<< " bytes\n"
				<< "  - lookahead: " << LOOKAHEAD_FAST << "\n\n"
				
				<< "- balanced\n"
				<< "  - best for general use\n"
				<< "  - window size: " << WINDOW_SIZE_BALANCED << " bytes\n"
				<< "  - lookahead: " << LOOKAHEAD_BALANCED << "\n\n"
				
				<< "- slow\n"
				<< "  - best for long term storage\n"
				<< "  - window size: " << WINDOW_SIZE_SLOW << " bytes\n"
				<< "  - lookahead: " << LOOKAHEAD_SLOW << "\n\n"
				
				<< "- archive\n"
				<< "  - best for maximum compression\n"
				<< "  - window size: " << WINDOW_SIZE_ARCHIVE << " bytes\n"
				<< "  - lookahead: " << LOOKAHEAD_ARCHIVE << "\n";

			KalaDataCore::PrintMessage(ss.str());

			return;
		}

		else if (commandName == "tvb"
			|| commandName == "--tvb")
		{
			ostringstream ss{};

			ss << "Toggles compression verbose messages on and off.\n"
				<< "If true, then the following info is also displayed:\n\n"

				<< "general:\n"
				<< "  - resolved paths for go, create, delete, compress and decompress commands"
				<< "  - archive version, window size, lookahead and min match when starting compression/decompression"

				<< "individual file logs:\n"
				<< "  - compressed/decompressed file is empty\n"
				<< "  - original file size is bigger than the "
				<< "compressed file size so it will not be compressed/decompressed\n"
				<< "  - stored file size is smaller or equal than the "
				<< "compressed file size so it will be compressed/decompressed\n\n"
				
				<< "compression/decompression success log additional rows:\n"
				<< "  - compression/expansion ratio\n"
				<< "  - compression/expansion factor\n"
				<< "  - throughput\n"
				<< "  - total files\n"
				<< "  - compressed files\n"
				<< "  - raw files\n"
				<< "  - empty files\n";

			KalaDataCore::PrintMessage(ss.str());

			return;
		}

		else if (commandName == "c"
			|| commandName == "--c")
		{
			ostringstream ss{};

			ss << "Takes in a directory which will be compressed into a '.kdat' file inside the target path parent directory.\n\n"
				<< "Requirements and restrictions:\n\n"

				<< "Origin:\n"
				<< "  - path must exist\n"
				<< "  - path must be a directory\n"
				<< "  - directory must not be empty\n"
				<< "  - directory size must not exceed 5GB\n\n"

				<< "Target:\n"
				<< "  - path must not exist\n"
				<< "  - path must have the '.kdat' extension\n"
				<< "  - path parent directory must be writable\n";

			KalaDataCore::PrintMessage(ss.str());

			return;
		}

		else if (commandName == "dc"
			|| commandName == "--dc")
		{
			ostringstream ss{};

			ss << "Takes in a compressed '.kdat' file path which will be decompressed inside the target directory.\n\n"
				<< "Requirements and restrictions:\n\n"

				<< "Origin:\n"
				<< "  - path must exist\n"
				<< "  - path must be a regular file\n"
				<< "  - path must have the '.kdat' extension\n\n"

				<< "Target:\n"
				<< "  - path must exist\n"
				<< "  - path must be a directory\n"
				<< "  - directory must be writable\n";

			KalaDataCore::PrintMessage(ss.str());

			return;
		}

		else if (commandName == "exit"
			|| commandName == "--exit")
		{
			KalaDataCore::PrintMessage("Shuts down KalaData\n");

			return;
		}

		KalaDataCore::PrintMessage(
			"Cannot get info about command '" + commandName + "' because it does not exist! Type '--help' to list all commands\n",
			MessageType::MESSAGETYPE_ERROR);
	}

	void Command::Command_Go(const string& target)
	{
		string canonicalTarget = ResolvePath(target, true);

		if (canonicalTarget.empty()) return;

		if (canonicalTarget == currentPath)
		{
			KalaDataCore::PrintMessage(
				"Already located at the same path '" + canonicalTarget + "'!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		if (!is_directory(canonicalTarget))
		{
			KalaDataCore::PrintMessage(
				"Target path '" + canonicalTarget + "' is not a directory!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		currentPath = canonicalTarget;

		KalaDataCore::PrintMessage("Moved to directory '" + canonicalTarget + "'\n",
			MessageType::MESSAGETYPE_SUCCESS);
	}

	void Command::Command_Root()
	{
		string rootDir = current_path().root_path().string();

		if (currentPath == rootDir)
		{
			KalaDataCore::PrintMessage("Already located at system root '" + currentPath + "'!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		currentPath = rootDir;

		KalaDataCore::PrintMessage(
			"Navigated to system root directory '" + currentPath + "'\n",
			MessageType::MESSAGETYPE_SUCCESS);
	}

	void Command::Command_Home()
	{
		if (currentPath == current_path().string())
		{
			KalaDataCore::PrintMessage("Already located at KalaData root '" + currentPath + "'!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		currentPath = current_path().string();

		KalaDataCore::PrintMessage(
			"Navigated to KalaData root '" + currentPath + "'\n",
			MessageType::MESSAGETYPE_SUCCESS);
	}

	void Command::Command_Where()
	{
		//always set current path if unset
		if (currentPath.empty()) currentPath = current_path().string();

		KalaDataCore::PrintMessage("Currently located at '" + currentPath + "'\n");
	}

	void Command::Command_List()
	{
		if (currentPath.empty()) currentPath = current_path().string();

		KalaDataCore::PrintMessage("Listing all files and directories in '" + currentPath + "'\n");

		for (const auto& thisPath : directory_iterator(currentPath))
		{
			string suffix = is_directory(path(thisPath))
				? "/"
				: "";

			string fullName = "  " + path(thisPath).filename().string() + suffix;

			KalaDataCore::PrintMessage(fullName);
		}
	}

	void Command::Command_Create(const string& target)
	{
		//Case-insensitive equals check
		auto Equals = [](const string& a, const string& b)
			{
				return a.size() == b.size()
					&& equal(a.begin(), a.end(), b.begin(),
						[](unsigned char ac, unsigned char bc)
						{
							return toupper(ac) == toupper(bc);
						});
			};

		string thisStem = path(target).stem().string();

		if (any_of(restrictedFileNames, 
				[&](const string& name) { return Equals(thisStem, name); }))
		{
			KalaDataCore::PrintMessage("File name '" + target + "' is restricted on Windows!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		auto canonicalTarget = ResolvePath(target);

		if (exists(canonicalTarget))
		{
			KalaDataCore::PrintMessage("Cannot create new directory '" + canonicalTarget + "' because it already exists!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		try
		{
			create_directories(canonicalTarget);
		}
		catch (const exception& e)
		{
			ostringstream ss{};

			ss << "Failed to create new directory! Reason: " << e.what() << "\n";

			KalaDataCore::PrintMessage(
				ss.str(),
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		KalaDataCore::PrintMessage(
			"Created new directory '" + path(canonicalTarget).stem().string() + "' at '" + canonicalTarget + "'\n",
			MessageType::MESSAGETYPE_SUCCESS);
	}

	void Command::Command_Delete(const string& target)
	{
		auto canonicalTarget = ResolvePath(target, true);

		if (!exists(canonicalTarget)) return;

		string answer{};

		ostringstream ss{};

		ss << "Are you sure you want to delete file or directory '" << canonicalTarget << "'?\n"
			<< "This is permanent and your file or directory can't be recovered!\n\n"
			<< "Type 'delete' to continue, any other answer skips the deletion.\n\n"

			<< "Your answer: ";

		KalaDataCore::PrintMessage(ss.str());

		cin >> answer;

		if (answer != "delete")
		{
			KalaDataCore::PrintMessage(
				"Skipped the deletion of file or directory '" + canonicalTarget + "'\n");

			return;
		}

		try
		{
			if (is_directory(canonicalTarget)) remove_all(canonicalTarget);
			else remove(canonicalTarget);
		}
		catch (const exception& e)
		{
			ostringstream ss{};

			ss << "Failed to delete file or directory! Reason: " << e.what() << "\n";

			KalaDataCore::PrintMessage(
				ss.str(),
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		KalaDataCore::PrintMessage(
			"Deleted file or directory '" + canonicalTarget + "'\n",
			MessageType::MESSAGETYPE_SUCCESS);
	}

	void Command::Command_SetCompressionMode(const string& mode)
	{
		auto it = presets.find(mode);
		if (it == presets.end())
		{
			KalaDataCore::PrintMessage(
				"Compression mode '" + mode + "' does not exist!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		Archive::SetWindowSize(it->second.window);
		Archive::SetLookAhead(it->second.lookahead);

		ostringstream ss{};

		ss << "Set compression mode to '" + mode + "'!\n"
			<< "  Window size is '" << Archive::GetWindowSize() << " bytes'\n"
			<< "  Lookahead is '" << Archive::GetLookAhead() << "'\n";

		KalaDataCore::PrintMessage(
			ss.str(),
			MessageType::MESSAGETYPE_SUCCESS);
	}

	void Command::Command_ToggleCompressionVerbosity()
	{
		bool state = KalaDataCore::IsVerboseLoggingEnabled();
		state = !state;

		KalaDataCore::SetVerboseLoggingState(state);

		string stateStr = state ? "true" : "false";

		KalaDataCore::PrintMessage(
			"Set compression verbose logging state to '" + stateStr + "'!\n");
	}

	void Command::Command_Compress(
		const string& origin,
		const string& target)
	{
		if (origin == "/"
			|| origin == "\\")
		{
			KalaDataCore::PrintMessage(
				"Path '" + origin + "' is not allowed as origin path!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		auto canonicalOrigin = ResolvePath(origin, true);
		auto canonicalTarget = ResolvePath(target);

		if (canonicalOrigin.empty()) return;

		if (!is_directory(canonicalOrigin))
		{
			KalaDataCore::PrintMessage(
				"Origin '" + canonicalOrigin + "' must be a directory!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		if (is_empty(canonicalOrigin))
		{
			KalaDataCore::PrintMessage(
				"Origin '" + canonicalOrigin + "' must not be an empty directory!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		uint64_t originSize = GetFolderSize(canonicalOrigin);
		if (originSize > maxFolderSize)
		{
			string convertedOriginSize = ConvertSizeToString(originSize);

			KalaDataCore::PrintMessage(
				"Origin '" + canonicalOrigin + "' size '" + convertedOriginSize + "' exceeds max allowed size '5.00GB'!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		if (exists(canonicalTarget))
		{
			KalaDataCore::PrintMessage(
				"Target '" + canonicalTarget + "' already exists!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		if (path(canonicalTarget).extension().string() != ".kdat")
		{
			KalaDataCore::PrintMessage(
				"Target path '" + canonicalTarget + "' must have the '.kdat' extension!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		string targetParentFolder = path(canonicalTarget).parent_path().string();
		if (!CanWriteToFolder(targetParentFolder))
		{
			KalaDataCore::PrintMessage(
				"Unable to write to target parent directory '" + targetParentFolder + "'!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		Archive::Compress(canonicalOrigin, canonicalTarget);
	}

	void Command::Command_Decompress(
		const string& origin,
		const string& target)
	{
		auto canonicalOrigin = ResolvePath(origin, true);
		auto canonicalTarget = ResolvePath(target);

		if (canonicalOrigin.empty()) return;

		if (!is_regular_file(canonicalOrigin))
		{
			KalaDataCore::PrintMessage(
				"Origin '" + canonicalOrigin + "' must be a regular file!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		if (path(canonicalOrigin).extension().string() != ".kdat")
		{
			KalaDataCore::PrintMessage(
				"Origin '" + canonicalOrigin + "' must have the '.kdat' extension!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		if (!exists(canonicalTarget))
		{
			KalaDataCore::PrintMessage(
				"Target directory '" + canonicalTarget + "' does not exist!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		if (!is_directory(canonicalTarget))
		{
			KalaDataCore::PrintMessage(
				"Target '" + canonicalTarget + "' must be a directory!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		string targetParentFolder = path(canonicalTarget).parent_path().string();
		if (!CanWriteToFolder(targetParentFolder))
		{
			KalaDataCore::PrintMessage(
				"Unable to write to target parent directory '" + targetParentFolder + "'!\n",
				MessageType::MESSAGETYPE_ERROR);

			return;
		}

		Archive::Decompress(canonicalOrigin, canonicalTarget);
	}

	void Command::Command_Exit()
	{
		KalaDataCore::Shutdown();
	}
}

uint64_t GetFolderSize(const string& folderPath)
{
	uint64_t size{};

	for (auto& p : recursive_directory_iterator(folderPath))
	{
		if (is_regular_file(p)) size += file_size(p);
	}

	return size;
}

bool CanWriteToFolder(const string& folderPath)
{
	try
	{
		path testFile = path(folderPath) / ".kaladata_write_access_test";
		ofstream writeTest(testFile.string(), ios::out | ios::trunc);

		if (!writeTest.is_open()) return false;

		writeTest << "test";
		writeTest.close();

		remove(testFile);

		return true;
	}
	catch (...)
	{
		return false;
	}
}

string ConvertSizeToString(uint64_t size)
{
	ostringstream ss{};

	ss << fixed
		<< setprecision(2)
		<< static_cast<double>(size) / (1024ull * 1024 * 1024)
		<< "GB";

	return ss.str();
}

string ResolvePath(
	const string& origin,
	bool checkExistence)
{
	path resolved{};

	//always set current path if unset
	if (currentPath.empty()) currentPath = current_path().string();

	if (checkExistence)
	{
		//default full path
		if (exists(origin)) resolved = path(origin);

		//path relative to KalaData root
		else if (exists(path(current_path()) / origin))
		{
			resolved = path(current_path()) / origin;
		}

		//path relative to virtual current path
		else if (exists(path(currentPath) / origin))
		{
			resolved = path(currentPath) / origin;
		}

		//path is invalid
		else
		{
			KalaDataCore::PrintMessage(
				"Path '" + origin + "' does not exist!\n",
				MessageType::MESSAGETYPE_ERROR);

			return "";
		}
	}
	else
	{
		//construct without validation
		resolved = path(origin).is_absolute()
			? path(origin)
			: path(currentPath) / origin;
	}

	if (KalaDataCore::IsVerboseLoggingEnabled())
	{
		KalaDataCore::PrintMessage(
			"Resolved to path '" + resolved.string() + "'!\n");
	}

	return weakly_canonical(resolved).string();
}