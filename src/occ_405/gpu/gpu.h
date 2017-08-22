/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/occ_405/gpu/gpu.h $                                       */
/*                                                                        */
/* OpenPOWER OnChipController Project                                     */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2011,2017                        */
/* [+] International Business Machines Corp.                              */
/*                                                                        */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/*                                                                        */
/* IBM_PROLOG_END_TAG                                                     */

#ifndef _GPU_H
#define _GPU_H

#include <occ_common.h>
#include <trac_interface.h>
#include <errl.h>
#include <rtls.h>
#include "gpu_structs.h"

#define GPU_TICK (CURRENT_TICK % MAX_NUM_TICKS)

// States for the GPU state machine (task_gpu_sm)
typedef enum
{
    GPU_STATE_RESET                     = 0x00, // Reset and initialize interface
    GPU_STATE_READ_TEMP                 = 0x10, // Read GPU core temperature
    GPU_STATE_READ_MEMORY_TEMP          = 0x20, // Read GPU memory temperature
    GPU_STATE_CHECK_MEM_TEMP_CAPABLE    = 0x30, // Read memory temperature capability
    GPU_STATE_CHECK_DRIVER_LOADED       = 0x40, // Check if Driver loaded
    GPU_STATE_READ_PWR_LIMIT            = 0x50, // Read Power Limits
    GPU_STATE_SET_PWR_LIMIT             = 0x60, // Set Power Limit
    GPU_STATE_IDLE                      = 0xFE, // Ok to schedule new task
    GPU_STATE_NO_LOCK                   = 0xFF  // Host owns, no communication allowed
} gpuState_e;

// States for the GPU reset state machine (gpu_reset_sm)
typedef enum
{
    GPU_RESET_STATE_NEW                       = 0x01, // new reset attempt
    GPU_RESET_STATE_INIT_BUS                  = 0x02,
    GPU_RESET_STATE_RESET_MASTER              = 0x03, // Reset master
    GPU_RESET_STATE_RESET_SLAVE               = 0x04, // Start of slave port 4 reset
    GPU_RESET_STATE_RESET_SLAVE_WAIT          = 0x05,
    GPU_RESET_STATE_RESET_SLAVE_COMPLETE      = 0x06,
    GPU_RESET_STATE_RESET_FINISH              = 0x07,
} gpuResetState_e;

// States for reading GPU core temperature (gpu_read_temp_sm)
typedef enum
{
    GPU_STATE_READ_TEMP_NEW      = 0x11, // new temp read
    GPU_STATE_READ_TEMP_START    = 0x12, // start write temp reg
    GPU_STATE_READ_TEMP_FINISH   = 0x13, // read temperature
    GPU_STATE_READ_TEMP_COMPLETE = 0x14, // store temperature read
} gpuReadTempState_e;

// States for reading GPU memory temperature (gpu_read_mem_temp_sm)
typedef enum
{
    GPU_STATE_READ_MEM_TEMP_NEW      = 0x21,
    GPU_STATE_READ_MEM_TEMP_START    = 0x22,
    GPU_STATE_READ_MEM_TEMP_STOP     = 0x23,
    GPU_STATE_READ_MEM_TEMP_READ     = 0x24,
    GPU_STATE_READ_MEM_TEMP_COMPLETE = 0x25,
} gpuReadMemTempState_e;

// States for checking GPU memory temperature capability (gpu_read_mem_temp_capability_sm)
typedef enum
{
    GPU_STATE_READ_MEM_TEMP_CAPABLE_NEW      = 0x31,
    GPU_STATE_READ_MEM_TEMP_CAPABLE_START    = 0x32,
    GPU_STATE_READ_MEM_TEMP_CAPABLE_2        = 0x33,
    GPU_STATE_READ_MEM_TEMP_CAPABLE_3        = 0x34,
    GPU_STATE_READ_MEM_TEMP_CAPABLE_READ     = 0x35,
    GPU_STATE_READ_MEM_TEMP_CAPABLE_COMPLETE = 0x36,
} gpuReadMemTempCapableState_e;

// GPU IPC initialization
void gpu_ipc_init();

// GPU state machine
void task_gpu_sm(struct task *i_self);


#endif //_GPU_H
