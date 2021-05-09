# libipceng
Library for Linux Inter-Process Communication (IPC): simultaneous, asynchronous, and based Linux kernel doubly linked-list.
Both posix message queue and shared memory models are handled and can be used.

simultaneous: two processes can send data to each other at the same time\
asynchronous: there is no need to synchronous send-receive communication models

This is actually just a more beautiful wrapper around posix message queue and probably simpler!

# Build
```
mkdir build
cd build
cmake ..
make
```

## Install
```
cmake -DCMAKE_INSTALL_PREFIX=/path/to/dir/ ..
make
make install
```

# Test
```
mkdir build
cd build
cmake -DTEST=ON ..
./tests/test_ipceng
```