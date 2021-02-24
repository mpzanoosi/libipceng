#include "ipceng.h"
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <time.h>

// macros
#ifndef free_safe
#define free_safe(ptr) do{ free(ptr); (ptr)=NULL; } while(0)
#endif

// internal helper functions
static int _read_procfile_oneline(char *file_name, char **buff)
{
	if (!file_name || !buff)
		return -1;

	*buff = NULL;
	FILE *fp = fopen(file_name, "r");
	if (fp == NULL)
		return -1;

	*buff = malloc(1024);
	// reading just first line
	if (fgets(*buff, 1024, fp) == NULL) {
		free_safe(*buff);
		fclose(fp);
		return -1;
	}

	fclose(fp);
	return 0;
}

// internal structures/enums
enum ipcstate
{
	IPC_ENTITY_OPENED,
	IPC_ENTITY_CLOSED
};

struct mqwrap
{
	char *name;
	int timeout;
	int oflags;
	struct mq_attr attr;
	mqd_t mqd;
	enum ipcstate state;
};

struct qdoor
{
	char *name;
	// embedded message queues descriptors and names
	struct mqwrap sendq;
	struct mqwrap recvq;
	// internal qdoor linked list member
	struct list_head _list;
};

struct shm
{
	char *name;
	int shmd;
	int oflag;
	mode_t mode;
	size_t size;
	enum ipcstate state;
	// internal pointer to hold output of mmap
	void *ptr;
	// internal shm linked list member
	struct list_head _list;
};

// main functin implementation
struct ipceng *ipceng_init(char *name)
{
	if (!name)
		return NULL;

	struct ipceng *new_obj = (struct ipceng *)malloc(sizeof(struct ipceng));
	new_obj->name = strdup(name);
	new_obj->has_logging = true;
	new_obj->err_code = 0;
	new_obj->err_msg = strdup("no error");
	INIT_LIST_HEAD(&new_obj->qdoor_list);
	new_obj->qdoor_count = 0;
	INIT_LIST_HEAD(&new_obj->shm_list);
	new_obj->shm_count = 0;
	INIT_LIST_HEAD(&new_obj->_list);

	return new_obj;
}

void ipceng_set_error(struct ipceng *obj, int _errno, char *_errmsg)
{
	obj->err_code = _errno;
	free_safe(obj->err_msg);
	obj->err_msg = strdup(_errmsg);
}

int ipceng_term(struct ipceng *obj)
{
	if (ipceng_qdoor_close_all(obj) != 0) {
		ipceng_set_error(obj, IPCENG_ERR_TERM, \
			"terminating ipceng object failed: terminating qdoors failed");
		return -1;
	}
	free_safe(obj->name);
	free_safe(obj->err_msg);
	free_safe(obj);

	ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
	return 0;
}

int ipceng_log_enable(struct ipceng *obj)
{
	obj->has_logging = true;
	ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
	return 0;
}

int ipceng_log_disable(struct ipceng *obj)
{
	obj->has_logging = false;
	ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
	return 0;
}

int ipceng_errno(struct ipceng *obj)
{
	return obj->err_code;
}

char *ipceng_errmsg(struct ipceng *obj)
{
	return obj->err_msg;
}

