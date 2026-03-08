# BlockStore

A schema-free storage framework in C++.

## Usage Example

From Test/string_test.cpp:

```c++
block_manager.open_file("data.db");
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

As the storage unit, a block has an upper size limit (4096 bytes for example), and each block can be accessed by a unique reference. Complex data structures are built on blocks with referencing.

> As a comparison, a common file system or a database manages blocks and B-Tree or similar structures internally, but here blocks and references are exposed for users to build custom data structures.

#### Item

An item is a logical concept that could have arbitrary type, size or sub-items. It can be actually stored in one or multiple blocks organized in some data structure, complying with the size limit of a block.

A large item spanning multiple blocks can be referenced by the reference of its root block. And a small item can be stored as a property of a parent item along with other properties in one block, without having an own reference exposed for others.

For example, a list can be regarded as an item that has child items as its elements. The list contains a root (sentinel) block and for each child item, an element block to store the item as well as references to previous and next element blocks. A child item can be stored inline in the element block if it's small enough to fit in and not going to be referenced by other items, or it can be stored in separate blocks while the element block stores only its reference.

## Implementation

### Storage Backend

Ideally, we only need a backend that can allocate and modify blocks. In this project, I'm using SQLite, a relational database, as backend simulating this behaviour with a single table and to provide transaction safety.

Each block is stored as a row in `BLOCK` table with integer primary key `id` as its reference. Additional columns and tables are used for garbage collection.

### Garbage Collection

This project implements mark-and-sweep garbage collection. The `root` block is created when the database is initialized, and all data will be eventually referenced by the `root` block in some data structure. Each block stores its own data as well as references to other blocks. During garbage collection, the root block itself and blocks that are directly or indirectly referenced by the root block will be scanned and marked as active, and the remaining blocks are then deleted.

Table `BLOCK` contains a boolean field `gc` as the mark and a blob field `ref` storing the list of references for each block. Table `SCAN` stores references of the next blocks to be searched. The reference of the root block is first inserted in table `SCAN`, and in each loop, some references will be fetched from table `SCAN` and those referenced blocks that are unmarked will be marked and their lists of references will be then inserted to table `SCAN`. When table `SCAN` becomes empty, all items unmarked are deleted. The mark is flipped at each round of garbage collection.

The reference of the root block, the mark and the progress of garbage collection are stored in a single row in table `META`. Scanning and sweeping are implemented batch-wise with a callback after each batch, so that garbage collection can be interrupted. The mechanism described above assumes no blocks are created or modified during garbage collection, otherwise, special procedures are applied.

### Block Creation/Read/Write

A block is created with empty data and the reference is returned. With the reference, we can read and write the data of the block.

> Another design: a block must be created with some initial data before we can get its reference. Then the child blocks it references must be created first, and thus no circular references can be made directly. And the root block will also have to be properly initialized, whose reference will then have to be able to update. It's actually a typing problem whether a block could possibly be empty.

### Serialization

### Cache

## Advanced Topics

### Typing

### Copy-On-Write

### Merge
