//Copyright(C) 2025 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include <vector>
#include <string>

#include "KalaHeaders/core_types.hpp"
#include "KalaHeaders/logging.hpp"

namespace KalaData::Core
{
	using KalaHeaders::LogType;

	using std::vector;
	using std::string;

	class Command
	{
	public:
		//Useful for temporarily stopping detection of new commands
		static void SetCommandAllowState(bool newState) { canAllowCommands = newState; }
		static bool GetCommandAllowState() { return canAllowCommands; }

		//Controls how a command is handled
		static void HandleCommand(vector<string> parameters);

		//Print version to console
		static void Command_Version();

		//Print KalaData description to console
		static void Command_About();

		//List all commands in console
		static void Command_Help();

		//Print info about selected command in console
		static void Command_Help_Command(const string& commandName);

		//Navigate to your chosen directory, compress/decompress
		//can handle files and directories relative to it
		static void Command_Go(const string& target);

		//Navigate to system root directory
		static void Command_Root();

		//Navigate to KalaData root directory
		static void Command_Home();

		//Prints your current path (KalaData root or the one set with --go)
		static void Command_Where();

		//Lists all files and directories in your current path (KalaData root or the one set with --go)
		static void Command_List();

		//Creates a new directory at the chosen path
		static void Command_Create(const string& target);

		//Deletes the file or directory at the chosen path (asks for confirmation before permanently deleting)
		static void Command_Delete(const string& target);

		//Set compression mode to chosen value
		static void Command_SetCompressionMode(const string& mode);

		//Toggles compression verbose messages on and off
		static void Command_ToggleCompressionVerbosity();

		//Compression pre-checks
		static void Command_Compress(
			const string& origin,
			const string& target);

		//Decompression pre-checks
		static void Command_Decompress(
			const string& origin,
			const string& target);

		//Shuts down KalaData
		static void Command_Exit();
	private:
		static inline bool canAllowCommands = true;
	};
}