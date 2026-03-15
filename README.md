# BlockStore

A schema-free storage framework in C++.

## Usage Example

From Test/file_test.cpp:

```c++
BlockManager block_manager("file_test.db");
block<std::string> root = block_manager.get_root();
root.write("Hello world!");
std::cout << root.read() << std::endl;
```

## Building Steps

- Install the following software and tools:
  - Visual Studio with C++ and CMake
  - Ninja

- Put these dependencies in the same parent folder:
  - [CppSerialize](https://github.com/hchenqi/CppSerialize.git)
  - [SQLite3Helper](https://github.com/hchenqi/SQLite3Helper.git)

- Configure and build with CMake

## Design

Rather than a relational database which must have a fixed schema with tables, rows and indexes, I want to have a more flexible database system supporting arbitrary item types and data structures.

Following a natural principle, that each storage unit should have an upper size limit, a large item must be split into small data blocks, which are then managed by references in common data structures like list, tree and graph. With referencing of arbitrary items, garbage collection is also needed for optimizing storage space after deletion.

For example, each row in a table of a relational database could be regarded as an item, and each index also an item which stores an ordered map of keys to rows. The ordered map could be explicitly implemented using a tree where each node stores references to child nodes or actual data within a fixed size.

### Concepts

#### Block

As the storage unit, a block has an upper size limit (4096 bytes for example), and each block can be accessed by a unique reference. Complex data structures are built on blocks by referencing.

> As a comparison, a common file system or a database manages blocks and B-Tree or similar structures internally, but here blocks and references are exposed for users to build custom data structures.

#### Item

An item is a logical concept that could have arbitrary type, size or sub-items. It can be actually stored in one or multiple blocks organized in some data structure, complying with the size limit of a block.

A large item spanning multiple blocks can be referenced by the reference of its root block. And a small item can be stored as a property of a parent item along with other properties in one block, without having an own reference exposed for others.

> For example, a list can be regarded as an item that has child items as its elements. The list contains a root (sentinel) block and for each child item, an element block to store the item as well as references to previous and next element blocks. A child item can be stored inline in the element block if it's small enough to fit in and not going to be referenced by other items, or it can be stored in separate blocks while the element block stores only its reference.

## Implementation

### Storage Backend

Ideally, we only need a backend that can allocate and modify blocks. In this project, I'm using SQLite, a relational database, as backend simulating this behaviour with a single table and to provide transaction safety.

Each block is stored as a row in `BLOCK` table with integer primary key `id` as its reference and blob field `data` for its data. Additional columns and tables are used for garbage collection.

The backend is wrapped in `BlockManager` class, which provides interfaces for creating blocks, reading/writing block data by reference with transactions, and garbage collection.

### Garbage Collection

This project implements mark-and-sweep garbage collection. The `root` block is created when the database is initialized, and all data will be eventually referenced by the `root` block in some data structure. Each block stores its own data as well as references to other blocks. During garbage collection, the root block itself and blocks that are directly or indirectly referenced by the root block will be scanned and marked as active, and the remaining blocks are then deleted.

Table `BLOCK` contains a boolean field `gc` as the mark and a blob field `ref` storing the list of references for each block. Table `SCAN` stores references of the next blocks to be searched. The reference of the root block is first inserted in table `SCAN`, and in each loop, some references will be fetched from table `SCAN` and those referenced blocks that are unmarked will be marked and their lists of references will be then inserted to table `SCAN`. When table `SCAN` becomes empty, all items unmarked are deleted. The mark is flipped at each round of garbage collection.

> There can be duplications of references in table `SCAN` which could make table `SCAN` grow uncontrollably. Therefore, table `SCAN` can store the references uniquely, or it can be replaced by a partial index on table `BLOCK`.

The reference of the root block, the mark and the progress of garbage collection are stored in a single row in table `META`. Scanning and sweeping are implemented batch-wise with a callback after each batch, so that garbage collection can be interrupted. The mechanism described above assumes no blocks are created or modified during garbage collection, otherwise, special procedures are applied.

### Block Creation/Read/Write

A block is created through `BlockManager` without initial data and its reference is returned as `block_ref`. With `block_ref` we can read and write the data of the block. A `block_ref` itself can also be encoded as data and stored in a block.

Block creation and write operations can be grouped in transactions.

`BlockManager` maintains a set of active references, which include reference to the root block, references to blocks just created, references to blocks being read and references decoded from the data of a block. Each entry in the set also keeps the number of `block_ref` instances. The entry is removed from the set when the number becomes 0.

When garbage collection begins, all active references in the set are added to table `SCAN`. During scanning, references newly added to the set will also be added to table `SCAN`.

> This ensures no dangling reference exists after garbage collection. A dangling reference could only appear when a block actually referenced is not marked and thus deleted, and this only happens when a block already marked during garbage collection is updated with new references to blocks never going to be marked. But this is not the case because new references will always be marked.

> We need to add all new references in the set to table `SCAN`, not just the ones referenced by a block already marked which is being updated, because all active references in the set can potentially be referenced by some block later. Before sweeping, they must be either marked or removed from the set.

### Data

The data of a block is stored in binary form as an array of bytes with a maximum of 4096 bytes.

> As an optimization, the limit can be lowered so that the data and additional information of a row stored in SQLite doesn't take more than one actual page.

The interpretation of the data is defined by user. Therefore, when updating the data, the list of references must be explicitly provided for garbage collection.

A class template `block<T>` extends `block_ref` for reading and writing blocks in custom type `T` using the serialization framework `CppSerialize`. It also handles the serialization and deserialization of `block_ref` automatically.

### Cache

A block might be accessed frequently or shared by multiple items. To avoid querying the database every time while maintaining the consistency of the data shared, especially for data structures like list, a cache storing deserialized blocks is provided as `BlockCache`.

> `BlockManager` only provides the raw block data read and write interfaces, keeps a set of active references, but doesn't store the data. `BlockCache` is built on `BlockManager` that stores a map from active block references to deserialized block data objects in their own types.

## Advanced

### Dynamic Typing

### Copy-On-Write

### Merge
