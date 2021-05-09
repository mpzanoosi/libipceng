# libipceng
Library for Linux Inter-Process Communication (IPC): a more beautiful wrapper around posix message queue and shm, and probably simpler!

![alt text](https://github.com/mpzanoosi/libipceng/blob/master/help.jpg?raw=true)

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
