#include "ref.h"
#include "manager.h"

#include <stdexcept>


namespace BlockStore {


block_ref::block_ref() : manager(nullptr), ref(0) {}

block_ref::block_ref(BlockManager& manager, ref_t ref) : manager(&manager), ref(ref) { this->manager->inc_ref(ref); }

block_ref::block_ref(block_ref&& other) : manager(other.manager), ref(other.ref) { other.manager = nullptr; other.ref = 0; }

block_ref::block_ref(const block_ref& other) : manager(other.manager), ref(other.ref) { if (manager != nullptr) { manager->inc_ref(ref); } }

block_ref& block_ref::operator=(block_ref&& other) { if (manager != nullptr) { manager->dec_ref(ref); } manager = other.manager; ref = other.ref; other.manager = nullptr; other.ref = 0; return *this; }

block_ref& block_ref::operator=(const block_ref& other) { if (manager != nullptr) { manager->dec_ref(ref); } manager = other.manager; ref = other.ref; if (manager != nullptr) { manager->inc_ref(ref); } return *this; }

block_ref::~block_ref() { if (manager != nullptr) { manager->dec_ref(ref); } }

void block_ref::check() const { if (manager == nullptr) { throw std::invalid_argument("block_ref uninitialized"); } }

std::vector<std::byte> block_ref::read() const { check(); return manager->read(ref); }

void block_ref::write(const std::vector<std::byte>& data, const std::vector<ref_t>& ref_list) { check(); return manager->write(ref, data, ref_list); }


} // namespace BlockStore
