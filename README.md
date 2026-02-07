# BlockStore

A schema-free storage framework in C++ that supports storage and retrieval of custom objects and data structures.

## An Example

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
