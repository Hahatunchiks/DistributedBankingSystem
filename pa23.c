#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "process.h"

timestamp_t get_physical_time() {
    return 0;
}

void transfer(void *parent_data, local_id src, local_id dst, balance_t amount) {
    struct parent_proc *parent = parent_data;

    Message msg;
    memset(msg.s_payload, 0, MAX_PAYLOAD_LEN);
    msg.s_header.s_type = TRANSFER;
    msg.s_header.s_local_time = get_physical_time();
    msg.s_header.s_payload_len = sizeof(TransferOrder);
    msg.s_header.s_magic = MESSAGE_MAGIC;
    TransferOrder *body = (TransferOrder *) msg.s_payload;
    body->s_src = src;
    body->s_dst = dst;
    body->s_amount = amount;

    long result = send_full(parent->parent_pipes_out[src].fd[1], &msg);
    if (result < 0) {
        return;
    }
    memset(msg.s_payload, 0, msg.s_header.s_payload_len);
    if (receive_full(parent->parent_pipes_in[dst].fd[0], &msg) < 0) {
        return;
    }
}

int child_proc_work(struct child_proc *child) {

    Message message;
    message.s_header.s_type = STARTED;
    message.s_header.s_local_time = get_physical_time();
    pid_t p = getpid();
    pid_t pp = getppid();
    sprintf(message.s_payload, log_started_fmt, message.s_header.s_local_time, child->id, p, pp,
            child->balance.s_balance);
    message.s_header.s_payload_len = strlen(message.s_payload);

    if (log_event(events_log, message.s_payload) < 0) {
        return -1;
    }
    if (send_multicast(child, &message) < 0) {
        return -1;
    }
    if (receive_all(child, &message) < 0) {
        return -1;
    }

    char log_all_fmt[MAX_PAYLOAD_LEN];
    sprintf(log_all_fmt, log_received_all_started_fmt, get_physical_time(), child->id);
    if (log_event(events_log, log_all_fmt) < 0) {
        return -1;
    }

    // work
    while (1) {
        Message new_message;
        memset(new_message.s_payload, 0, MAX_PAYLOAD_LEN);
        char log_transfer[MAX_PAYLOAD_LEN];
        if (receive_any(child, &new_message) < 0) {
            return -1;
        }
        if (new_message.s_header.s_type == STOP) {
            break;
        } else if (new_message.s_header.s_type == TRANSFER) {

            TransferOrder received_transfer;
            memcpy(&received_transfer, new_message.s_payload, new_message.s_header.s_payload_len);

            if (received_transfer.s_src == child->id) {
                child->balance.s_balance = (balance_t) (child->balance.s_balance - received_transfer.s_amount);
                child->balance.s_time = get_physical_time();
                if (send(child, received_transfer.s_dst, &new_message) < 0) {
                    printf("CANNOT SEND TO DST %d -> %d\n", child->id, received_transfer.s_dst);
                }

                sprintf(log_transfer, log_transfer_out_fmt, child->balance.s_time, received_transfer.s_src,
                        received_transfer.s_amount, received_transfer.s_dst);
                log_event(events_log, log_transfer);
            } else if (received_transfer.s_dst == child->id) {
                sprintf(log_transfer, log_transfer_in_fmt, child->balance.s_time, received_transfer.s_dst,
                        received_transfer.s_amount, received_transfer.s_src);
                log_event(events_log, log_transfer);

                child->balance.s_balance = (balance_t) (child->balance.s_balance + received_transfer.s_amount);
                child->balance.s_time = get_physical_time();

                new_message.s_header.s_local_time = child->balance.s_time;
                new_message.s_header.s_type = ACK;
                memset(new_message.s_payload, 0, new_message.s_header.s_payload_len);
                new_message.s_header.s_payload_len = 0;
                if (send_full(child->parent_pipes_in.fd[1], &new_message) < 0) {
                    printf("CANNOT SEND TO PARENT %d -> parent\n", child->id);
                }
            } else {
                printf("broken received transfer order\n");
                return -3;
            }
        } else {
            printf("broken receive 2\n");
            return -2;
        }
    }
    // 3 step;
    Message done_message;
    done_message.s_header.s_type = DONE;
    sprintf(done_message.s_payload, log_done_fmt, get_physical_time(), child->id, child->balance.s_balance);
    done_message.s_header.s_payload_len = strlen(message.s_payload);

    if (log_event(events_log, done_message.s_payload) < 0) {
        return -1;
    }
    if (send_multicast(child, &done_message) < 0) {
        return -1;
    }
    if (receive_all(child, &done_message) < 0) {
        return -1;
    }

    char log_all_done_fmt[MAX_PAYLOAD_LEN];
    sprintf(log_all_done_fmt, log_received_all_done_fmt, get_physical_time(), child->id);
    if (log_event(events_log, log_all_done_fmt) < 0) {
        return -1;
    }
    return 0;
}

int init_parent_pipes(struct proc_pipe *pipes_array, local_id count) {
    for (local_id i = 1; i <= count; i++) {
        int res = pipe(pipes_array[i].fd);
        if (res < 0) {
            return -1;
        }
    }
    return 0;
}


