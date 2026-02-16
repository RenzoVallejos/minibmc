#ifndef POWER_CONTROLLER_H
#define POWER_CONTROLLER_H

#include <stdbool.h>

/* Power States */
typedef enum {
    STATE_OFF,
    STATE_POWERING_ON,
    STATE_ON,
    STATE_SHUTTING_DOWN,
    STATE_ERROR
} PowerState;

/* Events */
typedef enum {
    EVENT_POWER_BUTTON_PRESSED,
    EVENT_POWER_GOOD_RECEIVED,
    EVENT_SHUTDOWN_REQUESTED,
    EVENT_TIMEOUT,
    EVENT_FAILURE
} PowerEvent;

/* Actions */
typedef enum {
    ACTION_NONE,
    ACTION_ASSERT_POWER_BUTTON,
    ACTION_DEASSERT_POWER_BUTTON
} PowerAction;

/* Controller Structure */
typedef struct {
    PowerState current_state;
} PowerController;

/* API */
void power_controller_init(PowerController *controller);
PowerAction power_controller_handle_event(PowerController *controller, PowerEvent event);
PowerState power_controller_get_state(PowerController *controller);

#endif
