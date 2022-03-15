#pragma once

#include "core.h"


BEGIN_NAMESPACE(BlockStore)


enum GcPhase : uchar { Idle, Scan, Sweep };


struct Metadata {
	data_t root_index;
	bool gc_mark;
	GcPhase gc_phase;
};


END_NAMESPACE(BlockStore)