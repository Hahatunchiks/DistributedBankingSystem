#ifndef PA2_PROCESS_H
#define PA2_PROCESS_H

#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "common.h"
#include "ipc.h"
#include "pa2345.h"

struct proc_pipe {
    int fd[2];
};

struct child_proc {
    local_id id;
    local_id proc_count;
    struct proc_pipe parent_channel_in;
    struct proc_pipe parent_channel_out;
    struct proc_pipe *children_pipes;
};

int set_nonblock()
{

}
int log_event(const char *const file_name, const char *msg) {
    FILE *fd = fopen(file_name, "a+");
    int res = fprintf(fd, "%s", msg);
    printf("%s", msg);
    return res;
}

int send_full(int fd, const Message *msg) {
    long need_send = (long)(sizeof(MessageHeader) + msg->s_header.s_payload_len);
    long sent = 0;
    while (sent < need_send) {
        long res = write(fd, msg + sent, need_send - sent);
        if (res < 0) {
            return -1;
        }
        sent += res;
    }
    return 0;
}

int send(void *self, local_id dst, const Message *msg) {
    struct child_proc *proc = self;

    int result = send_full(proc->children_pipes[dst].fd[1], msg);
    return result;
}

int send_multicast(void *self, const Message *msg) {
    struct child_proc *proc = self;
    int result = log_event(events_log, msg->s_payload);
    if (result < 0) {
        return -1;
    }

    result = send_full(proc->parent_channel_in.fd[1], msg);
    if (result < 0) {
        return -1;
    }
    for (local_id i = 0; i < proc->proc_count; i++) {
        if (i != proc->id) {
            result = send(proc, i, msg);
            if (result < 0) {
                return -1;
            }
        }
    }
    return 0;
}

int receive_full(int fd, Message *msg, local_id id) {
    long need_receive = sizeof(MessageHeader);
    long received = 0;
    while (received < need_receive) {
        long res = read(fd, msg + received, need_receive - received);
        if (res < 0) {
            return -1;
        }
        received += res;
    }

    need_receive = msg->s_header.s_payload_len;
    received = 0;
    while (received < need_receive) {
        long res = read(fd, msg->s_payload + received, need_receive - received);
        if (res < 0) {
            return -1;
        }
        received += res;
    }
    return 0;
}

int receive(void *self, local_id from, Message *msg) {
    struct child_proc *proc = self;

    int result = receive_full(proc->children_pipes[from].fd[0], msg, proc->id);
    return result;
}

int receive_all(void *self, Message *msg) {
    struct child_proc *proc = self;
    int result = log_event(events_log, msg->s_payload);
    if (result < 0) {
        return -1;
    }
    result = send_full(proc->parent_channel_in.fd[1], msg);
    if (result < 0) {
        return -1;
    }

    for (local_id i = 0; i < proc->proc_count; i++) {
        if (i != proc->id) {
            result = receive(proc, i, msg);
            if (result < 0) {
                return -1;
            }
        }
    }
    return 0;
}

void wait_all(local_id cnt, struct proc_pipe* fd) {

    for (local_id i = 0; i < cnt; i++) {
        Message start_message;
        receive_full(fd[i].fd[0], &start_message, 15);
        Message done_message;
        receive_full(fd[i].fd[0], &done_message, 15);
        wait(NULL);
    }
}

#endif
