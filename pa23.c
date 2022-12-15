#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <getopt.h>

#include "common.h"
#include "io.h"
#include "clock.h"
#include "mutex.h"

static char use_mutex = 0;

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

    for (local_id i = 0; i < 5 * child->id; i++) {
        char log_loop_string[MAX_PAYLOAD_LEN];
        sprintf(log_loop_string, log_loop_operation_fmt, child->id, i + 1, 5 * child->id);
        if (use_mutex) {
            request_cs(child);
        }
        print(log_loop_string);
      //  log_event(events_log, log_loop_string);
        if (use_mutex) {
            release_cs(child);
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

    local_id done_received = child->done_counter;
    while (1) {
        for (int i = 1; i <= child->proc_count; i++) {
            if (i == child->id) continue;
            Message recv_done_message;
            if (receive(child, (local_id) i, &recv_done_message) < 0) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    continue;
                }
                return -1;
            }

            if (recv_done_message.s_header.s_type == DONE) {
                done_received++;
            } else if (recv_done_message.s_header.s_type == CS_REQUEST) {
                Message reply;
                reply.s_header.s_magic = MESSAGE_MAGIC;
                reply.s_header.s_type = CS_REPLY;
                reply.s_header.s_payload_len = 0;
                update_msg_local_time(&reply);
                while (1) {
                    if (send(child, (local_id) i, &reply) < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                        return -1;
                    }
                    break;
                }
            }
        }
        if (done_received == child->proc_count - 1) break;
    }
    sprintf(log_all_buff, log_received_all_done_fmt, get_lamport_time(), child->id);
    log_event(events_log, log_all_buff);
    return 0;
}

void k_proc_work(struct proc *parent) {
    Message start_message;
    if (receive_all(parent, &start_message) < 0) {
        return;
    }

    Message done_message;
    if (receive_all(parent, &done_message) < 0) {
        return;
    }

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

    local_id proc_count ;
    const struct option long_options[] = {
            { "mutexl", no_argument, (int*) &use_mutex, 1 },
            { NULL, 0, NULL, 0 }
    };

    int option = 0;
    int long_idx = -1;
    while ((option = getopt_long(argc, argv, "p:", long_options, &long_idx)) != -1) {
        if (option == 'p') {
            proc_count = (local_id)strtoul(optarg, NULL, 10);
            if (proc_count == 0 || errno != 0) {
                perror("ERROR while performing conversion of parameter p");
                return EINVAL;
            }
        }
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
        processes[i].balance.s_time = 0;
        processes[i].balance.s_balance_pending_in = 0;
        processes[i].balance.s_balance = 0;
        processes[i].history.s_history_len = 0;
        processes[i].history.s_id = processes[i].id;
        processes[i].done_counter = 0;
        for (int k = 0; k <= proc_count; k++) {
            processes->dr[k] = 0;
        }
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
            for (int j = 0; j <= proc_count; j++) {
                if (j != i) {
                    for (int k = 0; k <= proc_count; k++) {
                        if (k != j) {
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
        for (int j = 0; j <= proc_count; j++) {
            if (j != PARENT_ID) {
                for (int k = 0; k <= proc_count; k++) {
                    if (k != j) {
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
