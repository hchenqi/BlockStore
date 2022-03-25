#include "BlockStore/file_manager.h"


using namespace BlockStore;


int main(int argc, char* argv[]) {
	if (argc == 2) {
		FileManager file(argv[1]);
		file.StartGarbageCollection();
	}
	return 0;
}