void k_proc_work(struct parent_proc *parent) {
    for (int i = 1; i <= parent->child_proc_count; i++) {
        Message start_message;
        if (receive_full(parent->parent_pipes_in[i].fd[0], &start_message) < 0) {
            return;
        }
    }

    bank_robbery(parent, (local_id) (parent->child_proc_count));

    Message message;
    message.s_header.s_type = STOP;
    for (int i = 1; i <= parent->child_proc_count; i++) {
        if (send_full(parent->parent_pipes_out[i].fd[1], &message) < 0) {
            return;
        }
    }

    for (int i = 1; i <= parent->child_proc_count; i++) {
        Message done_message;
        if (receive_full(parent->parent_pipes_in[i].fd[0], &done_message) < 0) {
            return;
        }
    }
    for (int i = 1; i <= parent->child_proc_count; i++) {
        wait(NULL);
    }
}

void log_pipes(struct proc_pipe *pipes, local_id id, local_id count) {
    FILE *f = fopen(pipes_log, "a+");
    for (local_id i = 1; i <= count; i++) {
        if (i != id) {
            fprintf(f, "pipes: fd[0] %d fd[1]: %d processes id: %d %d\n", pipes[i].fd[0], pipes[i].fd[1], id, i);
        }
    }
    fclose(f);
}

int main(int argc, char **argv) {
    char *arg_end;
    local_id proc_count = (local_id) strtol(argv[2], &arg_end, 10);
    balance_t balances[proc_count+1];

    for (int i = 1; i <= proc_count; i++) {
        *arg_end = 0;
        balances[i] = (balance_t) strtol(argv[i + 2], &arg_end, 10);
    }


    // init parent pipes
    struct proc_pipe parent_pipes_in[proc_count+1];
    init_parent_pipes(parent_pipes_in, proc_count);
    log_pipes(parent_pipes_in, PARENT_ID, proc_count);
    struct proc_pipe parent_pipes_out[proc_count+1];
    init_parent_pipes(parent_pipes_out, proc_count);

    log_pipes(parent_pipes_out, PARENT_ID, proc_count);
    // open logs
    FILE *f = fopen(events_log, "a+");


    // init children pipes
    struct child_proc children[proc_count+1];
    struct proc_pipe children_pipes[proc_count+1][proc_count+1];
    for (local_id i = 1; i <= proc_count; i++) {
        for (local_id j = 1; j <= proc_count; j++) {
            if (i < j) {
                int first_pipe[2];
                int second_pipe[2];

                pipe(first_pipe);
                pipe(second_pipe);

                children_pipes[i][j].fd[0] = first_pipe[0];
                children_pipes[i][j].fd[1] = second_pipe[1];

                children_pipes[j][i].fd[1] = first_pipe[1];
                children_pipes[j][i].fd[0] = second_pipe[0];
            }
        }
        children[i].id = i;
        children[i].proc_count = proc_count;
        children[i].children_pipes = children_pipes[i];
        children[i].parent_pipes_in = parent_pipes_in[i];
        children[i].parent_pipes_out = parent_pipes_out[i];
        children[i].balance.s_balance = balances[i];
        if (make_nonblock_pipes(&children[i]) < 0) {
            return -1;
        }
    }

    for (local_id i = 1; i <= proc_count; i++) {
        log_pipes(children_pipes[i], i, proc_count);
    }

    pid_t proc_id = 0;
    for (local_id i = 1; i <= proc_count; i++) {
        proc_id = fork();
        if (proc_id < 0) {
            break;
        }
        if (proc_id == 0) {
            struct child_proc child = children[i];
            child.balance.s_time = get_physical_time();

            close(parent_pipes_in[i].fd[0]);
            close(parent_pipes_out[i].fd[1]);
            for (local_id j = 1; j <= proc_count; j++) {
                if (i != j) {
                    close(children_pipes[j][i].fd[0]);
                    close(children_pipes[j][i].fd[1]);

                    close(parent_pipes_in[j].fd[0]);
                    close(parent_pipes_in[j].fd[1]);
                    close(parent_pipes_out[j].fd[0]);
                    close(parent_pipes_out[j].fd[1]);
                    for (local_id k = 1; k <= proc_count; k++) {
                        if (i != k && k != j) {
                            close(children_pipes[j][k].fd[0]);
                            close(children_pipes[j][k].fd[1]);
                            close(children_pipes[k][j].fd[0]);
                            close(children_pipes[k][j].fd[1]);
                        }
                    }
                }
            }
            child_proc_work(&child);
            break;
        }
    }

    if (proc_id != 0) {
        for (local_id i = 1; i <= proc_count; i++) {
            for (local_id j = 1; j <= proc_count; j++) {
                if (i < j) {
                    close(children_pipes[i][j].fd[0]);
                    close(children_pipes[i][j].fd[1]);
                    close(children_pipes[j][i].fd[0]);
                    close(children_pipes[j][i].fd[1]);
                }
            }
            close(parent_pipes_in[i].fd[1]);
            close(parent_pipes_out[i].fd[0]);
        }

        struct parent_proc parent;
        parent.id = PARENT_ID;
        parent.parent_pipes_in = parent_pipes_in;
        parent.parent_pipes_out = parent_pipes_out;
        parent.child_proc_count = (int) proc_count;
        k_proc_work(&parent);
        fclose(f);
    }
    return 0;
}
