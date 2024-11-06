
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <stdio.h>
#include <string.h>

static std::map<std::string, int> occurences;
static std::vector<std::string> ordered_calls;

extern "C" void function_called(const char *name)
{
	std::string _name(name);
	int count = 1;
	if (occurences.find(_name) != occurences.end())
	{
		count += occurences[_name];
	}
	else
	{
		ordered_calls.push_back(_name);
	}

	occurences[_name] = count;
}

extern "C" void functions_dump()
{
	char out[1024];
	std::ofstream file("called_functions.txt");

	if (file.is_open())
	{
		//auto it = occurences.begin();
		//for (; it != occurences.end(); it++)
		//{
		//	file.write(it->first.c_str(), it->first.size());
		//	file.write("\n", strlen("\n"));
		//}
		auto it = ordered_calls.begin();
		for (; it != ordered_calls.end(); it++)
		{
			snprintf(out, sizeof(out), "%s -> %d\n", it->c_str(), occurences.at(*it));
			file.write(out, strnlen(out, sizeof(out)));
		}

		file.close();
	}
}