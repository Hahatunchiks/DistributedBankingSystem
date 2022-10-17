
#include "process.h"
#include "pa2345.h"
#include "common.h"

void transfer(void *parent_data, local_id src, local_id dst,
              balance_t amount) {
    // student, please implement me
}

void log_pipes(struct proc_pipe *pipes, local_id id, int count) {
    FILE *f = fopen(pipes_log, "a+");
    for (int i = 0; i < count; i++) {
        if (i != id) {
            fprintf(f, "pipes: fd[0]: %d fd[1]: %d processes id: %d  %d\n", pipes[i].fd[0], pipes[i].fd[1], id, i);
        }
    }
    fclose(f);
}

int child_proc_work(struct child_proc *child);

int main(int argc, char *argv[]) {

    char *arg_end;
    int proc_count = (int) strtol(argv[2], &arg_end, 10);
    printf("%d\n", proc_count);

    balance_t balances[proc_count];

    for (int i = 0; i < proc_count; i++) {
        *arg_end = 0;
        balances[i] = (balance_t) strtol(argv[i + 3], &arg_end, 10);
        printf("%d\n", balances[i]);
    }

    // parent proc init
    struct parent_proc k_process;
    k_process.id = -1;
    k_process.child_proc_count = proc_count;
    init_parent_pipes(&k_process);
    log_pipes(k_process.parent_pipes_in, k_process.id, k_process.child_proc_count);
    log_pipes(k_process.parent_pipes_out, k_process.id, k_process.child_proc_count);


    struct child_proc children[proc_count];
    for (int i = 0; i < proc_count; i++) {

        children[i].proc_count = (local_id) proc_count;
        children[i].id = (local_id) i;
        children[i].balance_state.s_balance = balances[i];
        children[i].balance_state.s_time = 1; // todo : use get_physical_time() from libruntime.so

        init_child_pipes(&children[i]);
        log_pipes(children[i].children_pipes, children[i].id, proc_count);
        children[i].parent_pipes_in = k_process.parent_pipes_out[i];
        children[i].parent_pipes_out = k_process.parent_pipes_in[i];
    }

    // child proc init
    pid_t proc_id;
    for (int i = 0; i < proc_count; i++) {
        proc_id = fork();
        if (proc_id == 0) {
            child_proc_work(&children[i]);
            break;
        }
    }
    return 0;
}


int child_proc_work(struct child_proc *child) {

    pid_t p = getpid();
    pid_t pp = getppid();

    // start signal
    Message message;
    message.s_header.s_type = STARTED;

    sprintf(message.s_payload, log_started_fmt, child->balance_state.s_time, child->id, p, pp, child->balance_state.s_balance);
    message.s_header.s_payload_len = strlen(message.s_payload);
    int result = send_multicast(child, &message);
    if (result < 0) {
        return -1;
    }

    return 0;
}
