#pragma once

#include "block.h"


BEGIN_NAMESPACE(BlockStore)


class BlockManager {
public:
	static void open_file(const char file[]);

public:
	static block_ref get_root();

	// transaction
protected:
	static void begin_transaction();
	static void commit();
	static void rollback();
public:
	static decltype(auto) transaction(auto f) {
		begin_transaction();
		try {
			if constexpr (std::is_void_v<std::invoke_result_t<decltype(f)>>) {
				f();
				commit();
			} else {
				decltype(auto) res = f();
				commit();
				return res;
			}
		} catch (...) {
			rollback();
			throw;
		}
	}

	// gc
public:
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
		uint64 max_index = 0;
		uint64 sweeping_index = 0;
	};

	class GCCallback {
	public:
		using GCInfo = GCInfo;
	public:
		virtual void Notify(const GCInfo& info) {}
		virtual bool Interrupt(const GCInfo& info) { return false; }
	};

private:
	static GCCallback default_gc_callback;

public:
	static const GCInfo& get_gc_info();
	static void gc(GCCallback& callback = default_gc_callback);
};

static constexpr BlockManager block_manager;


END_NAMESPACE(BlockStore)