#ifndef IPCENG_H
#define IPCENG_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <mqueue.h>
#include <time.h>
#include "list.h"

// list of errors
#define IPCENG_ERR_NOERROR				0
#define IPCENG_ERR_QDOORADD				-1
#define IPCENG_ERR_QDOORDEL				-2
#define IPCENG_ERR_QDOOROPEN			-3
#define IPCENG_ERR_QDOORPUSH			-4
#define IPCENG_ERR_QDOORPOP				-5
#define IPCENG_ERR_TERM					-6

// default values
#define IPCENG_DAFAULT_MSGCOUNT		10
#define	IPCENG_DAFAULT_MSGSIZE		1024 				// in bytes
#define IPCENG_PRIO_MIN				0
#define IPCENG_PRIO_MAX				31
#define	IPCENG_DAFAULT_PRIO			IPCENG_PRIO_MIN
#define IPCENG_DEFAULT_TIMEOUT		3					// in seconds

// main structure
struct ipceng
{
	char *name;
	bool has_logging;
	int err_code;
	char *err_msg;
	// qdoor list
	struct list_head qdoor_list;
	int qdoor_count;
	// internal ipceng linked list member; **this is not use by libipceng**
	struct list_head _list;
};

// functions

/**
 * @brief      function to make a 'struct ipceng *' object; call ipceng_term
 *             after you have done with the object; use 'name' field as a
 *             reference name for sending messages
 *
 * @param      name  the name which is used as reference name for sending
 *                   messages
 *
 * @return     NULL = failed, not NULL = succeeded
 */
struct ipceng *ipceng_init(char *name);

/**
 * @brief      function to terminate ipc engine object
 *
 * @param      obj   target ipc engine object
 *
 * @return     0 = succeeded, -1 = failed (check ipceng_errmsg() or
 *             ipceng_errno())
 */
int ipceng_term(struct ipceng *obj);

/**
 * @brief      function to enable logging into stderr
 *
 * @param      obj   target ipc engine object
 *
 * @return     0 = succeeded, -1 = failed (check ipceng_errmsg() or
 *             ipceng_errno())
 */
int ipceng_log_enable(struct ipceng *obj);

/**
 * @brief      function to disable logging into stderr
 *
 * @param      obj   target ipc engine object
 *
 * @return     0 = succeeded, -1 = failed (check ipceng_errmsg() or
 *             ipceng_errno())
 */
int ipceng_log_disable(struct ipceng *obj);

/**
 * @brief      get errno code of the ipc engine object
 *
 * @param      obj   target ipc engine object
 *
 * @return     last error code in the object
 */
int ipceng_errno(struct ipceng *obj);

/**
 * @brief      get error message of the ipc engine object
 *
 * @param      obj   target ipc engine object
 *
 * @return     last error message in the object
 */
char *ipceng_errmsg(struct ipceng *obj);

/**
 * @brief      function to add and open a new qdoor to ipc engine object
 *
 * @param      obj           ipc engine object
 * @param      qdoor_name    to-be-added qdoor name; this is the name of your
 *                           target process which you want to communicate with;
 *                           the target process should call ipceng_init() with
 *                           this field to be able to communicate with current
 *                           process; it should not be added before
 * @param[in]  msg_maxcount  max number of messages that can be fit into each of
 *                           sending side of qdoor; use -1 if you want to use
 *                           internal default values
 * @param[in]  msg_maxsize   max size of each message (in bytes) in this qdoor;
 *                           use -1 if you want to use internal default values
 * @param[in]  timeout_send  timeout for sending side
 * @param[in]  timeout_recv  timeout for receiving side
 *
 * @return     0 = succeeded, -1 = failed (check ipceng_errmsg() or
 *             ipceng_errno())
 */
int ipceng_qdoor_add(struct ipceng *obj,
	char *qdoor_name,
	int msg_maxcount,
	int msg_maxsize,
	int timeout_send,
	int timeout_recv);

/**
 * @brief      macro to use ipceng_qdoor_add in simple mode
 *
 * @param      obj         ipc engine object
 * @param      qdoor_name  to-be-added qdoor name; this is the name of your
 *                         target process which you want to communicate with;
 *                         the target process should call ipceng_init() with
 *                         this field to be able to communicate with current
 *                         process
 *
 * @return     exactly same as ipceng_qdoor_add
 */
#define ipceng_qdoor_add_simple(obj, qdoor_name) \
	ipceng_qdoor_add(obj, qdoor_name, -1, -1, IPCENG_DEFAULT_TIMEOUT, IPCENG_DEFAULT_TIMEOUT)

