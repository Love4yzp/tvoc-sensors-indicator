#pragma once

/* Call once before entering the main loop. */
void mock_sensors_start(void);

/* Call each iteration of the main loop to advance mock timers. */
void mock_sensors_tick(void);
