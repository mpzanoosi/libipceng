#include <stdio.h>
#include <string.h>
#include "ipceng.h"

int qdoor_test1()
{
	struct ipceng *eng1 = ipceng_init("eng1");
	
	if (ipceng_qdoor_add_simple(eng1, "eng2") != 0) {
		printf("eng1 error: %s\n", ipceng_errmsg(eng1));
		return 0;
	}

	if (ipceng_qdoor_send_simple(eng1, "eng2", "hello world!") != 0) {
		printf("eng1 error: %s\n", ipceng_errmsg(eng1));
		return 0;
	}

	struct ipceng *eng2 = ipceng_init("eng2");

	if (ipceng_qdoor_add_simple(eng2, "eng1") != 0) {
		printf("eng2 error: %s\n", ipceng_errmsg(eng2));
		return 0;
	}

	char *new_msg;

	if (ipceng_qdoor_recv_simple(eng2, "eng1", &new_msg) != 0) {
		printf("eng2 error: %s\n", ipceng_errmsg(eng2));
		return 0;
	}

	printf("received message in eng2 (from eng1 qdoor): %s\n", new_msg);
	free(new_msg);

	return 0;
}

int qdoor_test2()
{
	struct ipceng *eng1 = ipceng_init("eng1");
	struct ipceng *eng2 = ipceng_init("eng2");
	
	if (ipceng_qdoor_add_simple(eng1, "eng2") != 0) {
		printf("eng1 error: %s\n", ipceng_errmsg(eng1));
		return 0;
	}
	if (ipceng_qdoor_add_simple(eng2, "eng1") != 0) {
		printf("eng2 error: %s\n", ipceng_errmsg(eng2));
		return 0;
	}

	char *new_msg;
	int i, try_count = 10;
	for (i = 0; i < try_count; i++) {
		if (ipceng_qdoor_send_simple(eng1, "eng2", "hello world!") != 0) {
			printf("eng1 error: %s\n", ipceng_errmsg(eng1));
			continue;
		}
		if (ipceng_qdoor_recv_simple(eng2, "eng1", &new_msg) != 0) {
			printf("eng2 error: %s\n", ipceng_errmsg(eng2));
			continue;
		}
		printf("received message in eng2 (from eng1 qdoor): %s\n", new_msg);
		free(new_msg);
	}

	return 0;
}

int shm_test1()
{
	struct ipceng *eng1 = ipceng_init("eng1");
	struct ipceng *eng2 = ipceng_init("eng2");
	int shm_size = 100;
	
	if (ipceng_shm_add(eng1, "loloshm",shm_size) != 0) {
		printf("eng1 error: %s\n", ipceng_errmsg(eng1));
		return 0;
	}
	if (ipceng_shm_add(eng2, "loloshm", shm_size) != 0) {
		printf("eng2 error: %s\n", ipceng_errmsg(eng2));
		return 0;
	}

	int i, try_count = 10;
	char *data, *buff;
	size_t addr, size;
	for (i = 0; i < try_count; i++) {
		data = "hello world!";
		addr = 0;
		size = strlen(data);
		if (ipceng_shm_write(eng1, "loloshm", data, addr, size) != 0) {
			printf("eng1 error: %s\n", ipceng_errmsg(eng1));
			continue;
		}
		if (ipceng_shm_read(eng2, "loloshm", &buff, addr, size) != 0) {
			printf("eng2 error: %s\n", ipceng_errmsg(eng2));
			continue;
		}
		printf("eng2 reading shared memory at address %ld: %s\n", addr, buff);
		free(buff);
	}

	return 0;
}

int main(int argc, char const *argv[])
{
	// qdoor_test1();
	// qdoor_test2();
	shm_test1();
	return 0;
}