//
// Created by mike on 12.11.22.
//

#ifndef PA2_CLOCK_H
#define PA2_CLOCK_H
#include "ipc.h"

static timestamp_t curr_local_time;

timestamp_t get_lamport_time() {
    return curr_local_time;
}

void increment_lamport_time() {
    curr_local_time++;
}

void  update_lamport_time(const Message *msg) {
    if (curr_local_time < msg->s_header.s_local_time) {
        curr_local_time = msg->s_header.s_local_time;
    }
    increment_lamport_time();
}



#endif //PA2_CLOCK_H
