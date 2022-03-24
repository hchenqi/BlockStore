#include "BlockStore/file_manager.h"


using namespace BlockStore;


int main() {
	FileManager file("file_test.db");
	file.SetRootIndex(block_index_invalid);
	return 0;
}