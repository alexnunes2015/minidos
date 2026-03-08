#ifndef MOUSE_H
#define MOUSE_H

typedef struct {
    int x;
    int y;
    int dx;
    int dy;
    unsigned int buttons;
    unsigned int seq;
    int present;
} mouse_state_t;

void mouse_init(void);
void mouse_handle_irq(void);
int mouse_get_state(mouse_state_t* out);

#endif
