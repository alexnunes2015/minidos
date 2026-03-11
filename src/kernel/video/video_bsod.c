#include "video_internal.h"

static int str_len_local(const char* s) {
    int n = 0;
    while (s && s[n] != '\0') {
        n++;
    }
    return n;
}

static void draw_string_centered_custom(int row, const char* s, u32 fg, u32 bg) {
    int len = str_len_local(s);
    int col = (text_cols - len) / 2;
    if (col < 0) {
        col = 0;
    }
    draw_string_custom(col, row, s, fg, bg);
}

void video_show_bsod(const char* stop_code, const char* detail) {
    const char* stop = stop_code ? stop_code : "0E : 016F : BFF93BD4";
    const char* extra = detail ? detail : "A TEST FATAL EXCEPTION HAS OCCURRED.";

    init_video_once();

    if (graphics_mode) {
        const u32 bg = 0x0000AAu;
        const u32 fg = 0xFFFFFFu;

        clear_graphics(bg);
        draw_string_centered_custom(3, " MINI DOS ", bg, 0xB8B8B8u);
        draw_string_custom(4, 6, "AN ERROR HAS OCCURRED. TO CONTINUE:", fg, bg);
        draw_string_custom(4, 8, "PRESS ENTER TO RETURN TO SHELL, OR", fg, bg);
        draw_string_custom(4, 10, "PRESS CTRL+ALT+DEL TO RESTART YOUR COMPUTER. IF YOU DO THIS,", fg, bg);
        draw_string_custom(4, 11, "YOU WILL LOSE ANY UNSAVED INFORMATION IN ALL OPEN APPLICATIONS.", fg, bg);
        draw_string_custom(4, 13, "ERROR: ", fg, bg);
        draw_string_custom(11, 13, stop, fg, bg);
        draw_string_custom(4, 15, extra, fg, bg);
        draw_string_centered_custom(18, "PRESS ANY KEY TO CONTINUE _", fg, bg);
        video_note_dirty(0, 0, fb_width, fb_height);
        video_maybe_present_pending();
        return;
    }

    {
        volatile u8* video = TEXT_VIDEO_MEMORY;
        const u8 attr = 0x1F;
        int pos = 0;

        for (int y = 0; y < TEXT_SCREEN_HEIGHT; y++) {
            for (int x = 0; x < TEXT_SCREEN_WIDTH; x++) {
                video[pos++] = ' ';
                video[pos++] = attr;
            }
        }

        const char* lines[] = {
            "                                  MINI DOS                                   ",
            "",
            " AN ERROR HAS OCCURRED. TO CONTINUE:",
            "",
            " PRESS ENTER TO RETURN TO SHELL, OR",
            "",
            " PRESS CTRL+ALT+DEL TO RESTART YOUR COMPUTER. IF YOU DO THIS,",
            " YOU WILL LOSE ANY UNSAVED INFORMATION IN ALL OPEN APPLICATIONS.",
            "",
            " ERROR: ",
            "",
            "                          PRESS ANY KEY TO CONTINUE _",
            0
        };

        int row = 1;
        for (int li = 0; lines[li] != 0 && row < TEXT_SCREEN_HEIGHT; li++, row++) {
            const char* s = lines[li];
            for (int x = 0; s[x] != '\0' && x < TEXT_SCREEN_WIDTH; x++) {
                int off = (row * TEXT_SCREEN_WIDTH + x) * 2;
                video[off] = (u8)s[x];
                video[off + 1] = attr;
            }
        }

        row = 9;
        for (int x = 0; stop[x] != '\0' && (8 + x) < TEXT_SCREEN_WIDTH; x++) {
            int off = (row * TEXT_SCREEN_WIDTH + 8 + x) * 2;
            video[off] = (u8)stop[x];
            video[off + 1] = attr;
        }

        row = 11;
        for (int x = 0; extra[x] != '\0' && (1 + x) < TEXT_SCREEN_WIDTH; x++) {
            int off = (row * TEXT_SCREEN_WIDTH + 1 + x) * 2;
            video[off] = (u8)extra[x];
            video[off + 1] = attr;
        }
    }
}