/**
 * @brief      function to delete a qdoor
 *
 * @param      obj         ipc engine object
 * @param      qdoor_name  target qdoor name
 *
 * @return     0 = succeeded, -1 = failed (check ipceng_errmsg() or
 *             ipceng_errno())
 */
int ipceng_qdoor_del(struct ipceng *obj, char *qdoor_name);

/**
 * @brief      function to terminate and delete all qdoors of obj
 *
 * @param      obj   ipc engine object
 *
 * @return     0 = succeeded, -1 = failed (check ipceng_errmsg() or
 *             ipceng_errno())
 */
int ipceng_qdoor_del_all(struct ipceng *obj);

/**
 * @brief      function to open a closed qdoor
 *
 * @param      obj         ipc engine object
 * @param      qdoor_name  target qdoor name
 *
 * @return     0 = succeeded, -1 = failed (check ipceng_errmsg() or
 *             ipceng_errno())
 */
int ipceng_qdoor_open(struct ipceng *obj, char *qdoor_name);

/**
 * @brief      function to close an opened qdoor; this will not remove the qdoor
 *             and its message queues (use ipceng_qdoor_del instead)
 *
 * @param      obj         ipc engine object
 * @param      qdoor_name  target qdoor name
 *
 * @return     0 = succeeded, -1 = failed (check ipceng_errmsg() or
 *             ipceng_errno())
 */
int ipceng_qdoor_close(struct ipceng *obj, char *qdoor_name);

/**
 * @brief      function to close all qdoors
 *
 * @param      obj   ipc engine object
 *
 * @return     0 = succeeded, -1 = failed (check ipceng_errmsg() or
 *             ipceng_errno())
 */
int ipceng_qdoor_close_all(struct ipceng *obj);

/**
 * @brief      function to push a message into a qdoor
 *
 * @param      obj         ipc engine object
 * @param      qdoor_name  target qdoor name
 * @param      msg         target message
 * @param[in]  prio        message priority
 *
 * @return     0 = succeeded, -1 = failed (check ipceng_errmsg() or
 *             ipceng_errno())
 */
int ipceng_qdoor_push(struct ipceng *obj, char *qdoor_name, char *msg, int prio);

/**
 * @brief      macro just to rename ipceng_qdoor_push
 *
 * @param      obj         ipc engine object
 * @param      qdoor_name  target qdoor name
 * @param      msg         target message
 * @param      prio        message priority
 *
 * @return     0 = succeeded, -1 = failed (check ipceng_errmsg() or
 *             ipceng_errno())
 */
#define ipceng_qdoor_send(obj, qdoor_name, msg, prio) ipceng_qdoor_push(obj, qdoor_name, msg, prio)

/**
 * @brief      same as ipceng_qdoor_send but with message priority = 0
 *
 * @param      obj         ipc engine object
 * @param      qdoor_name  target qdoor name
 * @param      msg         target message
 *
 * @return     0 = succeeded, -1 = failed (check ipceng_errmsg() or
 *             ipceng_errno())
 */
#define ipceng_qdoor_send_simple(obj, qdoor_name, msg) ipceng_qdoor_push(obj, qdoor_name, msg, 0)

/**
 * @brief      function to pop a message from a qdoor; you should free *buff if
 *             pop is successful (return value is not 0); if prio is NULL then
 *             filling that is ignored
 *
 * @param      obj         ipc engine object
 * @param      qdoor_name  target qdoor name
 * @param      buff        target message
 * @param[in]  prio        priority of received message
 *
 * @return     0 = succeeded, -1 = failed (check ipceng_errmsg() or
 *             ipceng_errno())
 */
int ipceng_qdoor_pop(struct ipceng *obj, char *qdoor_name, char **buff, int *prio);

/**
 * @brief      exactly same as ipceng_qdoor_pop just to rename it
 *
 * @param      obj         ipc engine object
 * @param      qdoor_name  target qdoor name
 * @param      buff        received message; should be freed after you end using
 *                         that
 * @param[in]  prio        size of message in bytes
 *
 * @return     0 = succeeded, -1 = failed (check ipceng_errmsg() or
 *             ipceng_errno())
 */
#define ipceng_qdoor_recv(obj, qdoor_name, buff, prio) ipceng_qdoor_pop(obj, qdoor_name, buff, prio)

/**
 * @brief      same as ipceng_qdoor_recv but without any need to know prio
 *
 * @param      obj         ipc engine object
 * @param      qdoor_name  target qdoor name
 * @param      buff        received message; should be freed after you end using
 *                         that
 *
 * @return     0 = succeeded, -1 = failed (check ipceng_errmsg() or
 *             ipceng_errno())
 */
#define ipceng_qdoor_recv_simple(obj, qdoor_name, buff) ipceng_qdoor_pop(obj, qdoor_name, buff, NULL)

#endif // !IPCENG_H