int ipceng_qdoor_add(struct ipceng *obj,
	char *qdoor_name,
	int msg_maxcount,
	int msg_maxsize,
	int timeout_send,
	int timeout_recv)
{
	// qdoor should not be added before
	struct qdoor *iter;
	list_for_each_entry(iter, &obj->qdoor_list, _list) {
		if (!strcmp(iter->name, qdoor_name)) {
			ipceng_set_error(obj, IPCENG_ERR_QDOORADD, \
				"failed to add qdoor: qdoor has been added already");
			return -1;
		}
	}

	int target_msgmaxcount = (msg_maxcount == -1) ? IPCENG_DAFAULT_MSGCOUNT : msg_maxcount;
	int target_msgmaxsize = (msg_maxsize == -1) ? IPCENG_DAFAULT_MSGSIZE : msg_maxsize;
	char *buff;

	// check if target_msgmaxcount is greater than linux setting (/proc)
	if (_read_procfile_oneline("/proc/sys/fs/mqueue/msg_max", &buff) != 0) {
		ipceng_set_error(obj, IPCENG_ERR_QDOORADD, \
			"failed to add qdoor: can't read /proc/sys/fs/mqueue/msg_max");
		return -1;
	}
	if (target_msgmaxcount > atoi(buff)) {
		ipceng_set_error(obj, IPCENG_ERR_QDOORADD, \
			"failed to add qdoor: msg_maxcount is greater than linux setting /proc/sys/fs/mqueue/msg_max");
		free_safe(buff);
		return -1;
	}
	free_safe(buff);

	// check if target_msgmaxsize is greater than linux setting (/proc)
	if (_read_procfile_oneline("/proc/sys/fs/mqueue/msgsize_max", &buff) != 0) {
		ipceng_set_error(obj, IPCENG_ERR_QDOORADD, \
			"failed to add qdoor: can't read /proc/sys/fs/mqueue/msgsize_max");
		return -1;
	}
	if (target_msgmaxsize > atoi(buff)) {
		ipceng_set_error(obj, IPCENG_ERR_QDOORADD, \
			"failed to add qdoor: msg_maxcount is greater than linux setting /proc/sys/fs/mqueue/msgsize_max");
		free_safe(buff);
		return -1;
	}
	free_safe(buff);

	// creating new_qdoor object
	struct qdoor *new_qdoor = (struct qdoor *)malloc(sizeof(struct qdoor));
	new_qdoor->name = strdup(qdoor_name);
	// filling sendq and recvq
	int mqnames_len = strlen("/2.mq") + strlen(obj->name) + strlen(qdoor_name) + 1;
	// filling sendq
	new_qdoor->sendq.name = (char *)malloc(mqnames_len);
	sprintf(new_qdoor->sendq.name, "/%s2%s.mq", obj->name, qdoor_name);
	new_qdoor->sendq.timeout = timeout_send;
	new_qdoor->sendq.oflags = (timeout_send > 0) ? (O_CREAT | O_WRONLY) : \
		(O_CREAT | O_WRONLY | O_NONBLOCK);
	new_qdoor->sendq.attr.mq_flags = 0;
	new_qdoor->sendq.attr.mq_maxmsg = target_msgmaxcount;
	new_qdoor->sendq.attr.mq_msgsize = target_msgmaxsize;
	new_qdoor->sendq.attr.mq_curmsgs = 0;
	new_qdoor->sendq.mqd = mq_open(new_qdoor->sendq.name, new_qdoor->sendq.oflags, \
		0664, &new_qdoor->sendq.attr);
	if (new_qdoor->sendq.mqd == (mqd_t)-1) {
		ipceng_set_error(obj, IPCENG_ERR_QDOORADD, \
			"failed to add qdoor: unable to open sending mq");
		free_safe(new_qdoor->sendq.name);
		free_safe(new_qdoor);
		return -1;
	}
	// filling recvq
	new_qdoor->recvq.name = (char *)malloc(mqnames_len);
	sprintf(new_qdoor->recvq.name, "/%s2%s.mq", qdoor_name, obj->name);
	new_qdoor->recvq.timeout = timeout_recv;
	new_qdoor->recvq.oflags = (timeout_recv > 0) ? (O_CREAT | O_RDONLY) : \
		(O_CREAT | O_RDONLY | O_NONBLOCK);
	new_qdoor->recvq.attr.mq_flags = 0;
	new_qdoor->recvq.attr.mq_maxmsg = target_msgmaxcount;
	new_qdoor->recvq.attr.mq_msgsize = target_msgmaxsize;
	new_qdoor->recvq.attr.mq_curmsgs = 0;
	new_qdoor->recvq.mqd = mq_open(new_qdoor->recvq.name, new_qdoor->recvq.oflags, \
		0664, &new_qdoor->recvq.attr);
	if (new_qdoor->recvq.mqd == (mqd_t)-1) {
		ipceng_set_error(obj, IPCENG_ERR_QDOORADD, \
			"failed to add qdoor: unable to open receiving mq");
		// if can't open recvq, then should remove sendq as well
		mq_unlink(new_qdoor->sendq.name);
		free_safe(new_qdoor->sendq.name);
		free_safe(new_qdoor->recvq.name);
		free_safe(new_qdoor);
		return -1;
	}
	new_qdoor->sendq.state = IPC_ENTITY_OPENED;
	new_qdoor->recvq.state = IPC_ENTITY_OPENED;
	// adding new_qdoor into obj
	list_add_tail(&new_qdoor->_list, &obj->qdoor_list);
	obj->qdoor_count++;

	ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
	return 0;
}

