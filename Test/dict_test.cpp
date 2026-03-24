#include "BlockStore/Item/OrderedRefMap.h"
#include "CppSerialize/stl/string.h"

#include <iostream>
#include <cctype>


using namespace BlockStore;


std::vector<std::string> tokenize_command(const std::string& input) {
	std::vector<std::string> tokens;
	size_t i = 0;
	while (i < input.size()) {
		while (i < input.size() && std::isspace(static_cast<unsigned char>(input[i]))) {
			i++;
		}
		if (i >= input.size()) {
			break;
		}
		if (input[i] == '"') {
			i++;
			size_t start = i;
			while (i < input.size() && input[i] != '"') {
				i++;
			}
			tokens.emplace_back(input.substr(start, i - start));
			if (i < input.size() && input[i] == '"') {
				i++;
			}
		} else {
			size_t start = i;
			while (i < input.size() && !std::isspace(static_cast<unsigned char>(input[i]))) {
				i++;
			}
			tokens.emplace_back(input.substr(start, i - start));
		}
	}
	return tokens;
}


void print(const auto& container) {
	for (auto i : container) {
		std::cout << i.first.read() << ": " << i.second.read() << std::endl;
	}
	std::cout << std::endl;
}


template<class T>
struct CacheType {
	using Type = BlockCacheDynamicAdapter<T>;
};

template<>
struct CacheType<std::string> {
	using Type = BlockCache<std::string>;
};

template<class T>
using Cache = CacheType<T>::Type;


int main() {
	BlockManager block_manager("dict_test.db");

	BlockCacheDynamic node_cache(block_manager);
	BlockCacheDynamic leaf_cache(block_manager);
	BlockCache<std::string> key_cache(block_manager);
	BlockCache<std::string> value_cache(block_manager);

	OrderedRefMap<std::string, std::string, Cache> map(node_cache, leaf_cache, key_cache, value_cache, block_manager.get_root());

	for (;;) {
		std::cout << "> ";

		std::string command;
		if (!std::getline(std::cin, command)) {
			break;
		}

		auto arg = tokenize_command(command);
		if (arg.empty()) {
			continue;
		}

		try {
			if (arg[0] == "exit" || arg[0] == "quit") {
				break;
			} else if (arg[0] == "insert" || arg[0] == "i") {
				if (arg.size() != 3) {
					throw std::invalid_argument("insert requires \"key\" \"value\"");
				}
				map.insert(arg[1], arg[2]);
				std::cout << "inserted\n";
			} else if (arg[0] == "delete" || arg[0] == "d") {
				if (arg.size() != 2) {
					throw std::invalid_argument("delete requires \"key\"");
				}
				map.erase(arg[1]);
				std::cout << "deleted\n";
			} else if (arg[0] == "get" || arg[0] == "g") {
				if (arg.size() != 2) {
					throw std::invalid_argument("get requires \"key\"");
				}
				std::cout << map.at(arg[1]).get() << std::endl;
			} else if (arg[0] == "set" || arg[0] == "s") {
				if (arg.size() != 3) {
					throw std::invalid_argument("set requires \"key\" \"value\"");
				}
				map.at(arg[1]).set(arg[2]);
				std::cout << "updated\n";
			} else if (arg[0] == "list" || arg[0] == "l") {
				print(map);
			} else {
				std::cout << "unknown command: " << arg[0] << std::endl;
				std::cout << "commands: insert \"key\" \"value\", delete \"key\", get \"key\", set \"key\" \"value\", list, exit" << std::endl;
			}
		} catch (const std::exception& e) {
			std::cerr << "error: " << e.what() << std::endl;
		}
	}

	return 0;
}
