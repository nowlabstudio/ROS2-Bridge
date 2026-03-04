#ifndef USER_CHANNELS_H
#define USER_CHANNELS_H

/**
 * Regisztráld ide az összes aktív csatornát.
 * Ez az egyetlen fájl, amit rendszeresen szerkesztesz.
 *
 * Példa:
 *   #include "motor_left.h"
 *   channel_register(&motor_left_channel);
 */
void user_register_channels(void);

#endif /* USER_CHANNELS_H */
