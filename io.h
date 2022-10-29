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

struct proc_pipe {
    int fd[2];
};

struct child_proc {
    local_id id;
    local_id proc_count;
    struct proc_pipe parent_pipes_in;
    struct proc_pipe parent_pipes_out;
    struct proc_pipe *children_pipes;
    BalanceState balance;
    BalanceHistory history;
};

struct parent_proc {
    local_id id;
    int child_proc_count;
    struct proc_pipe *parent_pipes_in;
    struct proc_pipe *parent_pipes_out;
};

int log_event(const char *const file_name, const char *msg) {
    FILE *fd = fopen(file_name, "a+");
    int res = fprintf(fd, "%s", msg);
    printf("%s", msg);
    fclose(fd);
    return res;
}

int send_full(int fd, const Message *msg) {
    long need_send = (long) (sizeof(MessageHeader) + msg->s_header.s_payload_len);
    long sent = 0;
    while (sent < need_send) {
        long res = write(fd, msg + sent, need_send - sent);
        if (res <= 0) {
            return -1;
        }
        sent += res;
    }
    return 0;
}

int send(void *self, local_id dst, const Message *msg) {

    local_id *id = self;
    if (*id == 0) {
        struct parent_proc *parent = self;
        return send_full(parent->parent_pipes_out[dst].fd[1], msg);
    }

    struct child_proc *proc = self;
    int result = send_full(proc->children_pipes[dst].fd[1], msg);
    return result;
}

int send_multicast(void *self, const Message *msg) {

    local_id *id = self;
    if (*id == 0) {
        struct parent_proc *parent = self;
        for (local_id i = 1; i <= (local_id) parent->child_proc_count; i++) {
            if(send(parent, i, msg) < 0) {
                return -1;
            }
//            if(msg->s_header.s_type == STOP) {
//                sleep(2);
//            }
        }
        return 0;
    }

    struct child_proc *proc = self;

    long result = send_full(proc->parent_pipes_in.fd[1], msg);
    if (result < 0) {
        return -1;
    }
    //send to other
    for (local_id i = 1; i <= proc->proc_count; i++) {
        if (i != proc->id) {
            result = send(proc, i, msg);
            if (result < 0) {
                return -1;
            }
        }
    }
    return 0;
}

int receive_full(int fd, Message *msg) {

    long need_receive = sizeof(MessageHeader);
    long received = 0;
    while (received < need_receive) {
        long res = read(fd, msg + received, need_receive - received);
        if (res <= 0) {
            return -1;
        }
       // if (res == 0) return 0;
        received += res;
    }

    need_receive = msg->s_header.s_payload_len;
    received = 0;
    while (received < need_receive) {
        long res = read(fd, msg->s_payload + received, need_receive - received);
        if (res <= 0) {
            return -1;
        }
        received += res;
    }

    return 0;
}

int receive(void *self, local_id from, Message *msg) {
    local_id *id = self;
    if (*id == 0) {
        struct parent_proc *parent = self;
        while (1) {
            int result = receive_full(parent->parent_pipes_in[from].fd[0], msg);
            if (result < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                return -1;
            }
            break;
        }
        return 0;
    }

    struct child_proc *proc = self;
    int result = receive_full(proc->children_pipes[from].fd[0], msg);
    return result;
}

int receive_all(void *self, Message *msg) {

    local_id *id = self;
    if (*id == 0) {
        struct parent_proc *parent = self;
        for (int i = 1; i <= parent->child_proc_count; i++) {
            memset(msg, 0, sizeof(Message));
            if (receive(parent, (local_id) i, msg) < 0) {
                return -1;
            }
        }
        return 0;
    }

    struct child_proc *proc = self;
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


//SET NONBLOCKING  STATE
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

int make_nonblock_pipes(struct child_proc *child) {
    for (int i = 1; i <= child->proc_count; i++) {
        if (i != child->id) {
            if (set_nonblock(child->children_pipes[i].fd[0]) < 0) {
                printf("cannot set non block \n");
                return -1;
            }

            if (set_nonblock(child->children_pipes[i].fd[1]) < 0) {
                printf("cannot set non block \n");
                return -1;
            }
        }
    }
    if (set_nonblock(child->parent_pipes_out.fd[0]) < 0) {
        printf("cannot set non block1\n");
        return -1;
    }
    if (set_nonblock(child->parent_pipes_out.fd[1]) < 0) {
        printf("cannot set non block2\n");
        return -1;
    }

    if (set_nonblock(child->parent_pipes_in.fd[0]) < 0) {
        printf("cannot set non block1\n");
        return -1;
    }
    if (set_nonblock(child->parent_pipes_in.fd[1]) < 0) {
        printf("cannot set non block2\n");
        return -1;
    }
    return 0;
}


int receive_any(void *self, Message *msg) {
    struct child_proc *child = self;

    while (1) {
        long result = receive_full(child->parent_pipes_out.fd[0], msg);
        if (result < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
            return -1;
        }
        if (result >= 0) {
            return 0;
        }

        for (int i = 1; i <= child->proc_count; i++) {
            if (i != child->id) {
                result = receive(child, (local_id) i, msg);
                if (result < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
                    return -1;
                }

                if (result >= 0) {
                    return 0;
                }
            }
        }
    }
}

#endif //PA2_IO_H
