#ifndef BOOT_SPLASH_H
#define BOOT_SPLASH_H

void boot_splash_begin(void);
void boot_splash_try_load_logo(void);
void boot_splash_pump(void);
void boot_splash_finish(void);
int boot_splash_is_active(void);

#endif
