# My redis

This repository aims to implement an in-memory key-value store, primarily used for caching. The project is written in C++ to allow efficient implementation of fundamental data structures, such as hash tables.

## How does it work

To add a new pair you need to first build the executables with cmake 
``` 
mkdir build && cd build
cmake ..
make
./server
```
After running these commands open a new terminal and run the executable client. In this executable you can enter the parameters to set get or delete a new value. For example:
```
./client set x 1
./client get x
./client del x
```
You can run the commands separately and see how the client adds a new key, gets the value 1 with `get x` and the deletes it (try to search it again with `get x` and you won't obtain any value)