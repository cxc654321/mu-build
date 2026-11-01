/* Copyright (c) 2026 Aurora Silicon */
/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <string.h>

#include "../Silicon/Apple/AppleSiliconPkg/Drivers/AppleNANDStorageDxe/Shared/AppleNvmeCore.h"

static int failures;

static void check(int condition, const char *message)
{
    if (condition) {
        printf("PASS: %s\n", message);
    } else {
        printf("FAIL: %s\n", message);
        failures++;
    }
}

int main(void)
{
    struct ntasi_ans_sqe command;
    struct ntasi_ans_tcb tcb;

    check(ntasi_ans_hw_t604x.secure_io_queue_registers,
          "T6040/T6041 selects the secure queue-admission window");
    check(!ntasi_ans_hw_t604x.linear_sq_ctrl_present &&
              !ntasi_ans_hw_t604x.prp_null_check_ctrl_present &&
              !ntasi_ans_hw_t604x.max_pend_cmds_ctrl_present,
          "T6040/T6041 never accesses deleted legacy ANS controls");

    memset(&command, 0, sizeof(command));
    command.opcode = NTASI_ANS_ADMIN_CMD_CREATE_CQ;
    command.tag = 7;
    ntasi_ans_tcb_fill(&tcb, &command, NTASI_ANS_DMA_TO_DEVICE);
    check(tcb.dma_flags == 0,
          "a command with PRP1 zero has no TCB DMA direction flags");
    check(tcb.opcode == command.opcode && tcb.command_id == command.tag,
          "a no-data TCB still mirrors opcode and command tag");

    command.prp1 = UINT64_C(0x12345000);
    ntasi_ans_tcb_fill(&tcb, &command, NTASI_ANS_DMA_FROM_DEVICE);
    check(tcb.dma_flags == NTASI_ANS_TCB_DMA_FROM_DEVICE,
          "a mapped command retains its requested DMA direction");
    check(tcb.prp1 == command.prp1,
          "a mapped command retains PRP1 in the TCB");

    if (failures != 0)
        return 1;
    puts("T6040 ANS contract: all assertions passed");
    return 0;
}
