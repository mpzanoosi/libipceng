#include "ipceng.h"

// macros
#ifndef free_safe
#define free_safe(ptr) do{ free(ptr); (ptr)=NULL; } while(0)
#endif


// internal helper functions
static int _read_proc_oneline_file(char *file_name, char **buff)
{
	if (!file_name || !buff)
		return -1;

	*buff = NULL;
	FILE* fp = fopen(file_name, "r");
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
enum mqstate
{
	MQ_OPENED,
	MQ_CLOSED
};

struct mqwrap
{
	char *name;
	int timeout;
	int oflags;
	struct mq_attr attr;
	mqd_t mqd;
	enum mqstate state;
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
		ipceng_set_error(obj, IPCENG_ERR_TERM, "terminating ipceng object failed: terminating qdoors failed");
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
	if (!qdoor_name) {
		ipceng_set_error(obj, IPCENG_ERR_QDOORADD, "failed to add qdoor: qdoor_name is NULL");
		return -1;
	}
	// qdoor should not be added before
	struct qdoor *iter;
	list_for_each_entry(iter, &obj->qdoor_list, _list) {
		if (!strcmp(iter->name, qdoor_name)) {
			ipceng_set_error(obj, IPCENG_ERR_QDOORADD, "failed to add qdoor: qdoor has been added already");
			return -1;
		}
	}

	int target_msgmaxcount = (msg_maxcount == -1) ? IPCENG_DAFAULT_MSGCOUNT : msg_maxcount;
	int target_msgmaxsize = (msg_maxsize == -1) ? IPCENG_DAFAULT_MSGSIZE : msg_maxsize;
	char *buff;

	// check if target_msgmaxcount is greater than linux setting (/proc)
	if (_read_proc_oneline_file("/proc/sys/fs/mqueue/msg_max", &buff) != 0) {
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
	if (_read_proc_oneline_file("/proc/sys/fs/mqueue/msgsize_max", &buff) != 0) {
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
	new_qdoor->sendq.oflags = (timeout_send > 0) ? (O_CREAT | O_WRONLY) : (O_CREAT | O_WRONLY | O_NONBLOCK);
	new_qdoor->sendq.attr.mq_flags = 0;
	new_qdoor->sendq.attr.mq_maxmsg = target_msgmaxcount;
	new_qdoor->sendq.attr.mq_msgsize = target_msgmaxsize;
	new_qdoor->sendq.attr.mq_curmsgs = 0;
	new_qdoor->sendq.mqd = mq_open(new_qdoor->sendq.name, new_qdoor->sendq.oflags, 0664, &new_qdoor->sendq.attr);
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
	new_qdoor->recvq.oflags = (timeout_recv > 0) ? (O_CREAT | O_RDONLY) : (O_CREAT | O_RDONLY | O_NONBLOCK);
	new_qdoor->recvq.attr.mq_flags = 0;
	new_qdoor->recvq.attr.mq_maxmsg = target_msgmaxcount;
	new_qdoor->recvq.attr.mq_msgsize = target_msgmaxsize;
	new_qdoor->recvq.attr.mq_curmsgs = 0;
	new_qdoor->recvq.mqd = mq_open(new_qdoor->recvq.name, new_qdoor->recvq.oflags, 0664, &new_qdoor->recvq.attr);
	if (new_qdoor->recvq.mqd == (mqd_t)-1) {
		ipceng_set_error(obj, IPCENG_ERR_QDOORADD, \
			"failed to add qdoor: unable to open receiving mq");
		mq_unlink(new_qdoor->sendq.name);
		free_safe(new_qdoor->sendq.name);
		free_safe(new_qdoor->recvq.name);
		free_safe(new_qdoor);
		return -1;
	}
	new_qdoor->sendq.state = MQ_OPENED;
	new_qdoor->recvq.state = MQ_OPENED;
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
	}
	
	ipceng_set_error(obj, IPCENG_ERR_NOERROR, "no error");
	return 0;
}

int ipceng_qdoor_open(struct ipceng *obj, char *qdoor_name)
{
	struct qdoor *iter;
	list_for_each_entry(iter, &obj->qdoor_list, _list) {
		if (!strcmp(iter->name, qdoor_name)) {
			if (iter->sendq.state != MQ_OPENED) {
				iter->sendq.mqd = mq_open(iter->sendq.name, iter->sendq.oflags, 0664, &iter->sendq.attr);
				if (iter->sendq.mqd == (mqd_t)-1) {
					ipceng_set_error(obj, IPCENG_ERR_QDOOROPEN, \
						"failed to open qdoor: unable to open sending mq");
					return -1;
				}
				iter->sendq.state = MQ_OPENED;
			}
			if (iter->recvq.state != MQ_OPENED) {
				iter->recvq.mqd = mq_open(iter->recvq.name, iter->recvq.oflags, 0664, &iter->recvq.attr);
				if (iter->recvq.mqd == (mqd_t)-1) {
					ipceng_set_error(obj, IPCENG_ERR_QDOOROPEN, \
						"failed to open qdoor: unable to open receiving mq");
					mq_close(iter->sendq.mqd);
					iter->sendq.state = MQ_CLOSED;
					return -1;
				}
				iter->recvq.state = MQ_OPENED;
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
	if (qd->sendq.state != MQ_CLOSED) {
		mq_close(qd->sendq.mqd);
		qd->sendq.state = MQ_CLOSED;
	}
	if (qd->recvq.state != MQ_CLOSED) {
		mq_close(qd->recvq.mqd);
		qd->recvq.state = MQ_CLOSED;
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
		ipceng_set_error(obj, IPCENG_ERR_QDOORPUSH, "failed to push into qdoor: out of range priority");
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
				if (mq_timedreceive(iter->recvq.mqd, *buff, iter->recvq.attr.mq_msgsize, prio, &tm) > 0) {
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
				if (mq_receive(iter->recvq.mqd, *buff, iter->recvq.attr.mq_msgsize, prio) == 0) {
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