
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <stdio.h>
#include <string.h>

static std::map<std::string, int> occurences;

extern "C" void function_called(const char *name)
{
	std::string _name(name);
	int count = 1;
	if (occurences.find(_name) != occurences.end())
	{
		count += occurences[_name];
	}

	occurences[_name] = count;
}

extern "C" void functions_dump()
{
	std::ofstream file("called_functions.txt");

	if (file.is_open())
	{
		auto it = occurences.begin();
		for (; it != occurences.end(); it++)
		{
			file.write(it->first.c_str(), it->first.size());
			file.write("\n", strlen("\n"));
		}

		file.close();
	}
}