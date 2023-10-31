/***************************************************************************
Copyright © 2023 Shell M. Shrader <shell at shellware dot com>
----------------------------------------------------------------------------
This work is free. You can redistribute it and/or modify it under the
terms of the Do What The Fuck You Want To Public License, Version 2,
as published by Sam Hocevar. See the COPYING file for more details.
****************************************************************************/
#include "Bootstrap.h"

#ifdef ENABLE_DEBUG
    TelnetSpy SerialAndTelnet;
    Bootstrap bs = Bootstrap(PROJECT_NAME, &SerialAndTelnet, 1500000);
#else
    Bootstrap bs = Bootstrap(PROJECT_NAME);
#endif

#define STATION_ID_LEN 100

typedef struct my_config_type : config_type {
    tiny_int station_id_flag;
    char station_id[STATION_ID_LEN];
} MY_CONFIG_TYPE;

MY_CONFIG_TYPE my_config;

