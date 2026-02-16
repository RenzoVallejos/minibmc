#include <stdio.h>
#include "power_controller.h"

void perform_action(PowerAction action) {

    switch (action) {
        case ACTION_ASSERT_POWER_BUTTON:
            printf("HAL: Power button asserted\n");
            break;

        case ACTION_DEASSERT_POWER_BUTTON:
            printf("HAL: Power button released\n");
            break;

        default:
            break;
    }
}

int main() {

    PowerController controller;
    power_controller_init(&controller);

    PowerAction action;

    action = power_controller_handle_event(&controller, EVENT_POWER_BUTTON_PRESSED);
    perform_action(action);

    action = power_controller_handle_event(&controller, EVENT_POWER_GOOD_RECEIVED);
    perform_action(action);

    action = power_controller_handle_event(&controller, EVENT_SHUTDOWN_REQUESTED);
    perform_action(action);

    action = power_controller_handle_event(&controller, EVENT_POWER_GOOD_RECEIVED);
    perform_action(action);

    return 0;
}
