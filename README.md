# libipceng
Library for Linux Inter-Process Communication (IPC): simultaneous, asynchronous, and based Linux kernel doubly linked-list.
Both posix message queue and shared memory models are handled and can be used.

simultaneous: two processes can send data to each other at the same time\
asynchronous: there is no need to synchronous send-receive communication models

This is actually just a more beautiful wrapper around posix message queue and probably simpler!
