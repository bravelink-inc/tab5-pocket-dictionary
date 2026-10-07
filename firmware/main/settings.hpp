#pragma once
// Small persistent settings (NVS).
void settings_init();
int  settings_get_rotation(int fallback);
void settings_set_rotation(int rotation);
