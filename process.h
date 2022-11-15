#ifndef PA2_PROCESS_H
#define PA2_PROCESS_H

#include "common.h"
#include "io.h"
#include "clock.h"

void update_msg_local_time(Message *msg) {
    increment_lamport_time();
    msg->s_header.s_local_time = get_lamport_time();
}

void save_balance_history(struct proc *child, timestamp_t t, balance_t s_balance_pending_in) {
    child->balance.s_time = t;
    child->balance.s_balance_pending_in = s_balance_pending_in;
    child->history.s_history[t] = child->balance;
}

int send_balance_history(struct proc *child) {
    Message history_message;
    memset(history_message.s_payload, 0, MAX_PAYLOAD_LEN);

    history_message.s_header.s_type = BALANCE_HISTORY;
    history_message.s_header.s_payload_len = sizeof(BalanceState) * child->history.s_history_len + sizeof(uint8_t) * 2;
    memcpy(history_message.s_payload, &child->history,
           sizeof(BalanceState) * child->history.s_history_len + sizeof(uint8_t) * 2);
    history_message.s_header.s_magic = MESSAGE_MAGIC;
    update_msg_local_time(&history_message);
    return send(child, PARENT_ID, &history_message);
}


void make_balance_snapshot(struct proc *child, balance_t s_balance_pending_in, timestamp_t local_time) {
    for (timestamp_t t = local_time; t < get_lamport_time(); t++) {
        save_balance_history(child, t, s_balance_pending_in);
    }
    child->history.s_history_len = get_lamport_time();
}


int send_money(struct proc *child, TransferOrder *received_transfer, Message *new_message) {
    child->balance.s_balance = (balance_t) (child->balance.s_balance - received_transfer->s_amount);
    update_msg_local_time(new_message);
    if (send(child, received_transfer->s_dst, new_message) < 0) {
        return -1;
    }

    char log_transfer[MAX_PAYLOAD_LEN];
    sprintf(log_transfer, log_transfer_out_fmt, get_lamport_time(), received_transfer->s_src,
            received_transfer->s_amount, received_transfer->s_dst);
    log_event(events_log, log_transfer);
    return 0;
}


int receive_money(struct proc *child, TransferOrder *received_transfer, Message *new_message) {
    char log_transfer[MAX_PAYLOAD_LEN];
    sprintf(log_transfer, log_transfer_in_fmt, get_lamport_time(), received_transfer->s_dst,
            received_transfer->s_amount, received_transfer->s_src);
    log_event(events_log, log_transfer);
    make_balance_snapshot(child, received_transfer->s_amount, new_message->s_header.s_local_time - 1);
    printf("%d\n", new_message->s_header.s_local_time);
    printf("%d\n", get_lamport_time());
    child->balance.s_balance = (balance_t) (child->balance.s_balance + received_transfer->s_amount);
    new_message->s_header.s_local_time = child->balance.s_time;
    new_message->s_header.s_type = ACK;
    memset(new_message->s_payload, 0, new_message->s_header.s_payload_len);
    new_message->s_header.s_payload_len = 0;

    update_msg_local_time(new_message);
    return send(child, PARENT_ID, new_message);
}

int handle_transfer(struct proc *child, Message *new_message) {
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
