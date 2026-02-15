#ifndef KEYBOARD_H
#define KEYBOARD_H

void keyboard_read_line(char* buffer, int max_len);
char keyboard_get_char();
int keyboard_try_get_char(char* out);
void keyboard_init(void);
void keyboard_handle_irq(void);
void keyboard_set_irq_mode(int enabled);

#endif
