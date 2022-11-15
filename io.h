#ifndef PA2_IO_H
#define PA2_IO_H

#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>

#include "ipc.h"
#include "banking.h"
#include "pa2345.h"
#include "clock.h"

struct proc_pipe {
    int fd[2];
};

struct proc {
    local_id id;
    local_id proc_count;
    struct proc_pipe *pipes;
    BalanceState balance;
    BalanceHistory history;
};


int log_event(const char *const file_name, const char *msg) {
    FILE *fd = fopen(file_name, "a+");
    int res = fprintf(fd, "%s", msg);
    printf("%s", msg);
    fclose(fd);
    return res;
}

int send(void *self, local_id dst, const Message *msg) {
    struct proc *proc = self;
    long result = write(proc->pipes[dst].fd[1], msg,
                        (long) (sizeof(MessageHeader) + msg->s_header.s_payload_len));
    if (result <= 0) {
        return -1;
    }
    return 0;
}

int send_multicast(void *self, const Message *msg) {
    struct proc *proc = self;
    for (local_id i = 0; i <= proc->proc_count; i++) {
        if (i != proc->id) {
            int result = send(self, i, msg);
            if (result < 0) {
                return -1;
            }
        }
    }
    return 0;
}

int receive(void *self, local_id from, Message *msg) {
    struct proc *proc = self;
    long need_receive = sizeof(MessageHeader) /*+ MAX_PAYLOAD_LEN*/;
    long result = read(proc->pipes[from].fd[0], msg, need_receive);
    if (result <= 0) {
        return -1;
    }

    need_receive = msg->s_header.s_payload_len;
    if(need_receive == 0) {
        return 0;
    }

    result = read(proc->pipes[from].fd[0], msg->s_payload, need_receive);
    if (result <= 0) {
        return -1;
    }
    update_lamport_time(msg);
    return 0;
}

int receive_all(void *self, Message *msg) {
    struct proc *proc = self;
    for (local_id i = 1; i <= proc->proc_count; i++) {
        if (i != proc->id) {
            memset(msg, 0, sizeof(Message));
            int result = receive(proc, i, msg);
            if (result < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    i--;
                    continue;
                }
                return -1;
            }
        }
    }
    return 0;
}


int set_nonblock(int fd) {
    const int flag = fcntl(fd, F_GETFL);
    if (flag == -1) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, flag | O_NONBLOCK) != 0) {
        return -1;
    }
    return 0;
}

int make_nonblock_pipes(struct proc *child) {
    for (int i = 0; i <= child->proc_count; i++) {
        if (i != child->id) {
            if (set_nonblock(child->pipes[i].fd[0]) < 0) {
                return -1;
            }
            if (set_nonblock(child->pipes[i].fd[1]) < 0) {
                return -1;
            }
        }
    }
    return 0;
}


int receive_any(void *self, Message *msg) {
    struct proc *child = self;
    while (1) {
        for (int i = 0; i <= child->proc_count; i++) {
            if (i != child->id) {
                long result = receive(child, (local_id) i, msg);
                if (result < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
                    return -1;
                }
                if (result == 0) {
                    return 0;
                }
            }
        }
    }
}

#endif //PA2_IO_H
