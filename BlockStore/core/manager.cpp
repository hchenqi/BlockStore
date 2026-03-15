#include "manager.h"
#include "db.h"


namespace BlockStore {


BlockManager::BlockManager(const char file[]) : db(std::make_unique<DB>(file)) {}

BlockManager::~BlockManager() {}

block_ref BlockManager::get_root() { return block_ref(*this, db->get_root()); }

block_ref BlockManager::allocate() { return block_ref(*this, db->allocate()); }

void BlockManager::inc_ref(ref_t ref) { return db->inc_ref(ref); }

void BlockManager::dec_ref(ref_t ref) { return db->dec_ref(ref); }

std::vector<std::byte> BlockManager::read(ref_t ref) const { return db->read(ref); }

void BlockManager::write(ref_t ref, const std::vector<std::byte>& data, const std::vector<ref_t>& ref_list) { return db->write(ref, data, ref_list); }

void BlockManager::begin_transaction() { db->BeginTransaction(); }

void BlockManager::commit() { db->Commit(); }

void BlockManager::rollback() { db->Rollback(); }

const GCInfo& BlockManager::get_gc_info() { return db->get_gc_info(); }

void BlockManager::gc(const GCOption& option) { return db->gc(option); }


} // namespace BlockStore
