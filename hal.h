#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    RKEY_NONE = 0,
    RKEY_UP,
    RKEY_DOWN,
    RKEY_LEFT,
    RKEY_RIGHT,
    RKEY_PGUP,
    RKEY_PGDN,
    RKEY_OK,        /* Enter */
    RKEY_SPACE,     /* Space */
    RKEY_BACK,      /* ESC */
    RKEY_BACKSPACE, /* Backspace */
    RKEY_TAB,       /* Tab: 切換書架/設定分頁 */
    RKEY_SHELF,     /* B: 返回書架 */
    RKEY_OPEN,      /* O: 匯入/開啟書籍 */
    RKEY_TOC,       /* C: 章節目錄 */
    RKEY_JUMP,      /* G: 跳頁 */
    RKEY_THEME,     /* T: 切換主題 */
    RKEY_SPACING,   /* L: 切換行距 */
    RKEY_MENU,      /* M: 閱讀選單 */
    RKEY_DELETE,    /* D: 移除書籍/資料夾 */
    RKEY_BOOKMARK,  /* K: 書籤 */
    RKEY_SEARCH,    /* F: 閱讀時全文搜尋 / 書架時格式篩選 */
    RKEY_REFLOW,    /* R: PDF 原版/文字重排切換 */
    RKEY_AUTO,      /* A: 自動翻頁開關 */
    RKEY_FULLSCREEN,/* I: 全螢幕沉浸模式 */
    RKEY_HELP,      /* H / ?: 按鍵說明與裝置資訊 */
    RKEY_NEW_FOLDER,/* N: 新增資料夾 */
    RKEY_MOVE_FOLDER,/* V: 移動書籍至資料夾 */
    RKEY_ZOOM_IN,   /* + / = / Z: PDF 放大 / 文字加大 */
    RKEY_ZOOM_OUT,  /* - / X: PDF 縮小 / 文字縮小 */
    RKEY_ZOOM_RESET,/* 0: PDF 重置為適應整頁 */
    RKEY_TAB1,      /* 1 */
    RKEY_TAB2,      /* 2 */
    RKEY_CHAR       /* 一般鍵盤字元輸入 */
} ReaderKey;

int  hal_get_width(void);
int  hal_get_height(void);
int  hal_is_xrgb(void);
int  hal_get_last_keycode(void);

void hal_clear(uint32_t rgb);
void hal_put_pixel(int x, int y, uint32_t rgb);
void hal_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void hal_draw_rect(int x, int y, int w, int h, uint32_t rgb);
void hal_fill_circle(int cx, int cy, int r, uint32_t rgb);
void hal_draw_circle(int cx, int cy, int r, uint32_t rgb);
void hal_draw_line(int x0, int y0, int x1, int y1, uint32_t rgb);

void hal_blit_rgb888_scaled(const uint8_t *rgb, int img_w, int img_h,
                            int dst_x, int dst_y, int dst_w, int dst_h,
                            int clip_x, int clip_y, int clip_w, int clip_h,
                            int invert_colors);

void hal_set_font_style(int style, int size_idx);
void hal_set_body_bounds(int y_top, int y_bot);
void hal_set_text_bg(uint32_t rgb);
void hal_draw_line_utf8(int x, int y, const char *utf8_text, int byte_len, int max_px_w, uint32_t rgb);
void hal_clear_text_in_rect(int rx, int ry, int rw, int rh);
void hal_dim_rect(int rx, int ry, int rw, int rh);

void hal_present(void);

int  hal_draw_image_file(const char *filepath, int x, int y, int w, int h);

const char *hal_get_library_path(void);
void hal_get_local_time(int *y, int *m, int *d, int *hh, int *mm, int *ss);
int  hal_open_file_dialog(char *out_path, int max_len);
int  hal_find_sibling_image(const char *current_path, int delta, char *out_path, int max_len);
void hal_request_quit(void);

#endif
