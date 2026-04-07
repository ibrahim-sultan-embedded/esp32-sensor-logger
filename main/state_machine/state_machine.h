/**
 * @file state_machine.h
 * @brief Temperature monitoring state machine.
 *
 * States:
 *
 *         +----------+
 *    +--->|  NORMAL  |<---+
 *    |    +----+-----+    |
 *    |         | temp>35  |
 *    |    +----v-----+    |
 *    |    | WARNING  |----+ temp<33 (hysteresis)
 *    |    +----+-----+
 *    |         | temp>45
 *    |    +----v-----+
 *    +----| CRITICAL |  temp<40
 *         +----+-----+
 *              | 3x CRC error
 *         +----v-----+
 *         |  FAULT   |  (requires manual reset via CLI)
 *         +----------+
 */

#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include "common_types.h"

typedef enum {
    SYS_STATE_NORMAL   = 0,
    SYS_STATE_WARNING  = 1,
    SYS_STATE_CRITICAL = 2,
    SYS_STATE_FAULT    = 3,
} sys_state_t;

/** Initialize state machine to NORMAL */
void state_machine_init(void);

/**
 * Feed a new sensor reading into the state machine.
 * @return The new system state after processing.
 */
sys_state_t state_machine_update(const sensor_data_t *data);

/** Force reset to NORMAL (used by CLI 'reset' command) */
void state_machine_reset(void);

/** Get current state without updating */
sys_state_t state_machine_get(void);

/** Human-readable state name */
const char *state_machine_name(sys_state_t state);

#endif /* STATE_MACHINE_H */
