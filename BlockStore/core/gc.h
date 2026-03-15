#pragma once

#include "type.h"

#include <stdexcept>
#include <functional>


namespace BlockStore {


enum GCPhase : unsigned char {
	Idle,
	Scanning,
	Sweeping
};

struct GCInfo {
	bool mark = false;
	GCPhase phase = GCPhase::Idle;
	uint64 block_count_prev = 0;
	uint64 block_count = 0;
	uint64 block_count_marked = 0;
	uint64 max_id = 0;
	uint64 sweeping_id = 0;
};


struct GCOption {
	std::function<bool(const GCInfo&)> callback = [](const GCInfo&) { return false; };

	uint64 scan_step_depth = 64;
	uint64 scan_changes_limit = 16 * 1024;
	uint64 scan_batch_size = 256;
	uint64 delete_batch_size = 256 * 1024;

	constexpr void check() const {
		if (scan_step_depth > 0 && scan_changes_limit > 0 && scan_batch_size > 0 && delete_batch_size > 0) { return; }
		throw std::invalid_argument("invalid gc option");
	}
};


} // namespace BlockStore
