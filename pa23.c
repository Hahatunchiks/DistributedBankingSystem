#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "process.h"

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
    while (1) {
        result = receive_full(parent->parent_pipes_in[dst].fd[0], &msg);
        if (result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
        }
        break;
    }
}

int child_proc_work(struct child_proc *child) {

    if (child_synchro_with_other(child, STARTED, log_done_fmt, log_received_all_done_fmt) < 0) {
        return -1;
    }

    while (1) {
        Message new_message;
        memset(new_message.s_payload, 0, MAX_PAYLOAD_LEN);
        if (receive_any(child, &new_message) < 0) {
            return -1;
        }

        make_balance_snapshot(child);

        if (new_message.s_header.s_type == STOP) {
            break;
        } else if (new_message.s_header.s_type == TRANSFER) {
            if (handle_transfer(child, &new_message) < 0) {
                return -1;
            }
        } else {
            return -1;
        }
    }

    if (child_synchro_with_other(child, DONE, log_done_fmt, log_received_all_done_fmt) < 0) {
        return -1;
    }
    save_balance_history(child, get_physical_time());
    if (send_balance_history(child) < 0) {
        return -1;
    }
    return 0;
}


void k_proc_work(struct parent_proc *parent) {
    Message start_message;
    if (receive_all(parent, &start_message) < 0) {
        return;
    }

    bank_robbery(parent, (local_id) (parent->child_proc_count));

    Message message;
    message.s_header.s_type = STOP;
    if (send_multicast(parent, &message) < 0) {
        return;
    }

    Message done_message;
    if (receive_all(parent, &done_message) < 0) {
        return;
    }

    //receive balance history
    AllHistory all_history;
    all_history.s_history_len = parent->child_proc_count;
    for (local_id i = 1; i <= (local_id)parent->child_proc_count; i++) {
        Message history_message;
        if (receive(parent, i, &history_message) < 0) {
            return;
        }
        BalanceHistory *b = (BalanceHistory *) history_message.s_payload;
        memcpy(all_history.s_history + i - 1, b, history_message.s_header.s_payload_len);
    }
    print_history(&all_history);

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

    balance_t balances[proc_count + 1];
    for (int i = 1; i <= proc_count; i++) {
        *arg_end = 0;
        balances[i] = (balance_t) strtol(argv[i + 2], &arg_end, 10);
    }

    struct parent_proc parent;
    init_parent(&parent, proc_count);
    log_pipes(parent.parent_pipes_in, PARENT_ID, proc_count);
    log_pipes(parent.parent_pipes_out, PARENT_ID, proc_count);

    FILE *f = fopen(events_log, "a+");

    struct proc_pipe *children_pipes[proc_count+1];
    struct child_proc children[proc_count + 1];
    init_child(children, children_pipes, proc_count, parent.parent_pipes_in, parent.parent_pipes_out, balances);
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

            close(parent.parent_pipes_in[i].fd[0]);
            close(parent.parent_pipes_out[i].fd[1]);
            for (local_id j = 1; j <= proc_count; j++) {
                if (i != j) {
                    close(children_pipes[j][i].fd[0]);
                    close(children_pipes[j][i].fd[1]);

                    close(parent.parent_pipes_in[j].fd[0]);
                    close(parent.parent_pipes_in[j].fd[1]);
                    close(parent.parent_pipes_out[j].fd[0]);
                    close(parent.parent_pipes_out[j].fd[1]);
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
            free_pipes(children_pipes, proc_count);
            free_parent_pipes(parent.parent_pipes_in, parent.parent_pipes_out);
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
            close(parent.parent_pipes_in[i].fd[1]);
            close(parent.parent_pipes_out[i].fd[0]);
        }

        k_proc_work(&parent);
        free_pipes(children_pipes, proc_count);
        free_parent_pipes(parent.parent_pipes_in, parent.parent_pipes_out);

        fclose(f);
    }
    return 0;
}