void _ipceng_qdoor_del_by_entry(struct qdoor *qd)
{
	mq_unlink(qd->sendq.name);
	free_safe(qd->sendq.name);
	mq_unlink(qd->recvq.name);
	free_safe(qd->recvq.name);
	free_safe(qd->name);
	list_del(&qd->_list);
	free_safe(qd);
}

int ipceng_qdoor_del(struct ipceng *obj, char *qdoor_name)
{
	struct qdoor *iter, *iter_n;
	list_for_each_entry_safe(iter, iter_n, &obj->qdoor_list, _list) {
		if (!strcmp(iter->name, qdoor_name)) {
			_ipceng_qdoor_del_by_entry(iter);
			obj->qdoor_count--;
			ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
			return 0;
		}
	}
	
	ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
	return 0;
}

int ipceng_qdoor_del_all(struct ipceng *obj)
{
	struct qdoor *iter, *iter_n;
	list_for_each_entry_safe(iter, iter_n, &obj->qdoor_list, _list) {
		_ipceng_qdoor_del_by_entry(iter);
		obj->qdoor_count--;
	}
	
	ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
	return 0;
}

int ipceng_qdoor_open(struct ipceng *obj, char *qdoor_name)
{
	struct qdoor *iter;
	list_for_each_entry(iter, &obj->qdoor_list, _list) {
		if (!strcmp(iter->name, qdoor_name)) {
			if (iter->sendq.state != IPC_ENTITY_OPENED) {
				iter->sendq.mqd = mq_open(iter->sendq.name, iter->sendq.oflags, \
					0664, &iter->sendq.attr);
				if (iter->sendq.mqd == (mqd_t)-1) {
					ipceng_set_error(obj, IPCENG_ERR_QDOOROPEN, \
						"failed to open qdoor: unable to open sending mq");
					return -1;
				}
				iter->sendq.state = IPC_ENTITY_OPENED;
			}
			if (iter->recvq.state != IPC_ENTITY_OPENED) {
				iter->recvq.mqd = mq_open(iter->recvq.name, iter->recvq.oflags, \
					0664, &iter->recvq.attr);
				if (iter->recvq.mqd == (mqd_t)-1) {
					ipceng_set_error(obj, IPCENG_ERR_QDOOROPEN, \
						"failed to open qdoor: unable to open receiving mq");
					// should close sendq if recvq opening is failed as well
					mq_close(iter->sendq.mqd);
					iter->sendq.state = IPC_ENTITY_CLOSED;
					return -1;
				}
				iter->recvq.state = IPC_ENTITY_OPENED;
			}
			ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
			return 0;
		}
	}

	ipceng_set_error(obj, IPCENG_ERR_QDOOROPEN, "failed to open qdoor: qdoor not found");
	return -1;
}

void _ipceng_qdoor_close_by_entry(struct qdoor *qd)
{
	if (qd->sendq.state != IPC_ENTITY_CLOSED) {
		mq_close(qd->sendq.mqd);
		qd->sendq.state = IPC_ENTITY_CLOSED;
	}
	if (qd->recvq.state != IPC_ENTITY_CLOSED) {
		mq_close(qd->recvq.mqd);
		qd->recvq.state = IPC_ENTITY_CLOSED;
	}
}

int ipceng_qdoor_close(struct ipceng *obj, char *qdoor_name)
{
	struct qdoor *iter;
	list_for_each_entry(iter, &obj->qdoor_list, _list) {
		if (!strcmp(iter->name, qdoor_name)) {
			_ipceng_qdoor_close_by_entry(iter);
			ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
			return 0;
		}
	}
	
	ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
	return 0;
}

int ipceng_qdoor_close_all(struct ipceng *obj)
{
	struct qdoor *iter;
	list_for_each_entry(iter, &obj->qdoor_list, _list) {
		_ipceng_qdoor_close_by_entry(iter);
	}
	
	ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
	return 0;
}

