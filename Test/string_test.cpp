#include "BlockStore/block_manager.h"
#include "CppSerialize/stl/string.h"

#include <iostream>


using namespace BlockStore;


int main() {
	block_manager.open_file("data.db");
	block<std::string> root = block_manager.get_root();
	std::cout << root.read() << std::endl;
	root.write("Hello world!");
	std::cout << root.read() << std::endl;
	return 0;
}
