/*
 * power_controller.c — Power state machine implementation.
 * Handles events (button press, power good signal, timeout) and transitions
 * the host through its power states, returning the hardware action to perform.
 */
#include <stdio.h>
#include "power_controller.h"

void power_controller_init(PowerController *controller) {
    controller->current_state = STATE_OFF;
}

PowerAction power_controller_handle_event(PowerController *controller, PowerEvent event) {

    switch (controller->current_state) {

        case STATE_OFF:
            if (event == EVENT_POWER_BUTTON_PRESSED) {
                controller->current_state = STATE_POWERING_ON;
                return ACTION_ASSERT_POWER_BUTTON;
            }
            break;

        case STATE_POWERING_ON:
            if (event == EVENT_POWER_GOOD_RECEIVED) {
                controller->current_state = STATE_ON;
                return ACTION_DEASSERT_POWER_BUTTON;
            }
            else if (event == EVENT_TIMEOUT) {
                controller->current_state = STATE_ERROR;
            }
            break;

        case STATE_ON:
            if (event == EVENT_SHUTDOWN_REQUESTED) {
                controller->current_state = STATE_SHUTTING_DOWN;
                return ACTION_ASSERT_POWER_BUTTON;
            }
            break;

        case STATE_SHUTTING_DOWN:
            if (event == EVENT_POWER_GOOD_RECEIVED) {
                controller->current_state = STATE_OFF;
                return ACTION_DEASSERT_POWER_BUTTON;
            }
            break;

        case STATE_ERROR:
            break;
    }

    return ACTION_NONE;
}


PowerState power_controller_get_state(PowerController *controller) {
    return controller->current_state;
}
