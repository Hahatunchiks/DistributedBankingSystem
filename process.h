#ifndef PA2_PROCESS_H
#define PA2_PROCESS_H

#include "common.h"
#include "io.h"

int init_parent_pipes(struct proc_pipe *pipes_array, local_id count) {
    for (local_id i = 1; i <= count; i++) {
        int res = pipe(pipes_array[i].fd);
        if (res < 0) {
            return -1;
        }
    }
    return 0;
}

int init_parent(struct parent_proc *parent, local_id proc_count) {
    parent->id = PARENT_ID;
    parent->child_proc_count = (int) proc_count;
    parent->parent_pipes_in = malloc(sizeof(struct proc_pipe) * proc_count+1);
    parent->parent_pipes_out = malloc(sizeof(struct proc_pipe) * proc_count+1);

    if(init_parent_pipes(parent->parent_pipes_in, proc_count) < 0) return -1;
    return init_parent_pipes(parent->parent_pipes_out, proc_count);
}

int init_child(struct child_proc *children,  struct proc_pipe *children_pipes[], const local_id proc_count, const struct proc_pipe *parent_pipes_in, const struct proc_pipe *parent_pipes_out, const balance_t *balances) {

    for(local_id i = 1; i <= proc_count; i++)  {
        children_pipes[i] = malloc(sizeof(struct proc_pipe) * (proc_count+1));
    }

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
        children[i].balance.s_time = 0;
        children[i].balance.s_balance_pending_in = 0;
        children[i].history.s_history_len = 0;
        children[i].history.s_id = children[i].id;
        if (make_nonblock_pipes(&children[i]) < 0) {
            return -1;
        }
    }
    return 0;
}

void free_pipes(struct proc_pipe *children_pipes[], local_id proc_count) {
    for(local_id i = 1; i <= proc_count; i++)  {
        free(children_pipes[i]);
    }

}

void free_parent_pipes(struct proc_pipe *parent_pipes_in, struct proc_pipe *parent_pipes_out) {
    free(parent_pipes_in);
    free(parent_pipes_out);
}

int child_synchro_with_other(struct child_proc *child, int16_t h_type, const char *const log_fmt,
                             const char *const log_all_fmt) {
    Message message;
    message.s_header.s_type = h_type;
    sprintf(message.s_payload, log_fmt, get_physical_time(), child->id, child->balance.s_balance);
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

    char log_all_buff[MAX_PAYLOAD_LEN];
    sprintf(log_all_buff, log_all_fmt, get_physical_time(), child->id);
    if (log_event(events_log, log_all_buff) < 0) {
        return -1;
    }
    return 0;
}


void save_balance_history(struct child_proc *child, timestamp_t t) {
    child->balance.s_time = t;
    uint8_t balance_state_index = child->history.s_history_len;
    child->history.s_history[balance_state_index] = child->balance;
    child->history.s_history_len = balance_state_index + 1;
}


int send_balance_history(const struct child_proc *child) {
    Message history_message;
    memset(history_message.s_payload, 0, MAX_PAYLOAD_LEN);

    history_message.s_header.s_type = BALANCE_HISTORY;
    history_message.s_header.s_payload_len = sizeof(BalanceState) * child->history.s_history_len + sizeof(uint8_t) * 2;
    memcpy(history_message.s_payload, &child->history,
           sizeof(BalanceState) * child->history.s_history_len + sizeof(uint8_t) * 2);
    history_message.s_header.s_magic = MESSAGE_MAGIC;
    return send_full(child->parent_pipes_in.fd[1], &history_message);
}


void make_balance_snapshot(struct child_proc *child) {
    timestamp_t curr_time = get_physical_time();
    for (timestamp_t t = child->history.s_history_len; t < curr_time; t++) {
        save_balance_history(child, t);
    }
}


int send_money(struct child_proc *child, TransferOrder *received_transfer, Message *new_message) {
    child->balance.s_balance = (balance_t) (child->balance.s_balance - received_transfer->s_amount);
    if (send(child, received_transfer->s_dst, new_message) < 0) {
        return -1;
    }

    char log_transfer[MAX_PAYLOAD_LEN];
    sprintf(log_transfer, log_transfer_out_fmt, get_physical_time(), received_transfer->s_src,
            received_transfer->s_amount, received_transfer->s_dst);
    log_event(events_log, log_transfer);
    return 0;
}


int receive_money(struct child_proc *child, TransferOrder *received_transfer, Message *new_message) {
    char log_transfer[MAX_PAYLOAD_LEN];
    sprintf(log_transfer, log_transfer_in_fmt, get_physical_time(), received_transfer->s_dst,
            received_transfer->s_amount, received_transfer->s_src);
    log_event(events_log, log_transfer);

    child->balance.s_balance = (balance_t) (child->balance.s_balance + received_transfer->s_amount);

    new_message->s_header.s_local_time = child->balance.s_time;
    new_message->s_header.s_type = ACK;
    memset(new_message->s_payload, 0, new_message->s_header.s_payload_len);
    new_message->s_header.s_payload_len = 0;

    return send_full(child->parent_pipes_in.fd[1], new_message);
}

int handle_transfer(struct child_proc *child,  Message *new_message) {
    TransferOrder received_transfer;
    memcpy(&received_transfer, new_message->s_payload, new_message->s_header.s_payload_len);
    if (received_transfer.s_src == child->id) {
        return send_money(child, &received_transfer, new_message);

    } else if (received_transfer.s_dst == child->id) {
        return receive_money(child, &received_transfer, new_message);

    } else {
        return -1;
    }

}
#endif
