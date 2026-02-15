#ifndef VIDEO_H
#define VIDEO_H

void cls();
void print_string(const char* str);
void print_char(char c);
void update_cursor();
int video_is_graphics();
int video_get_width();
int video_get_height();
void video_clear_color(unsigned int rgb);
void video_fill_rect(int x, int y, int w, int h, unsigned int rgb);
void video_draw_text_at(int x, int y, const char* text, unsigned int fg, unsigned int bg);
int video_draw_indexed_image_centered(const unsigned char* pixels, int width, int height, const unsigned char* palette);
void video_draw_boot_gradient(unsigned int frame);
void video_show_bsod(const char* stop_code, const char* detail);

#endif
