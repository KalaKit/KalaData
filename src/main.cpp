//Copyright(C) 2025 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include <string>
#include <sstream>
#include <iterator>
#include <vector>
#include <iostream>

#include "command.hpp"
#include "core.hpp"

using KalaData::Core::KalaDataCore;
using KalaData::Core::Command;

using std::string;
using std::cout;
using std::cin;
using std::getline;
using std::istringstream;
using std::istream_iterator;
using std::vector;

int main(int argc, char* argv[])
{
	if (argc == 1)
	{
		string input;
		cout << KALADATA_VERSION "\n";
		cout << "Type '--help' to list all commands\n";

		cout << "KalaData> ";
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
	else
	{
		vector<string> commands(argv, argv + argc);
		Command::HandleCommand(commands);
	}

	KalaDataCore::Update();
}