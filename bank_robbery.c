/**
 * @file     bank_robbery.c
 * @Author   Michael Kosyakov and Evgeniy Ivanov (ifmo.distributedclass@gmail.com)
 * @date     March, 2014
 * @brief    Toy implementation of bank_robbery(), don't do it in real life ;)
 *
 * Students must not modify this file!
 */

#include <stdio.h>
#include "banking.h"

void bank_robbery(void * parent_data, local_id max_id)
{
    for (local_id i = 0; i < max_id; ++i) {
        transfer(parent_data, i, (local_id)(i + 1), i);
    }
    if (max_id > 0) {
        transfer(parent_data, max_id, 0, 1);
    }
}
