/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * RTX5 Thread Tracking Support for Orbuculum
 * ==========================================
 * Host-side header for decoding RTX5 (CMSIS-RTOS2) data structures
 * on a 32-bit ARMv7-M target (Cortex-M7). Keep ONLY target layout here.
 */

#ifndef _RTX5_H_
#define _RTX5_H_

#include <stdint.h>
#include <rtos_support.h>

/* --------------------------------------------------------------------------
 * Target layout assumptions
 * --------------------------------------------------------------------------
 * - ARMv7-M (Cortex-M7) 32-bit
 * - Little-endian
 * - Natural alignment (no packed attributes in the target)
 *
 * All offsets below are ABSOLUTE from the symbol base of the corresponding
 * target object (e.g., &osRtxInfo). Use them as 32-bit addresses; do NOT
 * reinterpret with host pointer sizes.
 */

/* osRtxInfo (global RTOS runtime) — absolute offsets from &osRtxInfo */
#define RTX5_INFO_OS_ID_OFFSET              0x00u /* const char* */
#define RTX5_INFO_VERSION_OFFSET            0x04u /* uint32_t    */
#define RTX5_INFO_KERNEL_OFFSET             0x08u /* struct {...} */
#define RTX5_INFO_TICK_IRQN_OFFSET          0x10u /* int32_t     */

/* thread sub-structure (inside osRtxInfo) */
#define RTX5_INFO_THREAD_OFFSET             0x14u /* struct {...} */

/* thread.run sub-structure */
#define RTX5_INFO_THREAD_RUN_OFFSET         0x14u
#define RTX5_INFO_THREAD_RUN_CURR_OFFSET    0x14u /* osRtxInfo.thread.run.curr (osRtxThread_t*) */
#define RTX5_INFO_THREAD_RUN_NEXT_OFFSET    0x18u /* osRtxInfo.thread.run.next (osRtxThread_t*) */

/* Convenience macros (address arithmetic on 32-bit target addresses) */
#define RTX5_ADDR(base, off)                ((uint32_t)((base) + (off)))
#define RTX5_ADDR_CURR(base)                RTX5_ADDR((base), RTX5_INFO_THREAD_RUN_CURR_OFFSET)
#define RTX5_ADDR_NEXT(base)                RTX5_ADDR((base), RTX5_INFO_THREAD_RUN_NEXT_OFFSET)

/* RTX5 Thread Control Block (TCB) — offsets from a thread control block base  */
#define RTX5_THREAD_ID_OFFSET               0    /* uint8_t  id (1=thread,2=timer,...)          */
#define RTX5_THREAD_STATE_OFFSET            1    /* uint8_t  state                               */
#define RTX5_THREAD_FLAGS_OFFSET            2    /* uint8_t  flags                               */
#define RTX5_THREAD_ATTR_OFFSET             3    /* uint8_t  attributes                          */
#define RTX5_THREAD_NAME_OFFSET             4    /* uint32_t pointer to name                     */
#define RTX5_THREAD_THREAD_NEXT_OFFSET      8    /* uint32_t next in thread list                 */
#define RTX5_THREAD_THREAD_PREV_OFFSET      12   /* uint32_t prev in thread list                 */
#define RTX5_THREAD_DELAY_NEXT_OFFSET       16   /* uint32_t next in delay list                  */
#define RTX5_THREAD_DELAY_PREV_OFFSET       20   /* uint32_t prev in delay list                  */
#define RTX5_THREAD_THREAD_JOIN_OFFSET      24   /* uint32_t waiting-to-join thread              */
#define RTX5_THREAD_DELAY_OFFSET            28   /* uint32_t delay time                          */
#define RTX5_THREAD_PRIORITY_OFFSET         32   /* int8_t   priority                            */
#define RTX5_THREAD_PRIORITY_BASE_OFFSET    33   /* int8_t   base priority                       */
#define RTX5_THREAD_STACK_FRAME_OFFSET      34   /* uint8_t  stack frame type                    */
#define RTX5_THREAD_FLAGS_OPTIONS_OFFSET    35   /* uint8_t  flags options                       */
#define RTX5_THREAD_WAIT_FLAGS_OFFSET       36   /* uint32_t waiting flags                       */
#define RTX5_THREAD_THREAD_FLAGS_OFFSET     40   /* uint32_t thread flags                        */
#define RTX5_THREAD_MUTEX_LIST_OFFSET       44   /* uint32_t owned mutex list ptr                */
#define RTX5_THREAD_STACK_MEM_OFFSET        48   /* uint32_t stack memory ptr                    */
#define RTX5_THREAD_STACK_SIZE_OFFSET       52   /* uint32_t stack size                          */
#define RTX5_THREAD_SP_OFFSET               56   /* uint32_t current SP                          */
#define RTX5_THREAD_THREAD_ADDR_OFFSET      60   /* uint32_t entry function address              */
#define RTX5_THREAD_TZ_MEMORY_OFFSET        64   /* uint32_t TZ memory id                        */
#define RTX5_THREAD_TZ_MODULE_OFFSET        68   /* uint32_t TZ module id                        */
#define RTX5_THREAD_RESERVED_OFFSET         72   /* uint32_t reserved                            */
#define RTX5_THREAD_ZONE_OFFSET             76   /* uint32_t zone number                         */

#define RTX5_THREAD_CB_SIZE                 80   /* total size of TCB in bytes                   */

/* Thread states */
#define RTX5_THREAD_INACTIVE                0
#define RTX5_THREAD_READY                   1
#define RTX5_THREAD_RUNNING                 2
#define RTX5_THREAD_BLOCKED                 3
#define RTX5_THREAD_TERMINATED              4

/* Object identifiers */
#define RTX5_ID_INVALID                     0x00u
#define RTX5_ID_THREAD                      0xF1u
#define RTX5_ID_TIMER                       0xF2u
#define RTX5_ID_EVENTFLAGS                  0xF3u
#define RTX5_ID_MUTEX                       0xF5u
#define RTX5_ID_SEMAPHORE                   0xF6u
#define RTX5_ID_MEMPOOL                     0xF7u
#define RTX5_ID_MESSAGE                     0xF9u
#define RTX5_ID_MESSAGEQUEUE                0xFAu

struct rtx5_info {
    uint32_t thread_run_curr;    /* current thread pointer (target address) */
    uint32_t thread_run_next;    /* next thread pointer (target address)    */
};

struct rtx5_thread_track {
    uint32_t thread_addr;        /* TCB base address on target              */
    char     name[64];           /* copied name                             */
    uint32_t thread_func;        /* entry function                          */
    uint8_t  priority;           /* priority                                */
    uint8_t  state;              /* state                                   */
    uint64_t total_cycles;       /* stats                                   */
    uint64_t run_count;
    uint64_t last_scheduled;
    uint64_t min_runtime;
    uint64_t max_runtime;
    uint32_t stack_usage;
    struct rtx5_thread_track *next;
    struct rtx5_thread_track *prev;
};


const char *rtx5GetPriorityName(int8_t priority);
const struct rtosOps *rtx5GetOps(void);
#endif /* _RTX5_H_ */