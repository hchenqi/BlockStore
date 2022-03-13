#include "BlockStore/file_manager.h"


using namespace BlockStore;


int main() {
	FileManager file("file_test.db");
	file.GetMetadata().gc_phase = GcPhase::Sweep;
	file.MetadataUpdated();
	return 0;
}