#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "process.h"

void transfer(void *parent_data, local_id src, local_id dst, balance_t amount) {
    struct proc *parent = parent_data;

    Message msg;
    memset(msg.s_payload, 0, MAX_PAYLOAD_LEN);
    msg.s_header.s_type = TRANSFER;
    msg.s_header.s_payload_len = sizeof(TransferOrder);
    msg.s_header.s_magic = MESSAGE_MAGIC;
    TransferOrder *body = (TransferOrder *) msg.s_payload;
    body->s_src = src;
    body->s_dst = dst;
    body->s_amount = amount;

    update_msg_local_time(&msg);
    long result = send(parent, src, &msg);
    if (result < 0) {
        return;
    }

    memset(msg.s_payload, 0, msg.s_header.s_payload_len);
    while (1) {
        result = receive(parent, dst, &msg);
        if (result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
        }
        break;
    }
}

int child_proc_work(struct proc *child) {

    Message message;
    message.s_header.s_type = STARTED;
    message.s_header.s_magic = MESSAGE_MAGIC;
    sprintf(message.s_payload, log_started_fmt, get_lamport_time(), child->id, getpid(), getppid(),
            child->balance.s_balance);

    log_event(events_log, message.s_payload);
    message.s_header.s_payload_len = strlen(message.s_payload);

    update_msg_local_time(&message);
    if (send_multicast(child, &message) < 0) {
        return -1;
    }

    if (receive_all(child, &message) < 0) {
        return -1;
    }

    char log_all_buff[MAX_PAYLOAD_LEN];
    sprintf(log_all_buff, log_received_all_started_fmt, get_lamport_time(), child->id);
    log_event(events_log, log_all_buff);

    int done_count = 0;
    while (1) {
        Message new_message;
        memset(new_message.s_payload, 0, MAX_PAYLOAD_LEN);

        while (1) {
            int res = receive_any(child, &new_message);
            if(res == 0) break;
            if(res == -1 && errno == EWOULDBLOCK) continue;
            perror("receive any");
            return -1;
        }

        make_balance_snapshot(child, 0, child->history.s_history_len);
        // async
        if (new_message.s_header.s_type == STOP) {
            break;
        } else if (new_message.s_header.s_type == TRANSFER) {
            if (handle_transfer(child, &new_message) < 0) {
                return -1;
            }
        } else if (new_message.s_header.s_type == DONE) {
            done_count++;
        } else {
            return -1;
        }
    }

    Message done_message;
    done_message.s_header.s_type = DONE;
    done_message.s_header.s_magic = MESSAGE_MAGIC;
    sprintf(done_message.s_payload, log_done_fmt, get_lamport_time(), child->id, child->balance.s_balance);
    log_event(events_log, done_message.s_payload);
    done_message.s_header.s_payload_len = strlen(done_message.s_payload);

    update_msg_local_time(&done_message);
    if (send_multicast(child, &done_message) < 0) {
        return -1;
    }

    for (int i = 0; i < child->proc_count - 1 - done_count; i++) {
        int res  = receive_any(child, &done_message);
        if (res == -1) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                i--;
                continue;
            }
            return -1;
        }
        make_balance_snapshot(child, 0, child->history.s_history_len);
    }
    sprintf(log_all_buff, log_received_all_done_fmt, get_lamport_time(), child->id);
    log_event(events_log, log_all_buff);
    if (send_balance_history(child) < 0) {
        printf("ERROR send balance");
        return -1;
    }
    return 0;
}


void k_proc_work(struct proc *parent) {
    Message start_message;
    if (receive_all(parent, &start_message) < 0) {
        return;
    }

    bank_robbery(parent, (local_id) (parent->proc_count));

    Message message;
    message.s_header.s_type = STOP;
    message.s_header.s_magic = MESSAGE_MAGIC;
    update_msg_local_time(&message);
    if (send_multicast(parent, &message) < 0) {
        return;
    }
    Message done_message;
    if (receive_all(parent, &done_message) < 0) {
        return;
    }

    AllHistory all_history;
    all_history.s_history_len = parent->proc_count;
    for (local_id i = 1; i <= (local_id) parent->proc_count; i++) {
        Message history_message;
        if (receive(parent, i, &history_message) < 0 ) {
            if(errno != EWOULDBLOCK || errno != EAGAIN) {
                return;
            } else {
                i--;
                continue;
            }
        }
        BalanceHistory *b = (BalanceHistory *) history_message.s_payload;
        memcpy(all_history.s_history + i - 1, b, history_message.s_header.s_payload_len);
    }
      print_history(&all_history);

    for (int i = 1; i <= parent->proc_count; i++) {
        wait(NULL);
    }
}

void log_pipes(struct proc_pipe *pipes, local_id id, local_id count) {
    FILE *f = fopen(pipes_log, "a+");
    for (local_id i = 0; i <= count; i++) {
        if (i != id) {
            fprintf(f, "pipes: fd[0] %d fd[1]: %d processes id: %d %d\n", pipes[i].fd[0], pipes[i].fd[1], id, i);
        }
    }
    fclose(f);
}

int main(int argc, char **argv) {
    FILE *f = fopen(events_log, "a+");
    char *arg_end;
    local_id proc_count = (local_id) strtol(argv[2], &arg_end, 10);

    balance_t balances[proc_count + 1];
    for (int i = 1; i <= proc_count; i++) {
        *arg_end = 0;
        balances[i] = (balance_t) strtol(argv[i + 2], &arg_end, 10);
    }

    struct proc_pipe children_pipes[proc_count + 1][proc_count + 1];
    struct proc processes[proc_count + 1];

    for (local_id i = 0; i <= proc_count; i++) {
        for (local_id j = 0; j <= proc_count; j++) {
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
        processes[i].id = i;
        processes[i].proc_count = proc_count;
        processes[i].pipes = children_pipes[i];
        processes[i].balance.s_balance = balances[i];
        processes[i].balance.s_time = 0;
        processes[i].balance.s_balance_pending_in = 0;
        processes[i].history.s_history_len = 0;
        processes[i].history.s_id = processes[i].id;
        if (make_nonblock_pipes(&processes[i]) < 0) {
            return -1;
        }
    }

    for (local_id i = 0; i <= proc_count; i++) {
        log_pipes(children_pipes[i], i, proc_count);
    }

    pid_t proc_id = 0;
    for (local_id i = 1; i <= proc_count; i++) {
        proc_id = fork();
        if (proc_id < 0) {
            break;
        }
        if (proc_id == 0) {
            struct proc child = processes[i];
            child.balance.s_time = get_lamport_time();
            for(int j = 0; j <= proc_count; j++) {
                if(j != i) {
                    for (int k = 0; k <= proc_count; k++) {
                        if(k != j) {
                            close(children_pipes[j][k].fd[0]);
                            close(children_pipes[j][k].fd[1]);
                        }
                    }
                }
            }
            child_proc_work(&child);
            break;
        }
    }

    if (proc_id != 0) {
        for(int j = 0; j <= proc_count; j++) {
            if(j != PARENT_ID) {
                for (int k = 0; k <= proc_count; k++) {
                    if(k != j) {
                        close(children_pipes[j][k].fd[0]);
                        close(children_pipes[j][k].fd[1]);
                    }
                }
            }
        }
        k_proc_work(&processes[PARENT_ID]);
    }
    fclose(f);
    return 0;
}