int ipceng_qdoor_push(struct ipceng *obj, char *qdoor_name, char *msg, int prio)
{
	// check for prio range
	if (!(prio >= IPCENG_PRIO_MIN && prio <= IPCENG_PRIO_MAX)) {
		ipceng_set_error(obj, IPCENG_ERR_QDOORPUSH, \
			"failed to push into qdoor: out of range priority");
		return -1;
	}

	struct qdoor *iter;
	list_for_each_entry(iter, &obj->qdoor_list, _list) {
		if (!strcmp(iter->name, qdoor_name)) {
			if (iter->sendq.timeout > 0) {
				// sending message with timeout
				struct timespec tm;
				clock_gettime(CLOCK_REALTIME, &tm);
				tm.tv_sec += iter->sendq.timeout;
				if (mq_timedsend(iter->sendq.mqd, msg, strlen(msg)+1, prio, &tm) == 0) {
					ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
					return 0;
				} else {
					ipceng_set_error(obj, errno, strerror(errno));
					return -1;
				}
			} else {
				// sending message without timeout
				if (mq_send(iter->sendq.mqd, msg, strlen(msg)+1, prio) == 0) {
					ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
					return 0;
				} else {
					ipceng_set_error(obj, errno, strerror(errno));
					return -1;
				}
			}
		}
	}

	ipceng_set_error(obj, IPCENG_ERR_QDOORPUSH, "failed to push into qdoor: qdoor not found");
	return -1;
}

int ipceng_qdoor_pop(struct ipceng *obj, char *qdoor_name, char **buff, int *prio)
{
	struct qdoor *iter;
	list_for_each_entry(iter, &obj->qdoor_list, _list) {
		if (!strcmp(iter->name, qdoor_name)) {
			if (iter->recvq.timeout > 0) {
				// receiving message with timeout
				struct timespec tm;
				clock_gettime(CLOCK_REALTIME, &tm);
				tm.tv_sec += iter->recvq.timeout;
				*buff = (char *)calloc(iter->recvq.attr.mq_msgsize, 1);
				if (mq_timedreceive(iter->recvq.mqd, *buff, iter->recvq.attr.mq_msgsize, prio, &tm) >= 0) {
					ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
					return 0;
				} else {
					free_safe(*buff);
					ipceng_set_error(obj, errno, strerror(errno));
					return -1;
				}
			} else {
				// receiving message without timeout
				*buff = (char *)calloc(iter->recvq.attr.mq_msgsize, 1);
				if (mq_receive(iter->recvq.mqd, *buff, iter->recvq.attr.mq_msgsize, prio) >= 0) {
					ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
					return 0;
				} else {
					free_safe(*buff);
					ipceng_set_error(obj, errno, strerror(errno));
					return -1;
				}
			}
		}
	}

	ipceng_set_error(obj, IPCENG_ERR_QDOORPOP, "failed to pop from qdoor: qdoor not found");
	return -1;
}

int ipceng_get_qdoor_count(struct ipceng *obj)
{
	return obj->qdoor_count;
}

// functions of shared memory part
int ipceng_shm_add(struct ipceng *obj, char *shm_name, size_t size)
{
	// shm should not be added before
	struct shm *iter;
	list_for_each_entry(iter, &obj->shm_list, _list) {
		if (!strcmp(iter->name, shm_name)) {
			ipceng_set_error(obj, IPCENG_ERR_SHMADD, \
				"failed to add shm: shm has been added already");
			return -1;
		}
	}

	int shmname_len = strlen("/_.shm") + strlen(obj->name) + strlen(shm_name) + 1;
	// creating shm object
	struct shm *new_shm = (struct shm *)malloc(sizeof(struct shm));
	new_shm->name = (char *)malloc(shmname_len);
	sprintf(new_shm->name, "/%s_%s.shm", obj->name, shm_name);
	new_shm->oflag = O_CREAT | O_RDWR;
	new_shm->mode = 0664;
	new_shm->shmd = shm_open(new_shm->name, new_shm->oflag, new_shm->mode);
	if (new_shm->shmd == -1) {
		ipceng_set_error(obj, errno, strerror(errno));
		free_safe(new_shm->name);
		free_safe(new_shm);
		return -1;
	}
	new_shm->size = size;
	if (ftruncate(new_shm->shmd, new_shm->size) != 0) {
		ipceng_set_error(obj, errno, strerror(errno));
		close(new_shm->shmd);
		free_safe(new_shm->name);
		free_safe(new_shm);
		return -1;
	}
	new_shm->ptr = mmap(NULL, new_shm->size, PROT_READ | PROT_WRITE, \
		MAP_SHARED, new_shm->shmd, 0);
	if (new_shm->ptr == MAP_FAILED) {
		ipceng_set_error(obj, errno, strerror(errno));
		close(new_shm->shmd);
		free_safe(new_shm->name);
		free_safe(new_shm);
		return -1;
	}
	new_shm->state = IPC_ENTITY_OPENED;

	ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
	return 0;
}

