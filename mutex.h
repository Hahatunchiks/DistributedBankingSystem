#ifndef PA2_MUTEX_H
#define PA2_MUTEX_H

#include "io.h"

int min_request(const void *self) {
    struct proc *process = (struct proc *) self;
    local_id min = 0;
    timestamp_t min_time = process->queue[0];
    for (local_id i = 0; i < process->proc_count; i++) {
        if (min_time > process->queue[i] || min_time == -1) {
            min_time = process->queue[i];
            min = i;
        }
    }

    return min + 1;
}

int request_cs(const void *self) {
    struct proc *process = (struct proc *) self;

    Message request_message;
    request_message.s_header.s_type = CS_REQUEST;
    request_message.s_header.s_payload_len = 0;
    request_message.s_header.s_magic = MESSAGE_MAGIC;
    update_msg_local_time(&request_message);

    process->queue[process->id - 1] = get_lamport_time();

    if (send_multicast(process, &request_message) < 0) {
        return -1;
    }

    local_id count = 0;
    while (1) {
        for (int i = 1; i <= process->proc_count; i++) {
            if (i == process->id) continue;
            Message reply_message;
            if (receive(process, (local_id) i, &reply_message) < 0) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    continue;
                }
                return -1;
            }

            if (reply_message.s_header.s_type == CS_REQUEST) {
                process->queue[i - 1] = reply_message.s_header.s_local_time;
                Message reply;
                reply.s_header.s_type = CS_REPLY;
                update_msg_local_time(&reply);
                reply.s_header.s_magic = MESSAGE_MAGIC;
                reply.s_header.s_payload_len = 0;

                while (1) {
                    if (send(process, (local_id) i, &reply) < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                        return -1;
                    }
                    break;
                }

            } else if (reply_message.s_header.s_type == CS_REPLY) { // msg time > request_message time
                count++;

            } else if (reply_message.s_header.s_type == CS_RELEASE) {
                process->queue[i-1] = -1;
            } else if (reply_message.s_header.s_type == DONE) {
                process->done_counter++;
            }
        }

        if (count == process->proc_count - 1 && min_request(process) == process->id) {
            break;
        }
    }

    return 0;
}

int release_cs(const void *self) {
    Message release_message;
    release_message.s_header.s_type = CS_RELEASE;
    release_message.s_header.s_payload_len = 0;
    release_message.s_header.s_magic = MESSAGE_MAGIC;
    update_msg_local_time(&release_message);

    struct proc *process = (struct proc *) self;
    process->queue[process->id - 1] = -1;
    return send_multicast(process, &release_message);
}

#endif //PA2_MUTEX_H
