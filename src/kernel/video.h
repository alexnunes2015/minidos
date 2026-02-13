#ifndef VIDEO_H
#define VIDEO_H

void cls();
void print_string(const char* str);
void print_char(char c);
void update_cursor();
int video_is_graphics();
int video_draw_indexed_image_centered(const unsigned char* pixels, int width, int height, const unsigned char* palette);
void video_draw_boot_gradient(unsigned int frame);

#endif