int ipceng_shm_del(struct ipceng *obj, char *shm_name)
{
	struct shm *iter, *iter_n;
	list_for_each_entry_safe(iter, iter_n, &obj->shm_list, _list) {
		if (!strcmp(iter->name, shm_name)) {
			munmap(iter->ptr, iter->size);
			close(iter->shmd);
			free_safe(iter->name);
			free_safe(iter);
		}
	}

	ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
	return 0;
}

int ipceng_shm_open(struct ipceng *obj, char *shm_name)
{
	struct shm *iter;
	list_for_each_entry(iter, &obj->shm_list, _list) {
		if (!strcmp(iter->name, shm_name)) {
			if (iter->state != IPC_ENTITY_OPENED) {
				iter->shmd = shm_open(iter->name, iter->oflag, iter->mode);
				if (iter->shmd == -1) {
					ipceng_set_error(obj, errno, strerror(errno));
					return -1;
				}
				if (ftruncate(iter->shmd, iter->size) != 0) {
					ipceng_set_error(obj, errno, strerror(errno));
					close(iter->shmd);
					return -1;
				}
				iter->ptr = mmap(NULL, iter->size, PROT_READ | PROT_WRITE, \
					MAP_SHARED, iter->shmd, 0);
				if (iter->ptr == MAP_FAILED) {
					ipceng_set_error(obj, errno, strerror(errno));
					close(iter->shmd);
					return -1;
				}
				iter->state = IPC_ENTITY_OPENED;
			}
			ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
			return 0;
		}
	}

	ipceng_set_error(obj, IPCENG_ERR_SHMOPEN, "failed to open shm: no shm found");
	return -1;
}

int ipceng_shm_close(struct ipceng *obj, char *shm_name)
{
	struct shm *iter;
	list_for_each_entry(iter, &obj->shm_list, _list) {
		if (!strcmp(iter->name, shm_name)) {
			if (iter->state != IPC_ENTITY_CLOSED) {
				close(iter->shmd);
				munmap(iter->ptr, iter->size);
				iter->state = IPC_ENTITY_CLOSED;
			}
		}
	}

	ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
	return 0;
}

int ipceng_shm_read(struct ipceng *obj, char *shm_name, char **buff, size_t addr, size_t size)
{
	struct shm *iter;
	list_for_each_entry(iter, &obj->shm_list, _list) {
		if (!strcmp(iter->name, shm_name)) {
			if (iter->state != IPC_ENTITY_OPENED) {
				ipceng_set_error(obj, IPCENG_ERR_SHMREAD, \
					"failed to read from shm: shm is not opened");
				return -1;
			}
			size_t last_offset = addr + size + 1;
			if (last_offset > iter->size) {
				ipceng_set_error(obj, IPCENG_ERR_SHMREAD, \
					"failed to read from shm: (addr,size) pair is out of range");
				return -1;
			}
			// now everything is ok, should read the bytes
			*buff = (char *)malloc(size);
			memcpy(*buff, iter->ptr + addr, size);
			ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
			return 0;
		}
	}

	ipceng_set_error(obj, IPCENG_ERR_SHMREAD, "failed to read from shm: no shm found");
	return -1;
}

int ipceng_shm_write(struct ipceng *obj, char *shm_name, char *data, size_t addr, size_t size)
{
	struct shm *iter;
	list_for_each_entry(iter, &obj->shm_list, _list) {
		if (!strcmp(iter->name, shm_name)) {
			if (iter->state != IPC_ENTITY_OPENED) {
				ipceng_set_error(obj, IPCENG_ERR_SHMWRITE, \
					"failed to read from shm: shm is not opened");
				return -1;
			}
			size_t last_offset = addr + size + 1;
			if (last_offset > iter->size) {
				ipceng_set_error(obj, IPCENG_ERR_SHMWRITE, \
					"failed to read from shm: (addr,size) pair is out of range");
				return -1;
			}
			// now everything is ok, should read the bytes
			memcpy(iter->ptr + addr, data, size);
			ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
			return 0;
		}
	}

	ipceng_set_error(obj, IPCENG_ERR_SHMWRITE, "failed to read from shm: no shm found");
	return -1;
}

int ipceng_get_shm_count(struct ipceng *obj)
{
	return obj->shm_count;
}
