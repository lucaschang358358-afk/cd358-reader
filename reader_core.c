/*
 * CD-358 輕量純按鍵電子書閱讀器核心 (Small-Screen Pure-Keyboard Edition)
 * - 100% 保留原版 PDF 閱讀引擎 (pdf_engine.c) 與翻頁/縮放/平移/重排邏輯
 * - 完整支援 PDF / EPUB (含插圖與目錄) / TXT / PNG / BMP
 * - 完整保留書架、資料夾、格式篩選、書籤、目錄、跳頁、搜尋、12 套主題、自動翻頁
 * - 專為 320x240 ~ 480x320 小螢幕與無觸控純實體按鍵最佳化
 */

#include "reader_core.h"
#include "book_loader.h"
#include "pdf_engine.h"
#include "themes.h"
#include "ui_gfx.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    SCR_HOME = 0,
    SCR_READ_TEXT,
    SCR_READ_PDF,
    SCR_READ_IMG,
    SCR_READ_MENU,
    SCR_TOC,
    SCR_JUMP,
    SCR_BOOKMARKS,
    SCR_SEARCH,
    SCR_NEW_FOLDER,
    SCR_MOVE_FOLDER,
    SCR_NEXT_VOL,
    SCR_DELETE_CONFIRM,
    SCR_QUIT_CONFIRM,
    SCR_HELP
} ScreenMode;

typedef enum {
    TAB_SHELF = 0,
    TAB_SETTINGS
} HomeTab;

#define MAX_BOOKS       32
#define MAX_FOLDERS     16
#define MAX_BOOKMARKS   16
#define MAX_SEARCH_HITS 64
#define ILLUST_CACHE_N   2

typedef struct {
    char path[384], title[96], author[64], folder[48];
    BookFormat format;
    size_t byte_offset, total_bytes;
    int page_idx, page_count;
} BookEntry;

typedef struct {
    size_t pos_val;
    int page_num, pct;
    char label[72];
} ManualBookmark;

typedef struct {
    size_t pos_val;
    int page_num;
    char snippet[84];
} SearchHit;

typedef struct {
    int img_id, w, h;
    uint8_t *rgb;
    unsigned stamp;
} IllustCacheSlot;

static ScreenMode g_screen = SCR_HOME;
static ScreenMode g_return_screen = SCR_HOME;
static HomeTab    g_home_tab = TAB_SHELF;

/* 使用者偏好設定 */
static int g_theme_idx      = 0;
static int g_font_style     = 0;
static int g_font_size_idx  = 0; /* 小螢幕預設緊湊16 */
static int g_spacing_idx    = 1; /* 小螢幕預設緊湊行距 */
static int g_margin_idx     = 1; /* 標準邊距 */
static int g_auto_sec_idx   = 2; /* 15秒 */
static int g_pdf_def_zoom   = 0; /* 預設適應整頁 */
static int g_pdf_pan_idx    = 1; /* 預設步距 70px */
static int g_fullscreen     = 0; /* 全螢幕沉浸模式 */

static const int FONT_PX[4]         = { 16, 18, 20, 24 };
static const int FONT_UNIT_W[4]     = {  8,  8,  9, 10 };
static const char *FONT_SIZE_NM[4]  = { "緊湊16", "標準18", "舒適20", "大字24" };
static const char *FONT_STYLE_NM[4] = { "標準正體", "粗體清晰", "柔雅陰影", "高對比" };
static const int SPACING_PX_SM[5]   = {  1,  3,  5,  8, 12 };
static const int SPACING_PX_LG[5]   = {  3,  6, 10, 15, 22 };
static const char *SPACING_NM[5]    = { "極緊", "緊湊", "標準", "寬鬆", "超寬" };
static const int MARGIN_PX_SM[4]    = {  4,  8, 14, 22 };
static const int MARGIN_PX_LG[4]    = { 16, 28, 48, 72 };
static const char *MARGIN_NM[4]     = { "極窄", "標準", "舒適", "專注" };
static const int AUTO_SECS[7]       = {  5, 10, 15, 20, 30, 45, 60 };
static const int PDF_PAN_STEP[3]    = { 40, 70, 110 };
static const char *PDF_PAN_NM[3]    = { "精細40", "標準70", "快速110" };
static const char *PDF_ZOOM_NM[5]   = { "適應整頁", "適應寬度", "135%", "175%", "225%" };
static const char *PDF_ZOOM_SHORT[5]= { "整頁", "適寬", "135%", "175%", "225%" };
static const char *FMT_FILTER_NM[5] = { "全部", "EPUB", "PDF", "TXT", "圖片" };

/* 書架與資料夾 */
static BookEntry g_books[MAX_BOOKS];
static int  g_book_count = 0;
static int  g_folder_count = 0;
static char g_folders[MAX_FOLDERS][48];
static char g_cur_folder[48] = {0};
static char g_pending_import_folder[48] = {0};
static int  g_filter_fmt = 0;
static int  g_shelf_sel = 0;
static int  g_shelf_scroll = 0;
static int  g_set_row = 0;

/* 狀態與計時器 */
static int g_last_clock_min = -1;
static int g_auto_active = 0;
static int g_auto_elapsed = 0;
static char g_toast_msg[96] = {0};
static int g_toast_ttl = 0;

/* 圖片快取 */
static IllustCacheSlot g_ill_cache[ILLUST_CACHE_N];
static unsigned g_ill_clock = 0;

/* 當前開啟書籍與文字分頁 */
static char g_filepath[384] = {0};
static BookDoc g_doc;
static PdfDoc *g_pdf = NULL;
static size_t *g_page_offsets = NULL;
static int g_page_count = 0;
static int g_current_page = 0;

/* 原版 PDF 狀態（完全保留原邏輯） */
static int g_pdf_page = 0;
static int g_pdf_zoom = 0;
static int g_pdf_pan_x = 0;
static int g_pdf_pan_y = 0;
static int g_pdf_last_pw = 320;
static int g_pdf_last_ph = 200;
static int g_pdf_reflow = 0;
static int g_pdf_reflow_scroll = 0;
static char *g_pdf_reflow_text = NULL;
static size_t g_pdf_reflow_len = 0;
static int g_pdf_reflow_page_cached = -1;

/* 彈窗與互動狀態 */
static ManualBookmark g_bmarks[MAX_BOOKMARKS];
static int g_bmark_count = 0;
static int g_bmark_sel = 0;
static int g_bmark_scroll = 0;

static char g_search_query[64] = {0};
static SearchHit g_search_hits[MAX_SEARCH_HITS];
static int g_search_cnt = 0;
static int g_search_sel = 0;
static int g_search_scroll = 0;
static int g_search_ran = 0;

static int g_read_menu_sel = 0;
static int g_read_menu_scroll = 0;
static int g_toc_sel = 0;
static int g_toc_scroll = 0;
static int g_jump_target = 0;
static char g_jump_digits[10] = {0};
static char g_new_folder_buf[48] = {0};
static int g_move_book_idx = -1;
static int g_move_folder_sel = 0;
static int g_move_folder_scroll = 0;
static int g_next_vol_idx = -1;
static int g_del_is_folder = 0;
static int g_del_target_idx = -1;
static int g_help_page = 0;

/* =========================================================================
 * 基礎工具函式
 * ========================================================================= */

static int is_small_screen(void) {
    return (hal_get_width() <= 400 || hal_get_height() <= 280);
}

static int get_top_bar_h(void) {
    if (g_fullscreen) return 0;
    return is_small_screen() ? 24 : 32;
}

static int get_bot_bar_h(void) {
    if (g_fullscreen) return 0;
    return is_small_screen() ? 22 : 30;
}

static int get_read_view_y(void) {
    int tb = get_top_bar_h();
    return tb > 0 ? (tb + 1) : 0;
}

static int get_read_view_h(void) {
    int h = hal_get_height();
    int tb = get_top_bar_h();
    int bb = get_bot_bar_h();
    int vh = h - tb - bb - (tb > 0 ? 1 : 0);
    return vh > 40 ? vh : 40;
}

static int is_image_fmt(BookFormat fmt) {
    return (fmt == BOOK_FMT_BMP || fmt == BOOK_FMT_PNG);
}

static ScreenMode active_read_screen(void) {
    if (g_doc.format == BOOK_FMT_PDF && g_pdf) return SCR_READ_PDF;
    if (is_image_fmt(g_doc.format)) return SCR_READ_IMG;
    return SCR_READ_TEXT;
}

static void show_toast(const char *msg) {
    snprintf(g_toast_msg, sizeof(g_toast_msg), "%s", msg ? msg : "");
    g_toast_ttl = 4;
}

static int utf8_char_info(const unsigned char *s, size_t rem, int *out_u) {
    unsigned char c = s[0];
    if ((c & 0x80) == 0) { if (out_u) *out_u = 1; return 1; }
    if ((c & 0xE0) == 0xC0 && rem >= 2) { if (out_u) *out_u = 2; return 2; }
    if ((c & 0xF0) == 0xE0 && rem >= 3) { if (out_u) *out_u = 2; return 3; }
    if ((c & 0xF8) == 0xF0 && rem >= 4) { if (out_u) *out_u = 2; return 4; }
    if (out_u) *out_u = 1;
    return 1;
}

static void utf8_ellipsis(const char *src, int max_u, char *dst, size_t dst_sz) {
    if (!src || !dst || dst_sz == 0) return;
    size_t slen = strlen(src), pos = 0, out = 0;
    int units = 0;
    while (pos < slen && out + 4 < dst_sz) {
        int u = 0, clen = utf8_char_info((const unsigned char *)(src + pos), slen - pos, &u);
        if (units + u > max_u) {
            if (out + 3 < dst_sz) { dst[out++] = '.'; dst[out++] = '.'; }
            break;
        }
        if (out + (size_t)clen >= dst_sz) break;
        memcpy(dst + out, src + pos, (size_t)clen);
        out += (size_t)clen;
        pos += (size_t)clen;
        units += u;
    }
    dst[out] = '\0';
}

static const char *get_filename_only(const char *path) {
    if (!path) return "";
    const char *p1 = strrchr(path, '\\');
    const char *p2 = strrchr(path, '/');
    const char *p = (p1 && p2) ? (p1 > p2 ? p1 : p2) : (p1 ? p1 : p2);
    return p ? (p + 1) : path;
}

static void draw_str(int x, int y, const char *s, int max_w, uint32_t rgb) {
    if (!s || !s[0] || max_w <= 8) return;
    hal_draw_line_utf8(x, y, s, (int)strlen(s), max_w, rgb);
}

static void sanitize_field(char *dst, size_t sz, const char *src) {
    if (!dst || sz == 0) return;
    size_t out = 0;
    if (!src) { dst[0] = '\0'; return; }
    for (size_t i = 0; src[i] != '\0' && out + 1 < sz; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '|' || c == '\r' || c == '\n' || c == '\t') dst[out++] = ' ';
        else if (c >= 0x20) dst[out++] = (char)c;
    }
    dst[out] = '\0';
}

static void clear_illust_cache(void) {
    for (int i = 0; i < ILLUST_CACHE_N; i++) {
        free(g_ill_cache[i].rgb);
        memset(&g_ill_cache[i], 0, sizeof(g_ill_cache[i]));
        g_ill_cache[i].img_id = -1;
    }
}

static const uint8_t *get_cached_illust(int img_id, int *out_w, int *out_h) {
    if (img_id < 0 || img_id >= g_doc.image_count || g_doc.format != BOOK_FMT_EPUB) return NULL;
    for (int i = 0; i < ILLUST_CACHE_N; i++) {
        if (g_ill_cache[i].rgb && g_ill_cache[i].img_id == img_id) {
            g_ill_cache[i].stamp = ++g_ill_clock;
            if (out_w) *out_w = g_ill_cache[i].w;
            if (out_h) *out_h = g_ill_cache[i].h;
            return g_ill_cache[i].rgb;
        }
    }
    int slot = 0;
    unsigned oldest = 0xFFFFFFFFu;
    for (int i = 0; i < ILLUST_CACHE_N; i++) {
        if (!g_ill_cache[i].rgb) { slot = i; break; }
        if (g_ill_cache[i].stamp < oldest) { oldest = g_ill_cache[i].stamp; slot = i; }
    }
    free(g_ill_cache[slot].rgb);
    g_ill_cache[slot].rgb = NULL;
    g_ill_cache[slot].img_id = img_id;
    g_ill_cache[slot].stamp = ++g_ill_clock;
    g_ill_cache[slot].rgb = book_extract_epub_image(
        g_filepath, g_doc.images[img_id].zip_path, &g_ill_cache[slot].w, &g_ill_cache[slot].h);
    if (out_w) *out_w = g_ill_cache[slot].w;
    if (out_h) *out_h = g_ill_cache[slot].h;
    return g_ill_cache[slot].rgb;
}

/* =========================================================================
 * 書庫與書籤持久化
 * ========================================================================= */

static void save_library(void) {
    const char *lp = hal_get_library_path();
    if (!lp || !lp[0]) return;
    FILE *f = fopen(lp, "wb");
    if (!f) return;
    fprintf(f, "#CFG358|%d|%d|%d|%d|%d|%d|%d|%d|%d\n",
            g_theme_idx, g_font_style, g_font_size_idx, g_spacing_idx,
            g_margin_idx, g_auto_sec_idx, g_pdf_def_zoom, g_pdf_pan_idx, g_fullscreen);
    for (int i = 0; i < g_folder_count; i++) {
        char fn[48];
        sanitize_field(fn, sizeof(fn), g_folders[i]);
        if (fn[0]) fprintf(f, "#FLD|%s\n", fn);
    }
    for (int i = 0; i < g_book_count; i++) {
        BookEntry *b = &g_books[i];
        char t[96], a[64], fld[48];
        sanitize_field(t, sizeof(t), b->title);
        sanitize_field(a, sizeof(a), b->author);
        sanitize_field(fld, sizeof(fld), b->folder);
        fprintf(f, "%d|%zu|%zu|%d|%d|%s|%s|%s|%s\n",
                (int)b->format, b->byte_offset, b->total_bytes,
                b->page_idx, b->page_count, t, a, fld, b->path);
    }
    fclose(f);
}

static void load_library(void) {
    g_book_count = 0;
    g_folder_count = 0;
    const char *lp = hal_get_library_path();
    if (!lp || !lp[0]) return;
    FILE *f = fopen(lp, "rb");
    if (!f) return;
    char line[800];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (len == 0) continue;
        if (strncmp(line, "#CFG358|", 8) == 0) {
            int c[9] = {0};
            if (sscanf(line + 8, "%d|%d|%d|%d|%d|%d|%d|%d|%d",
                       &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7], &c[8]) == 9) {
                if (c[0] >= 0 && c[0] < NUM_THEMES) g_theme_idx = c[0];
                if (c[1] >= 0 && c[1] < 4) g_font_style = c[1];
                if (c[2] >= 0 && c[2] < 4) g_font_size_idx = c[2];
                if (c[3] >= 0 && c[3] < 5) g_spacing_idx = c[3];
                if (c[4] >= 0 && c[4] < 4) g_margin_idx = c[4];
                if (c[5] >= 0 && c[5] < 7) g_auto_sec_idx = c[5];
                if (c[6] >= 0 && c[6] < 5) g_pdf_def_zoom = c[6];
                if (c[7] >= 0 && c[7] < 3) g_pdf_pan_idx = c[7];
                g_fullscreen = c[8] ? 1 : 0;
            }
            continue;
        }
        if (strncmp(line, "#FLD|", 5) == 0) {
            if (g_folder_count < MAX_FOLDERS && line[5]) {
                utf8_ellipsis(line + 5, 20, g_folders[g_folder_count++], 48);
            }
            continue;
        }
        if (line[0] == '#' || g_book_count >= MAX_BOOKS) continue;

        char *ps[8], *cur = line;
        int pcnt = 0;
        while (pcnt < 8) {
            char *bar = strchr(cur, '|');
            if (!bar) break;
            ps[pcnt++] = bar;
            cur = bar + 1;
        }
        if (pcnt < 8) continue;

        BookEntry *be = &g_books[g_book_count++];
        memset(be, 0, sizeof(*be));
        be->format      = (BookFormat)atoi(line);
        be->byte_offset = (size_t)strtoull(ps[0] + 1, NULL, 10);
        be->total_bytes = (size_t)strtoull(ps[1] + 1, NULL, 10);
        be->page_idx    = atoi(ps[2] + 1);
        be->page_count  = atoi(ps[3] + 1);

        size_t tl = (size_t)(ps[5] - (ps[4] + 1));
        if (tl >= sizeof(be->title)) tl = sizeof(be->title) - 1;
        memcpy(be->title, ps[4] + 1, tl);
        be->title[tl] = '\0';

        size_t al = (size_t)(ps[6] - (ps[5] + 1));
        if (al >= sizeof(be->author)) al = sizeof(be->author) - 1;
        memcpy(be->author, ps[5] + 1, al);
        be->author[al] = '\0';

        size_t fl = (size_t)(ps[7] - (ps[6] + 1));
        if (fl >= sizeof(be->folder)) fl = sizeof(be->folder) - 1;
        memcpy(be->folder, ps[6] + 1, fl);
        be->folder[fl] = '\0';

        snprintf(be->path, sizeof(be->path), "%s", ps[7] + 1);
        if (!be->title[0]) utf8_ellipsis(get_filename_only(be->path), 36, be->title, sizeof(be->title));
    }
    fclose(f);
}

static void update_book_in_library(const char *path, BookFormat fmt, const char *title,
                                   const char *author, size_t boff, size_t btot,
                                   int pidx, int pcnt) {
    if (!path || !path[0]) return;
    int found = -1;
    char prev_fld[48] = {0};
    for (int i = 0; i < g_book_count; i++) {
        if (strcmp(g_books[i].path, path) == 0) {
            found = i;
            snprintf(prev_fld, sizeof(prev_fld), "%s", g_books[i].folder);
            break;
        }
    }
    BookEntry cur;
    memset(&cur, 0, sizeof(cur));
    snprintf(cur.path, sizeof(cur.path), "%s", path);
    utf8_ellipsis((title && title[0]) ? title : get_filename_only(path), 40, cur.title, sizeof(cur.title));
    sanitize_field(cur.author, sizeof(cur.author), author ? author : "");
    cur.format      = fmt;
    cur.byte_offset = boff;
    cur.total_bytes = btot;
    cur.page_idx    = pidx;
    cur.page_count  = pcnt;
    if (prev_fld[0]) snprintf(cur.folder, sizeof(cur.folder), "%s", prev_fld);
    else if (g_pending_import_folder[0]) snprintf(cur.folder, sizeof(cur.folder), "%s", g_pending_import_folder);

    if (found >= 0) {
        for (int i = found; i > 0; i--) g_books[i] = g_books[i - 1];
        g_books[0] = cur;
    } else {
        int mv = (g_book_count < MAX_BOOKS) ? g_book_count : (MAX_BOOKS - 1);
        for (int i = mv; i > 0; i--) g_books[i] = g_books[i - 1];
        g_books[0] = cur;
        if (g_book_count < MAX_BOOKS) g_book_count++;
    }
    g_shelf_sel = 0;
    g_shelf_scroll = 0;
    save_library();
}

static void remove_book_at(int idx) {
    if (g_book_count <= 0 || idx < 0 || idx >= g_book_count) return;
    for (int i = idx; i + 1 < g_book_count; i++) g_books[i] = g_books[i + 1];
    g_book_count--;
    if (g_shelf_sel > 0 && g_shelf_sel >= g_book_count) g_shelf_sel--;
    save_library();
}

static int folder_find(const char *name) {
    for (int i = 0; i < g_folder_count; i++) {
        if (strcmp(g_folders[i], name) == 0) return i;
    }
    return -1;
}

static int folder_create(const char *name) {
    if (!name || !name[0] || g_folder_count >= MAX_FOLDERS) return 0;
    char cl[48];
    sanitize_field(cl, sizeof(cl), name);
    if (!cl[0] || folder_find(cl) >= 0) return 0;
    snprintf(g_folders[g_folder_count++], 48, "%s", cl);
    save_library();
    return 1;
}

static void folder_delete(int fi) {
    if (fi < 0 || fi >= g_folder_count) return;
    char tgt[48];
    snprintf(tgt, sizeof(tgt), "%s", g_folders[fi]);
    for (int i = 0; i < g_book_count; i++) {
        if (strcmp(g_books[i].folder, tgt) == 0) g_books[i].folder[0] = '\0';
    }
    for (int i = fi; i + 1 < g_folder_count; i++) {
        snprintf(g_folders[i], 48, "%s", g_folders[i + 1]);
    }
    g_folder_count--;
    if (strcmp(g_cur_folder, tgt) == 0) g_cur_folder[0] = '\0';
    save_library();
}

static int folder_count_books(const char *name) {
    int c = 0;
    for (int i = 0; i < g_book_count; i++) {
        if (strcmp(g_books[i].folder, name) == 0) c++;
    }
    return c;
}

static int book_matches_filters(const BookEntry *b) {
    if (g_filter_fmt == 1 && b->format != BOOK_FMT_EPUB) return 0;
    if (g_filter_fmt == 2 && b->format != BOOK_FMT_PDF)  return 0;
    if (g_filter_fmt == 3 && b->format != BOOK_FMT_TXT)  return 0;
    if (g_filter_fmt == 4 && !is_image_fmt(b->format))   return 0;
    return 1;
}

static int shelf_build_list(int *types, int *idxs, int max_n) {
    int cnt = 0;
    int filtering = (g_filter_fmt != 0);
    if (g_cur_folder[0] == '\0') {
        if (!filtering) {
            for (int f = 0; f < g_folder_count && cnt < max_n; f++) {
                types[cnt] = 1;
                idxs[cnt] = f;
                cnt++;
            }
        }
        for (int b = 0; b < g_book_count && cnt < max_n; b++) {
            if (!book_matches_filters(&g_books[b])) continue;
            if (!filtering && g_books[b].folder[0] != '\0') continue;
            types[cnt] = 0;
            idxs[cnt] = b;
            cnt++;
        }
    } else {
        for (int b = 0; b < g_book_count && cnt < max_n; b++) {
            if (strcmp(g_books[b].folder, g_cur_folder) == 0 && book_matches_filters(&g_books[b])) {
                types[cnt] = 0;
                idxs[cnt] = b;
                cnt++;
            }
        }
        for (int i = 0; i < cnt; i++) {
            for (int j = i + 1; j < cnt; j++) {
                if (strcmp(g_books[idxs[i]].title, g_books[idxs[j]].title) > 0) {
                    int t = idxs[i]; idxs[i] = idxs[j]; idxs[j] = t;
                }
            }
        }
    }
    return cnt;
}

static int find_next_volume_in_folder(const char *cur_path) {
    int ci = -1;
    for (int i = 0; i < g_book_count; i++) {
        if (strcmp(g_books[i].path, cur_path) == 0) { ci = i; break; }
    }
    if (ci < 0 || !g_books[ci].folder[0]) return -1;
    int list[MAX_BOOKS], n = 0;
    for (int i = 0; i < g_book_count; i++) {
        if (strcmp(g_books[i].folder, g_books[ci].folder) == 0) list[n++] = i;
    }
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (strcmp(g_books[list[i]].title, g_books[list[j]].title) > 0) {
                int t = list[i]; list[i] = list[j]; list[j] = t;
            }
        }
    }
    for (int i = 0; i + 1 < n; i++) {
        if (list[i] == ci) return list[i + 1];
    }
    return -1;
}

static void save_manual_bookmarks(void) {
    if (!g_filepath[0]) return;
    char p[420];
    snprintf(p, sizeof(p), "%s.bmk2", g_filepath);
    FILE *f = fopen(p, "wb");
    if (!f) return;
    for (int i = 0; i < g_bmark_count; i++) {
        char lbl[72];
        sanitize_field(lbl, sizeof(lbl), g_bmarks[i].label);
        fprintf(f, "%zu|%d|%d|%s\n", g_bmarks[i].pos_val, g_bmarks[i].page_num, g_bmarks[i].pct, lbl);
    }
    fclose(f);
}

static void load_manual_bookmarks(const char *path) {
    g_bmark_count = 0;
    g_bmark_sel = 0;
    g_bmark_scroll = 0;
    if (!path || !path[0]) return;
    char p[420];
    snprintf(p, sizeof(p), "%s.bmk2", path);
    FILE *f = fopen(p, "rb");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f) && g_bmark_count < MAX_BOOKMARKS) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        char *p1 = strchr(line, '|'); if (!p1) continue;
        char *p2 = strchr(p1 + 1, '|'); if (!p2) continue;
        char *p3 = strchr(p2 + 1, '|'); if (!p3) continue;
        ManualBookmark *bm = &g_bmarks[g_bmark_count++];
        bm->pos_val  = (size_t)strtoull(line, NULL, 10);
        bm->page_num = atoi(p1 + 1);
        bm->pct      = atoi(p2 + 1);
        snprintf(bm->label, sizeof(bm->label), "%s", p3 + 1);
    }
    fclose(f);
}

static void add_current_manual_bookmark(void) {
    if (g_bmark_count >= MAX_BOOKMARKS) { show_toast("書籤已達上限"); return; }
    ManualBookmark *bm = &g_bmarks[g_bmark_count];
    memset(bm, 0, sizeof(*bm));
    if (g_doc.format == BOOK_FMT_PDF && g_pdf) {
        int tot = pdf_page_count(g_pdf);
        bm->pos_val  = (size_t)g_pdf_page;
        bm->page_num = g_pdf_page + 1;
        bm->pct      = tot > 0 ? ((g_pdf_page + 1) * 100) / tot : 0;
        size_t tl = 0;
        char *pt = pdf_extract_page_text(g_pdf, g_pdf_page, &tl);
        if (pt && tl > 2) {
            char cl[72];
            sanitize_field(cl, sizeof(cl), pt);
            utf8_ellipsis(cl, 24, bm->label, sizeof(bm->label));
        } else {
            snprintf(bm->label, sizeof(bm->label), "PDF 第 %d 頁", g_pdf_page + 1);
        }
        free(pt);
    } else if (g_doc.text && g_page_offsets && g_page_count > 0) {
        size_t off = g_page_offsets[g_current_page];
        bm->pos_val  = off;
        bm->page_num = g_current_page + 1;
        bm->pct      = ((g_current_page + 1) * 100) / g_page_count;
        if (g_doc.text[off] == '\x01') {
            snprintf(bm->label, sizeof(bm->label), "[插圖頁 #%d]", g_current_page + 1);
        } else {
            char raw[80], cl[80];
            size_t rem = g_doc.text_len - off;
            if (rem > 70) rem = 70;
            memcpy(raw, g_doc.text + off, rem);
            raw[rem] = '\0';
            sanitize_field(cl, sizeof(cl), raw);
            utf8_ellipsis(cl, 24, bm->label, sizeof(bm->label));
        }
    } else {
        return;
    }
    g_bmark_count++;
    g_bmark_sel = g_bmark_count - 1;
    save_manual_bookmarks();
    show_toast("已新增書籤");
}

static void save_bookmark(void) {
    if (!g_filepath[0]) return;
    if (g_doc.format == BOOK_FMT_PDF && g_pdf) {
        int tot = pdf_page_count(g_pdf);
        char p[420];
        snprintf(p, sizeof(p), "%s.bmk", g_filepath);
        FILE *f = fopen(p, "wb");
        if (f) { fprintf(f, "%d", g_pdf_page); fclose(f); }
        update_book_in_library(g_filepath, BOOK_FMT_PDF, g_doc.title, g_doc.author,
                               (size_t)g_pdf_page, (size_t)tot, g_pdf_page, tot);
    } else if (is_image_fmt(g_doc.format)) {
        update_book_in_library(g_filepath, g_doc.format, g_doc.title, "", 0, 0, 0, 0);
    } else if (g_page_count > 0 && g_page_offsets) {
        size_t off = g_page_offsets[g_current_page];
        char p[420];
        snprintf(p, sizeof(p), "%s.bmk", g_filepath);
        FILE *f = fopen(p, "wb");
        if (f) { fprintf(f, "%zu", off); fclose(f); }
        update_book_in_library(g_filepath, g_doc.format, g_doc.title, g_doc.author,
                               off, g_doc.text_len, g_current_page, g_page_count);
    }
}

static size_t load_bookmark(const char *path) {
    if (!path || !path[0]) return 0;
    char p[420];
    snprintf(p, sizeof(p), "%s.bmk", path);
    FILE *f = fopen(p, "rb");
    if (f) {
        size_t off = 0;
        if (fscanf(f, "%zu", &off) == 1) { fclose(f); return off; }
        fclose(f);
    }
    for (int i = 0; i < g_book_count; i++) {
        if (strcmp(g_books[i].path, path) == 0) return g_books[i].byte_offset;
    }
    return 0;
}

/* =========================================================================
 * 文字排版、分頁與全文搜尋
 * ========================================================================= */

static int parse_img_marker(const char *buf, size_t len, size_t pos, int *out_id, size_t *out_step) {
    if (pos + 6 > len || buf[pos] != '\x01' || memcmp(buf + pos + 1, "IMG:", 4) != 0) return 0;
    int id = atoi(buf + pos + 5);
    size_t p = pos + 5;
    while (p < len && buf[p] != '\n' && buf[p] != '\r') p++;
    if (p < len && buf[p] == '\r') p++;
    if (p < len && buf[p] == '\n') p++;
    if (out_id) *out_id = id;
    if (out_step) *out_step = p - pos;
    return 1;
}

static size_t scan_one_line_buf(const char *buf, size_t len, size_t start_off, int max_units, size_t *out_draw_len) {
    size_t pos = start_off;
    int units = 0;
    while (pos < len) {
        char ch = buf[pos];
        if (ch == '\r') {
            *out_draw_len = pos - start_off;
            pos++;
            if (pos < len && buf[pos] == '\n') pos++;
            return pos - start_off;
        }
        if (ch == '\n') {
            *out_draw_len = pos - start_off;
            return (pos - start_off) + 1;
        }
        int u = 0, clen = utf8_char_info((const unsigned char *)(buf + pos), len - pos, &u);
        if (units + u > max_units && pos > start_off) {
            *out_draw_len = pos - start_off;
            return pos - start_off;
        }
        units += u;
        pos += (size_t)clen;
    }
    *out_draw_len = pos - start_off;
    return pos - start_off;
}

static void get_layout_params(int *out_max_units, int *out_lines, int *out_line_h, int *out_pad_x) {
    int w = hal_get_width();
    int sm = is_small_screen();
    int pad_x = sm ? MARGIN_PX_SM[g_margin_idx] : MARGIN_PX_LG[g_margin_idx];
    int sp = sm ? SPACING_PX_SM[g_spacing_idx] : SPACING_PX_LG[g_spacing_idx];
    int line_h = FONT_PX[g_font_size_idx] + sp;
    int uw = w - pad_x * 2;
    if (uw < 120) uw = 120;
    int max_u = uw / FONT_UNIT_W[g_font_size_idx];
    if (max_u < 12) max_u = 12;
    int vh = get_read_view_h() - (sm ? 8 : 16);
    int lines = vh / line_h;
    if (lines < 1) lines = 1;
    *out_max_units = max_u;
    *out_lines = lines;
    *out_line_h = line_h;
    if (out_pad_x) *out_pad_x = pad_x;
}

static void jump_to_offset(size_t target_offset) {
    g_current_page = 0;
    for (int i = 0; i < g_page_count; i++) {
        if (g_page_offsets[i] <= target_offset) g_current_page = i;
        else break;
    }
}

static int find_page_for_offset(size_t off) {
    int pg = 0;
    for (int i = 0; i < g_page_count; i++) {
        if (g_page_offsets[i] <= off) pg = i;
        else break;
    }
    return pg;
}

static void repaginate(size_t target_offset) {
    if (!g_doc.text) return;
    int max_u, lines, lh, px;
    get_layout_params(&max_u, &lines, &lh, &px);
    free(g_page_offsets);
    g_page_offsets = NULL;
    g_page_count = 0;
    int cap = 128;
    g_page_offsets = (size_t *)malloc((size_t)cap * sizeof(size_t));
    if (!g_page_offsets) return;
    size_t pos = 0;
    while (pos < g_doc.text_len || g_page_count == 0) {
        if (g_page_count >= cap) {
            cap *= 2;
            size_t *tmp = (size_t *)realloc(g_page_offsets, (size_t)cap * sizeof(size_t));
            if (!tmp) break;
            g_page_offsets = tmp;
        }
        g_page_offsets[g_page_count++] = pos;
        size_t img_step = 0;
        if (parse_img_marker(g_doc.text, g_doc.text_len, pos, NULL, &img_step)) {
            pos += img_step;
            if (pos >= g_doc.text_len) break;
            continue;
        }
        for (int ln = 0; ln < lines && pos < g_doc.text_len; ln++) {
            if (parse_img_marker(g_doc.text, g_doc.text_len, pos, NULL, NULL)) break;
            size_t dl = 0, step = scan_one_line_buf(g_doc.text, g_doc.text_len, pos, max_u, &dl);
            if (step == 0) break;
            pos += step;
        }
        if (pos >= g_doc.text_len) break;
    }
    jump_to_offset(target_offset);
}

static const char *find_ci_str(const char *hay, size_t hlen, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0 || nlen > hlen) return NULL;
    for (size_t i = 0; i + nlen <= hlen; i++) {
        size_t j = 0;
        while (j < nlen && tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j])) j++;
        if (j == nlen) return hay + i;
    }
    return NULL;
}

static void run_fulltext_search(void) {
    g_search_cnt = 0;
    g_search_sel = 0;
    g_search_scroll = 0;
    g_search_ran = 1;
    if (!g_search_query[0]) return;

    if (g_doc.format == BOOK_FMT_PDF && g_pdf) {
        int tot = pdf_page_count(g_pdf);
        for (int p = 0; p < tot && g_search_cnt < MAX_SEARCH_HITS; p++) {
            size_t tl = 0;
            char *pt = pdf_extract_page_text(g_pdf, p, &tl);
            if (pt && tl > 0) {
                const char *hit = find_ci_str(pt, tl, g_search_query);
                if (hit) {
                    SearchHit *sh = &g_search_hits[g_search_cnt++];
                    sh->pos_val = (size_t)p;
                    sh->page_num = p + 1;
                    size_t off = (size_t)(hit - pt);
                    size_t cs = (off > 10) ? (off - 10) : 0;
                    while (cs < off && ((unsigned char)pt[cs] & 0xC0) == 0x80) cs++;
                    char raw[96], cl[96];
                    size_t rl = tl - cs;
                    if (rl > 74) rl = 74;
                    memcpy(raw, pt + cs, rl);
                    raw[rl] = '\0';
                    sanitize_field(cl, sizeof(cl), raw);
                    utf8_ellipsis(cl, 28, sh->snippet, sizeof(sh->snippet));
                }
            }
            free(pt);
        }
        return;
    }

    if (g_doc.text && g_doc.text_len > 0) {
        size_t pos = 0, qlen = strlen(g_search_query);
        while (pos < g_doc.text_len && g_search_cnt < MAX_SEARCH_HITS) {
            const char *hit = find_ci_str(g_doc.text + pos, g_doc.text_len - pos, g_search_query);
            if (!hit) break;
            size_t off = (size_t)(hit - g_doc.text);
            SearchHit *sh = &g_search_hits[g_search_cnt++];
            sh->pos_val = off;
            sh->page_num = find_page_for_offset(off) + 1;
            size_t cs = (off > 10) ? (off - 10) : 0;
            while (cs < off && ((unsigned char)g_doc.text[cs] & 0xC0) == 0x80) cs++;
            char raw[96], cl[96];
            size_t rl = g_doc.text_len - cs;
            if (rl > 74) rl = 74;
            memcpy(raw, g_doc.text + cs, rl);
            raw[rl] = '\0';
            sanitize_field(cl, sizeof(cl), raw);
            utf8_ellipsis(cl, 28, sh->snippet, sizeof(sh->snippet));
            pos = off + qlen;
        }
    }
}

/* =========================================================================
 * 載入提示與 PDF 平移限制
 * ========================================================================= */

static void show_loading_card(const char *filepath) {
    const AppTheme *th = theme_get(g_theme_idx);
    int w = hal_get_width(), h = hal_get_height();
    BookFormat fmt = book_detect_format(filepath);
    int cw = w - 28;
    if (cw > 340) cw = 340;
    if (cw < 200) cw = 200;
    int ch = 92;
    int cx = (w - cw) / 2, cy = (h - ch) / 2;
    hal_clear_text_in_rect(cx - 4, cy - 4, cw + 8, ch + 8);
    ui_card(cx, cy, cw, ch, 8, th->card_bg, th->card_border, th->shadow, 1, ui_format_color(fmt));
    ui_draw_badge(cx + 12, cy + 14, book_format_tag(fmt), ui_format_color(fmt), 0xFFFFFF);
    hal_set_text_bg(th->card_bg);
    char msg[96], fn[72];
    utf8_ellipsis(get_filename_only(filepath), (cw - 24) / 8, fn, sizeof(fn));
    snprintf(msg, sizeof(msg), "載入 %s 中...", book_format_tag(fmt));
    draw_str(cx + 62, cy + 12, msg, cw - 72, th->text);
    draw_str(cx + 12, cy + 40, fn, cw - 24, th->muted);
    ui_progress_bar(cx + 12, cy + 68, cw - 24, 6, 65, th->track, ui_format_color(fmt));
    hal_present();
}

static void clamp_pdf_pan(void) {
    int vw = hal_get_width();
    int vh = get_read_view_h();
    int mx = g_pdf_last_pw - vw; if (mx < 0) mx = 0;
    int my = g_pdf_last_ph - vh; if (my < 0) my = 0;
    if (g_pdf_pan_x < 0) g_pdf_pan_x = 0;
    if (g_pdf_pan_x > mx) g_pdf_pan_x = mx;
    if (g_pdf_pan_y < 0) g_pdf_pan_y = 0;
    if (g_pdf_pan_y > my) g_pdf_pan_y = my;
}

static int count_pdf_reflow_lines(void) {
    if (!g_pdf_reflow_text || g_pdf_reflow_len == 0) return 0;
    int max_u, lines, lh, px;
    get_layout_params(&max_u, &lines, &lh, &px);
    size_t pos = 0;
    int total = 0;
    while (pos < g_pdf_reflow_len && total < 2000) {
        size_t dl = 0, step = scan_one_line_buf(g_pdf_reflow_text, g_pdf_reflow_len, pos, max_u, &dl);
        total++;
        if (step == 0) break;
        pos += step;
    }
    return total;
}

/* =========================================================================
 * 生命週期與開書流程（完整保留原版 PDF 邏輯）
 * ========================================================================= */

void reader_init(void) {
    memset(&g_doc, 0, sizeof(g_doc));
    g_pdf = NULL;
    for (int i = 0; i < ILLUST_CACHE_N; i++) g_ill_cache[i].img_id = -1;
    load_library();
    hal_set_font_style(g_font_style, g_font_size_idx);
    int y, m, d, hh, mm, ss;
    hal_get_local_time(&y, &m, &d, &hh, &mm, &ss);
    g_last_clock_min = mm;
    g_screen = SCR_HOME;
    g_home_tab = TAB_SHELF;
}

void reader_shutdown(void) {
    if (g_screen != SCR_HOME) {
        save_bookmark();
        save_manual_bookmarks();
    }
    save_library();
    clear_illust_cache();
    pdf_close(g_pdf);
    g_pdf = NULL;
    free(g_pdf_reflow_text);
    g_pdf_reflow_text = NULL;
    book_doc_free(&g_doc);
    free(g_page_offsets);
    g_page_offsets = NULL;
}

static int advance_one_page_auto(void) {
    if (g_screen == SCR_READ_TEXT && g_current_page + 1 < g_page_count) {
        g_current_page++;
        save_bookmark();
        return 1;
    }
    if (g_screen == SCR_READ_PDF && g_pdf && g_pdf_page + 1 < pdf_page_count(g_pdf)) {
        g_pdf_page++;
        g_pdf_pan_x = 0;
        g_pdf_pan_y = 0;
        g_pdf_reflow_scroll = 0;
        save_bookmark();
        return 1;
    }
    if (g_screen == SCR_READ_IMG) {
        char np[384] = {0};
        if (hal_find_sibling_image(g_filepath, 1, np, sizeof(np))) {
            snprintf(g_filepath, sizeof(g_filepath), "%s", np);
            utf8_ellipsis(get_filename_only(np), 40, g_doc.title, sizeof(g_doc.title));
            return 1;
        }
    }
    g_auto_active = 0;
    show_toast("已達末頁，自動翻頁結束");
    return 1;
}

int reader_tick_second(void) {
    int y, m, d, hh, mm, ss;
    hal_get_local_time(&y, &m, &d, &hh, &mm, &ss);
    int need_redraw = 0;
    if (g_toast_ttl > 0) {
        g_toast_ttl--;
        if (g_toast_ttl == 0) {
            g_toast_msg[0] = '\0';
            need_redraw = 1;
        }
    }
    int in_read = (g_screen == SCR_READ_TEXT || g_screen == SCR_READ_PDF || g_screen == SCR_READ_IMG);
    if (in_read && g_auto_active) {
        g_auto_elapsed++;
        need_redraw = 1;
        if (g_auto_elapsed >= AUTO_SECS[g_auto_sec_idx]) {
            g_auto_elapsed = 0;
            advance_one_page_auto();
        }
    }
    if (mm != g_last_clock_min) {
        g_last_clock_min = mm;
        need_redraw = 1;
    }
    return need_redraw;
}

int reader_open_file(const char *filepath) {
    if (!filepath || !filepath[0]) return 0;
    if (g_screen != SCR_HOME) {
        save_bookmark();
        save_manual_bookmarks();
    }
    g_auto_active = 0;
    show_loading_card(filepath);
    clear_illust_cache();

    BookFormat fmt = book_detect_format(filepath);
    /* === 原版 PDF 開啟邏輯完整保留 === */
    if (fmt == BOOK_FMT_PDF) {
        PdfDoc *nd = pdf_open(filepath);
        if (!nd) { show_toast("無法開啟 PDF 檔案"); return 0; }
        pdf_close(g_pdf);
        g_pdf = nd;
        free(g_pdf_reflow_text);
        g_pdf_reflow_text = NULL;
        g_pdf_reflow_page_cached = -1;
        g_pdf_reflow_scroll = 0;
        book_doc_free(&g_doc);
        g_doc.format = BOOK_FMT_PDF;
        const char *pt = pdf_get_title(g_pdf);
        utf8_ellipsis((pt && pt[0]) ? pt : get_filename_only(filepath), 40, g_doc.title, sizeof(g_doc.title));
        sanitize_field(g_doc.author, sizeof(g_doc.author), pdf_get_author(g_pdf));
        int pcnt = pdf_page_count(g_pdf);
        int cap = pcnt > BOOK_MAX_CHAPTERS ? BOOK_MAX_CHAPTERS : pcnt;
        for (int i = 0; i < cap; i++) {
            g_doc.chapters[i].offset = (size_t)i;
            snprintf(g_doc.chapters[i].title, sizeof(g_doc.chapters[i].title), "PDF 第 %d 頁", i + 1);
        }
        g_doc.chapter_count = cap;
        snprintf(g_filepath, sizeof(g_filepath), "%s", filepath);
        g_pdf_zoom = g_pdf_def_zoom;
        g_pdf_pan_x = 0;
        g_pdf_pan_y = 0;
        g_pdf_reflow = 0;
        size_t spg = load_bookmark(g_filepath);
        g_pdf_page = ((int)spg >= 0 && (int)spg < pcnt) ? (int)spg : 0;
        load_manual_bookmarks(g_filepath);
        g_screen = SCR_READ_PDF;
        save_bookmark();
        return 1;
    }

    BookDoc next_doc;
    if (!book_load_document(filepath, &next_doc)) {
        show_toast("無法開啟此電子書");
        return 0;
    }
    pdf_close(g_pdf);
    g_pdf = NULL;
    free(g_pdf_reflow_text);
    g_pdf_reflow_text = NULL;
    g_pdf_reflow_page_cached = -1;
    book_doc_free(&g_doc);
    g_doc = next_doc;
    snprintf(g_filepath, sizeof(g_filepath), "%s", filepath);
    load_manual_bookmarks(g_filepath);

    if (is_image_fmt(g_doc.format)) {
        free(g_page_offsets);
        g_page_offsets = NULL;
        g_page_count = 0;
        g_screen = SCR_READ_IMG;
        save_bookmark();
        return 1;
    }

    g_screen = SCR_READ_TEXT;
    repaginate(load_bookmark(g_filepath));
    save_bookmark();
    return 1;
}

void reader_on_resize(void) {
    hal_set_font_style(g_font_style, g_font_size_idx);
    if (g_doc.text && g_page_offsets && g_page_count > 0) {
        repaginate(g_page_offsets[g_current_page]);
    }
    if (g_doc.format == BOOK_FMT_PDF && g_pdf) {
        clamp_pdf_pan();
    }
}

static void open_new_book_dialog(void) {
    char path[384] = {0};
    snprintf(g_pending_import_folder, sizeof(g_pending_import_folder), "%s", g_cur_folder);
    if (hal_open_file_dialog(path, sizeof(path))) {
        reader_open_file(path);
    }
    g_pending_import_folder[0] = '\0';
}

static void open_toc_screen(void) {
    g_auto_active = 0;
    g_toc_sel = 0;
    if (g_doc.format == BOOK_FMT_PDF && g_pdf) {
        g_toc_sel = (g_pdf_page < g_doc.chapter_count) ? g_pdf_page : 0;
    } else if (g_page_offsets && g_page_count > 0) {
        size_t co = g_page_offsets[g_current_page];
        for (int i = 0; i < g_doc.chapter_count; i++) {
            if (g_doc.chapters[i].offset <= co) g_toc_sel = i;
            else break;
        }
    }
    g_toc_scroll = g_toc_sel > 2 ? (g_toc_sel - 2) : 0;
    g_screen = SCR_TOC;
}

static void open_jump_screen(void) {
    g_auto_active = 0;
    g_jump_digits[0] = '\0';
    if (g_doc.format == BOOK_FMT_PDF && g_pdf) g_jump_target = g_pdf_page;
    else g_jump_target = g_current_page;
    g_screen = SCR_JUMP;
}

static void open_help_screen(ScreenMode ret) {
    g_return_screen = ret;
    g_help_page = 0;
    g_screen = SCR_HELP;
}

static int get_read_menu_count(void) {
    return (g_doc.format == BOOK_FMT_PDF && g_pdf) ? 11 : 10;
}

static void activate_read_menu_item(int idx, int dir) {
    int is_pdf = (g_doc.format == BOOK_FMT_PDF && g_pdf);
    if (is_pdf) {
        switch (idx) {
            case 0: g_screen = SCR_READ_PDF; break;
            case 1:
                if (dir < 0) g_pdf_zoom = (g_pdf_zoom + 4) % 5;
                else g_pdf_zoom = (g_pdf_zoom + 1) % 5;
                g_pdf_pan_x = 0; g_pdf_pan_y = 0;
                break;
            case 2:
                g_pdf_reflow = !g_pdf_reflow;
                g_pdf_reflow_scroll = 0;
                g_screen = SCR_READ_PDF;
                break;
            case 3:
                g_fullscreen = !g_fullscreen;
                save_library();
                clamp_pdf_pan();
                g_screen = SCR_READ_PDF;
                break;
            case 4: open_toc_screen(); break;
            case 5: open_jump_screen(); break;
            case 6: g_auto_active = 0; g_screen = SCR_BOOKMARKS; break;
            case 7: g_auto_active = 0; g_search_ran = 0; g_screen = SCR_SEARCH; break;
            case 8:
                if (dir < 0) g_theme_idx = (g_theme_idx + NUM_THEMES - 1) % NUM_THEMES;
                else g_theme_idx = (g_theme_idx + 1) % NUM_THEMES;
                save_library();
                break;
            case 9:
                g_auto_active = !g_auto_active;
                g_auto_elapsed = 0;
                g_screen = SCR_READ_PDF;
                show_toast(g_auto_active ? "已開啟自動翻頁" : "已關閉自動翻頁");
                break;
            case 10:
                g_auto_active = 0;
                save_bookmark();
                save_manual_bookmarks();
                g_screen = SCR_HOME;
                break;
        }
        return;
    }

    switch (idx) {
        case 0: g_screen = active_read_screen(); break;
        case 1:
            if (dir < 0) g_font_size_idx = (g_font_size_idx + 3) % 4;
            else g_font_size_idx = (g_font_size_idx + 1) % 4;
            save_library();
            reader_on_resize();
            break;
        case 2:
            if (dir < 0) g_spacing_idx = (g_spacing_idx + 4) % 5;
            else g_spacing_idx = (g_spacing_idx + 1) % 5;
            save_library();
            reader_on_resize();
            break;
        case 3:
            g_fullscreen = !g_fullscreen;
            save_library();
            reader_on_resize();
            g_screen = active_read_screen();
            break;
        case 4: open_toc_screen(); break;
        case 5: open_jump_screen(); break;
        case 6: g_auto_active = 0; g_screen = SCR_BOOKMARKS; break;
        case 7: g_auto_active = 0; g_search_ran = 0; g_screen = SCR_SEARCH; break;
        case 8:
            if (dir < 0) g_theme_idx = (g_theme_idx + NUM_THEMES - 1) % NUM_THEMES;
            else g_theme_idx = (g_theme_idx + 1) % NUM_THEMES;
            save_library();
            break;
        case 9:
            g_auto_active = 0;
            save_bookmark();
            save_manual_bookmarks();
            g_screen = SCR_HOME;
            break;
    }
}

/* =========================================================================
 * 純鍵盤輸入處理
 * ========================================================================= */

void reader_on_key(ReaderKey key, char raw_char) {
    int is_ok = (key == RKEY_OK || key == RKEY_SPACE);

    if (g_auto_active && key != RKEY_AUTO &&
        (g_screen == SCR_READ_TEXT || g_screen == SCR_READ_PDF || g_screen == SCR_READ_IMG)) {
        g_auto_active = 0;
    }

    if (g_screen == SCR_HELP) {
        if (key == RKEY_LEFT || key == RKEY_PGUP || key == RKEY_UP) {
            if (g_help_page > 0) g_help_page--;
        } else if (key == RKEY_RIGHT || key == RKEY_PGDN || key == RKEY_DOWN || key == RKEY_SPACE) {
            if (g_help_page < 2) g_help_page++;
            else g_screen = g_return_screen;
        } else if (key == RKEY_BACK || key == RKEY_HELP || key == RKEY_OK || key == RKEY_BACKSPACE) {
            g_screen = g_return_screen;
        }
        return;
    }

    if (g_screen == SCR_QUIT_CONFIRM) {
        if (is_ok) { hal_request_quit(); }
        else if (key == RKEY_BACK || key == RKEY_BACKSPACE) { g_screen = SCR_HOME; }
        return;
    }

    if (g_screen == SCR_DELETE_CONFIRM) {
        if (is_ok) {
            if (g_del_is_folder) {
                folder_delete(g_del_target_idx);
                show_toast("已刪除資料夾（書籍保留於根目錄）");
            } else {
                remove_book_at(g_del_target_idx);
                show_toast("已從書架移除書籍");
            }
            g_screen = SCR_HOME;
        } else if (key == RKEY_BACK || key == RKEY_BACKSPACE) {
            g_screen = SCR_HOME;
        }
        return;
    }

    if (g_screen == SCR_NEXT_VOL) {
        if (is_ok && g_next_vol_idx >= 0 && g_next_vol_idx < g_book_count) {
            char np[384];
            snprintf(np, sizeof(np), "%s", g_books[g_next_vol_idx].path);
            reader_open_file(np);
        } else if (key == RKEY_BACK || key == RKEY_BACKSPACE) {
            g_screen = active_read_screen();
        }
        return;
    }

    if (g_screen == SCR_NEW_FOLDER) {
        if (key == RKEY_BACK) { g_screen = SCR_HOME; return; }
        if (key == RKEY_BACKSPACE) {
            size_t l = strlen(g_new_folder_buf);
            if (l > 0) g_new_folder_buf[l - 1] = '\0';
            return;
        }
        if (key == RKEY_OK) {
            if (g_new_folder_buf[0]) {
                if (folder_create(g_new_folder_buf)) show_toast("已建立資料夾");
                else show_toast("名稱重複或已達上限");
            }
            g_screen = SCR_HOME;
            return;
        }
        if (raw_char >= 0x20 && raw_char < 0x7F && raw_char != '|') {
            size_t l = strlen(g_new_folder_buf);
            if (l + 1 < sizeof(g_new_folder_buf)) {
                g_new_folder_buf[l] = raw_char;
                g_new_folder_buf[l + 1] = '\0';
            }
        }
        return;
    }

    if (g_screen == SCR_MOVE_FOLDER) {
        int tot = g_folder_count + 1;
        if (key == RKEY_BACK || key == RKEY_BACKSPACE) { g_screen = SCR_HOME; return; }
        if ((key == RKEY_UP || key == RKEY_LEFT) && g_move_folder_sel > 0) g_move_folder_sel--;
        else if ((key == RKEY_DOWN || key == RKEY_RIGHT) && g_move_folder_sel + 1 < tot) g_move_folder_sel++;
        else if (is_ok) {
            if (g_move_book_idx >= 0 && g_move_book_idx < g_book_count) {
                if (g_move_folder_sel == 0) g_books[g_move_book_idx].folder[0] = '\0';
                else snprintf(g_books[g_move_book_idx].folder, 48, "%s", g_folders[g_move_folder_sel - 1]);
                save_library();
                show_toast("已移動書籍");
            }
            g_screen = SCR_HOME;
        }
        return;
    }

    if (g_screen == SCR_HOME) {
        if (key == RKEY_HELP) { open_help_screen(SCR_HOME); return; }
        if (key == RKEY_OPEN) { open_new_book_dialog(); return; }
        if (key == RKEY_THEME) {
            g_theme_idx = (g_theme_idx + 1) % NUM_THEMES;
            save_library();
            show_toast(theme_get(g_theme_idx)->name);
            return;
        }
        if (key == RKEY_TAB) {
            g_home_tab = (g_home_tab == TAB_SHELF) ? TAB_SETTINGS : TAB_SHELF;
            return;
        }
        if (key == RKEY_TAB1) { g_home_tab = TAB_SHELF; return; }
        if (key == RKEY_TAB2) { g_home_tab = TAB_SETTINGS; return; }

        if (g_home_tab == TAB_SETTINGS) {
            const int NUM_SET_ROWS = 8;
            if (key == RKEY_BACK || key == RKEY_BACKSPACE) { g_home_tab = TAB_SHELF; return; }
            if (key == RKEY_UP && g_set_row > 0) g_set_row--;
            else if (key == RKEY_DOWN && g_set_row + 1 < NUM_SET_ROWS) g_set_row++;
            else if (key == RKEY_LEFT || key == RKEY_RIGHT || is_ok) {
                int d = (key == RKEY_LEFT) ? -1 : 1;
                switch (g_set_row) {
                    case 0: g_theme_idx     = (g_theme_idx + NUM_THEMES + d) % NUM_THEMES; break;
                    case 1: g_font_style    = (g_font_style + 4 + d) % 4; hal_set_font_style(g_font_style, g_font_size_idx); break;
                    case 2: g_font_size_idx = (g_font_size_idx + 4 + d) % 4; hal_set_font_style(g_font_style, g_font_size_idx); break;
                    case 3: g_spacing_idx   = (g_spacing_idx + 5 + d) % 5; break;
                    case 4: g_margin_idx    = (g_margin_idx + 4 + d) % 4; break;
                    case 5: g_auto_sec_idx  = (g_auto_sec_idx + 7 + d) % 7; break;
                    case 6: g_pdf_def_zoom  = (g_pdf_def_zoom + 5 + d) % 5; break;
                    case 7: g_pdf_pan_idx   = (g_pdf_pan_idx + 3 + d) % 3; break;
                }
                save_library();
            }
            return;
        }

        int types[MAX_BOOKS + MAX_FOLDERS], idxs[MAX_BOOKS + MAX_FOLDERS];
        int cnt = shelf_build_list(types, idxs, MAX_BOOKS + MAX_FOLDERS);
        if (cnt > 0 && g_shelf_sel >= cnt) g_shelf_sel = cnt - 1;
        if (cnt == 0) { g_shelf_sel = 0; g_shelf_scroll = 0; }

        if (key == RKEY_SEARCH) {
            g_filter_fmt = (g_filter_fmt + 1) % 5;
            g_shelf_sel = 0;
            g_shelf_scroll = 0;
            char msg[48];
            snprintf(msg, sizeof(msg), "篩選格式：%s", FMT_FILTER_NM[g_filter_fmt]);
            show_toast(msg);
            return;
        }
        if (key == RKEY_NEW_FOLDER) {
            g_new_folder_buf[0] = '\0';
            g_screen = SCR_NEW_FOLDER;
            return;
        }
        if (key == RKEY_BACK || key == RKEY_BACKSPACE) {
            if (g_cur_folder[0]) {
                g_cur_folder[0] = '\0';
                g_shelf_sel = 0;
                g_shelf_scroll = 0;
            } else if (g_filter_fmt != 0) {
                g_filter_fmt = 0;
                g_shelf_sel = 0;
                g_shelf_scroll = 0;
            } else if (key == RKEY_BACK) {
                g_screen = SCR_QUIT_CONFIRM;
            }
            return;
        }
        if (cnt <= 0) {
            if (is_ok) open_new_book_dialog();
            return;
        }

        if (key == RKEY_UP || key == RKEY_LEFT) {
            if (g_shelf_sel > 0) g_shelf_sel--;
        } else if (key == RKEY_DOWN || key == RKEY_RIGHT) {
            if (g_shelf_sel + 1 < cnt) g_shelf_sel++;
        } else if (key == RKEY_PGUP) {
            g_shelf_sel = (g_shelf_sel >= 5) ? (g_shelf_sel - 5) : 0;
        } else if (key == RKEY_PGDN) {
            g_shelf_sel += 5;
            if (g_shelf_sel >= cnt) g_shelf_sel = cnt - 1;
        } else if (key == RKEY_MOVE_FOLDER && types[g_shelf_sel] == 0) {
            g_move_book_idx = idxs[g_shelf_sel];
            g_move_folder_sel = 0;
            g_move_folder_scroll = 0;
            g_screen = SCR_MOVE_FOLDER;
        } else if (key == RKEY_DELETE) {
            g_del_is_folder = (types[g_shelf_sel] == 1);
            g_del_target_idx = idxs[g_shelf_sel];
            g_screen = SCR_DELETE_CONFIRM;
        } else if (is_ok) {
            if (types[g_shelf_sel] == 1) {
                snprintf(g_cur_folder, sizeof(g_cur_folder), "%s", g_folders[idxs[g_shelf_sel]]);
                g_shelf_sel = 0;
                g_shelf_scroll = 0;
            } else {
                char path[384];
                snprintf(path, sizeof(path), "%s", g_books[idxs[g_shelf_sel]].path);
                reader_open_file(path);
            }
        }
        return;
    }

    if (g_screen == SCR_READ_MENU) {
        int mcnt = get_read_menu_count();
        if (key == RKEY_BACK || key == RKEY_MENU || key == RKEY_BACKSPACE) {
            g_screen = active_read_screen();
            return;
        }
        if (key == RKEY_HELP) { open_help_screen(SCR_READ_MENU); return; }
        if (key == RKEY_UP && g_read_menu_sel > 0) g_read_menu_sel--;
        else if (key == RKEY_DOWN && g_read_menu_sel + 1 < mcnt) g_read_menu_sel++;
        else if (key == RKEY_LEFT) activate_read_menu_item(g_read_menu_sel, -1);
        else if (key == RKEY_RIGHT || is_ok) activate_read_menu_item(g_read_menu_sel, 1);
        return;
    }

    if (g_screen == SCR_BOOKMARKS) {
        if (key == RKEY_BACK || key == RKEY_BOOKMARK || key == RKEY_BACKSPACE) {
            g_screen = active_read_screen();
            return;
        }
        if (key == RKEY_NEW_FOLDER || key == RKEY_ZOOM_IN || raw_char == 'a' || raw_char == 'A') {
            add_current_manual_bookmark();
            return;
        }
        if (key == RKEY_UP && g_bmark_sel > 0) g_bmark_sel--;
        else if (key == RKEY_DOWN && g_bmark_sel + 1 < g_bmark_count) g_bmark_sel++;
        else if (key == RKEY_DELETE && g_bmark_count > 0) {
            for (int i = g_bmark_sel; i + 1 < g_bmark_count; i++) g_bmarks[i] = g_bmarks[i + 1];
            g_bmark_count--;
            if (g_bmark_sel >= g_bmark_count && g_bmark_count > 0) g_bmark_sel = g_bmark_count - 1;
            save_manual_bookmarks();
            show_toast("已刪除書籤");
        } else if (is_ok) {
            if (g_bmark_count == 0) {
                add_current_manual_bookmark();
            } else if (g_bmark_sel >= 0 && g_bmark_sel < g_bmark_count) {
                if (g_doc.format == BOOK_FMT_PDF && g_pdf) {
                    g_pdf_page = (int)g_bmarks[g_bmark_sel].pos_val;
                    g_pdf_pan_x = 0; g_pdf_pan_y = 0; g_pdf_reflow_scroll = 0;
                } else {
                    jump_to_offset(g_bmarks[g_bmark_sel].pos_val);
                }
                save_bookmark();
                g_screen = active_read_screen();
            }
        }
        return;
    }

    if (g_screen == SCR_SEARCH) {
        if (key == RKEY_BACK) { g_screen = active_read_screen(); return; }
        if (key == RKEY_BACKSPACE) {
            size_t l = strlen(g_search_query);
            if (l > 0) { g_search_query[l - 1] = '\0'; g_search_ran = 0; }
            return;
        }
        if (key == RKEY_UP && g_search_sel > 0) { g_search_sel--; return; }
        if (key == RKEY_DOWN && g_search_sel + 1 < g_search_cnt) { g_search_sel++; return; }
        if (key == RKEY_OK) {
            if (!g_search_ran) {
                run_fulltext_search();
            } else if (g_search_cnt > 0 && g_search_sel < g_search_cnt) {
                if (g_doc.format == BOOK_FMT_PDF && g_pdf) {
                    g_pdf_page = (int)g_search_hits[g_search_sel].pos_val;
                    g_pdf_pan_x = 0; g_pdf_pan_y = 0; g_pdf_reflow_scroll = 0;
                } else {
                    jump_to_offset(g_search_hits[g_search_sel].pos_val);
                }
                save_bookmark();
                g_screen = active_read_screen();
            }
            return;
        }
        if (raw_char >= 0x20 && raw_char < 0x7F) {
            size_t l = strlen(g_search_query);
            if (l + 1 < sizeof(g_search_query)) {
                g_search_query[l] = raw_char;
                g_search_query[l + 1] = '\0';
                g_search_ran = 0;
            }
        }
        return;
    }

    if (g_screen == SCR_TOC) {
        if (key == RKEY_BACK || key == RKEY_TOC || key == RKEY_BACKSPACE) {
            g_screen = active_read_screen();
            return;
        }
        if ((key == RKEY_UP || key == RKEY_LEFT) && g_toc_sel > 0) g_toc_sel--;
        else if ((key == RKEY_DOWN || key == RKEY_RIGHT) && g_toc_sel + 1 < g_doc.chapter_count) g_toc_sel++;
        else if (key == RKEY_PGUP) g_toc_sel = (g_toc_sel >= 6) ? (g_toc_sel - 6) : 0;
        else if (key == RKEY_PGDN) {
            g_toc_sel += 6;
            if (g_toc_sel >= g_doc.chapter_count) g_toc_sel = g_doc.chapter_count > 0 ? g_doc.chapter_count - 1 : 0;
        } else if (is_ok) {
            if (g_doc.chapter_count > 0) {
                if (g_doc.format == BOOK_FMT_PDF && g_pdf) {
                    g_pdf_page = (int)g_doc.chapters[g_toc_sel].offset;
                    g_pdf_pan_x = 0; g_pdf_pan_y = 0; g_pdf_reflow_scroll = 0;
                } else {
                    jump_to_offset(g_doc.chapters[g_toc_sel].offset);
                }
                save_bookmark();
            }
            g_screen = active_read_screen();
        }
        return;
    }

    if (g_screen == SCR_JUMP) {
        int tot = (g_doc.format == BOOK_FMT_PDF && g_pdf) ? pdf_page_count(g_pdf) : g_page_count;
        if (key == RKEY_BACK || key == RKEY_JUMP) { g_screen = active_read_screen(); return; }
        if (raw_char >= '0' && raw_char <= '9') {
            size_t l = strlen(g_jump_digits);
            if (l + 1 < sizeof(g_jump_digits)) {
                g_jump_digits[l] = raw_char;
                g_jump_digits[l + 1] = '\0';
                int v = atoi(g_jump_digits) - 1;
                if (v < 0) v = 0;
                if (tot > 0 && v >= tot) v = tot - 1;
                g_jump_target = v;
            }
            return;
        }
        if (key == RKEY_BACKSPACE) {
            size_t l = strlen(g_jump_digits);
            if (l > 0) {
                g_jump_digits[l - 1] = '\0';
                if (g_jump_digits[0]) {
                    int v = atoi(g_jump_digits) - 1;
                    if (v < 0) v = 0;
                    if (tot > 0 && v >= tot) v = tot - 1;
                    g_jump_target = v;
                }
            }
            return;
        }
        if (key == RKEY_LEFT)  { g_jump_digits[0] = '\0'; g_jump_target -= 10; }
        else if (key == RKEY_RIGHT) { g_jump_digits[0] = '\0'; g_jump_target += 10; }
        else if (key == RKEY_UP)    { g_jump_digits[0] = '\0'; g_jump_target -= 50; }
        else if (key == RKEY_DOWN)  { g_jump_digits[0] = '\0'; g_jump_target += 50; }
        else if (key == RKEY_PGUP)  { g_jump_digits[0] = '\0'; g_jump_target -= 1; }
        else if (key == RKEY_PGDN)  { g_jump_digits[0] = '\0'; g_jump_target += 1; }
        else if (is_ok) {
            if (g_jump_target >= 0 && g_jump_target < tot) {
                if (g_doc.format == BOOK_FMT_PDF && g_pdf) {
                    g_pdf_page = g_jump_target;
                    g_pdf_pan_x = 0; g_pdf_pan_y = 0; g_pdf_reflow_scroll = 0;
                } else {
                    g_current_page = g_jump_target;
                }
                save_bookmark();
            }
            g_screen = active_read_screen();
            return;
        }
        if (g_jump_target < 0) g_jump_target = 0;
        if (g_jump_target >= tot) g_jump_target = tot > 0 ? tot - 1 : 0;
        return;
    }

    if (key == RKEY_HELP) { open_help_screen(g_screen); return; }
    if (key == RKEY_AUTO) {
        g_auto_active = !g_auto_active;
        g_auto_elapsed = 0;
        show_toast(g_auto_active ? "已開啟自動翻頁" : "已關閉自動翻頁");
        return;
    }
    if (key == RKEY_FULLSCREEN) {
        g_fullscreen = !g_fullscreen;
        save_library();
        if (g_screen == SCR_READ_TEXT) reader_on_resize();
        else if (g_screen == SCR_READ_PDF) clamp_pdf_pan();
        show_toast(g_fullscreen ? "全螢幕模式（按 I 恢復）" : "標準模式");
        return;
    }

    /* PDF 閱讀畫面（完整保留原版 PDF 邏輯 + 純按鍵縮放 Z/X/0/+/-） */
    if (g_screen == SCR_READ_PDF && g_pdf) {
        int tot = pdf_page_count(g_pdf);
        int vw = hal_get_width();
        int vh = get_read_view_h();
        int pan_step = PDF_PAN_STEP[g_pdf_pan_idx];
        int cpx = (!g_pdf_reflow && g_pdf_last_pw > vw);
        int cpy = (!g_pdf_reflow && g_pdf_last_ph > vh);

        switch (key) {
            case RKEY_PGUP:
                if (g_pdf_page > 0) {
                    g_pdf_page--; g_pdf_pan_x = 0; g_pdf_pan_y = 0; g_pdf_reflow_scroll = 0;
                    save_bookmark();
                }
                break;
            case RKEY_PGDN:
            case RKEY_SPACE:
                if (g_pdf_page + 1 < tot) {
                    g_pdf_page++; g_pdf_pan_x = 0; g_pdf_pan_y = 0; g_pdf_reflow_scroll = 0;
                    save_bookmark();
                } else {
                    int nv = find_next_volume_in_folder(g_filepath);
                    if (nv >= 0) { g_next_vol_idx = nv; g_screen = SCR_NEXT_VOL; }
                }
                break;
            case RKEY_LEFT:
                if (cpx && g_pdf_pan_x > 0) {
                    g_pdf_pan_x -= pan_step;
                    clamp_pdf_pan();
                } else if (g_pdf_page > 0) {
                    g_pdf_page--; g_pdf_pan_x = 0; g_pdf_pan_y = 0; g_pdf_reflow_scroll = 0;
                    save_bookmark();
                }
                break;
            case RKEY_RIGHT:
                if (cpx && g_pdf_pan_x < g_pdf_last_pw - vw) {
                    g_pdf_pan_x += pan_step;
                    clamp_pdf_pan();
                } else if (g_pdf_page + 1 < tot) {
                    g_pdf_page++; g_pdf_pan_x = 0; g_pdf_pan_y = 0; g_pdf_reflow_scroll = 0;
                    save_bookmark();
                }
                break;
            case RKEY_UP:
                if (g_pdf_reflow && g_pdf_reflow_scroll > 0) {
                    g_pdf_reflow_scroll--;
                } else if (cpy && g_pdf_pan_y > 0) {
                    g_pdf_pan_y -= pan_step;
                    clamp_pdf_pan();
                } else if (g_pdf_page > 0) {
                    g_pdf_page--; g_pdf_pan_x = 0; g_pdf_pan_y = 0; g_pdf_reflow_scroll = 0;
                    save_bookmark();
                }
                break;
            case RKEY_DOWN:
                if (g_pdf_reflow) {
                    int max_u, lines, lh, px;
                    get_layout_params(&max_u, &lines, &lh, &px);
                    int tot_ln = count_pdf_reflow_lines();
                    if (g_pdf_reflow_scroll + lines < tot_ln) {
                        g_pdf_reflow_scroll++;
                    } else if (g_pdf_page + 1 < tot) {
                        g_pdf_page++; g_pdf_pan_x = 0; g_pdf_pan_y = 0; g_pdf_reflow_scroll = 0;
                        save_bookmark();
                    }
                } else if (cpy && g_pdf_pan_y < g_pdf_last_ph - vh) {
                    g_pdf_pan_y += pan_step;
                    clamp_pdf_pan();
                } else if (g_pdf_page + 1 < tot) {
                    g_pdf_page++; g_pdf_pan_x = 0; g_pdf_pan_y = 0; g_pdf_reflow_scroll = 0;
                    save_bookmark();
                }
                break;
            case RKEY_ZOOM_IN:
                if (g_pdf_zoom < 4) {
                    g_pdf_zoom++; g_pdf_pan_x = 0; g_pdf_pan_y = 0;
                }
                {
                    char zmsg[48];
                    snprintf(zmsg, sizeof(zmsg), "PDF 縮放：%s", PDF_ZOOM_NM[g_pdf_zoom]);
                    show_toast(zmsg);
                }
                break;
            case RKEY_ZOOM_OUT:
                if (g_pdf_zoom > 0) {
                    g_pdf_zoom--; g_pdf_pan_x = 0; g_pdf_pan_y = 0;
                }
                {
                    char zmsg[48];
                    snprintf(zmsg, sizeof(zmsg), "PDF 縮放：%s", PDF_ZOOM_NM[g_pdf_zoom]);
                    show_toast(zmsg);
                }
                break;
            case RKEY_ZOOM_RESET:
                g_pdf_zoom = 0; g_pdf_pan_x = 0; g_pdf_pan_y = 0;
                show_toast("PDF 縮放：適應整頁");
                break;
            case RKEY_REFLOW:
                g_pdf_reflow = !g_pdf_reflow;
                g_pdf_reflow_scroll = 0;
                show_toast(g_pdf_reflow ? "文字重排模式" : "PDF 原版檢視");
                break;
            case RKEY_THEME:
                g_theme_idx = (g_theme_idx + 1) % NUM_THEMES;
                save_library();
                show_toast(theme_get(g_theme_idx)->name);
                break;
            case RKEY_TOC: open_toc_screen(); break;
            case RKEY_JUMP: open_jump_screen(); break;
            case RKEY_BOOKMARK: g_screen = SCR_BOOKMARKS; break;
            case RKEY_SEARCH: g_search_ran = 0; g_screen = SCR_SEARCH; break;
            case RKEY_OPEN: open_new_book_dialog(); break;
            case RKEY_SHELF:
            case RKEY_BACKSPACE:
                save_bookmark(); save_manual_bookmarks(); g_screen = SCR_HOME;
                break;
            case RKEY_BACK:
            case RKEY_MENU:
                g_read_menu_sel = 0; g_read_menu_scroll = 0; g_screen = SCR_READ_MENU;
                break;
            default: break;
        }
        return;
    }

    if (g_screen == SCR_READ_IMG) {
        if (key == RKEY_LEFT || key == RKEY_UP || key == RKEY_PGUP) {
            char np[384] = {0};
            if (hal_find_sibling_image(g_filepath, -1, np, sizeof(np))) {
                snprintf(g_filepath, sizeof(g_filepath), "%s", np);
                utf8_ellipsis(get_filename_only(np), 40, g_doc.title, sizeof(g_doc.title));
            }
        } else if (key == RKEY_RIGHT || key == RKEY_DOWN || key == RKEY_PGDN || key == RKEY_SPACE) {
            char np[384] = {0};
            if (hal_find_sibling_image(g_filepath, 1, np, sizeof(np))) {
                snprintf(g_filepath, sizeof(g_filepath), "%s", np);
                utf8_ellipsis(get_filename_only(np), 40, g_doc.title, sizeof(g_doc.title));
            }
        } else if (key == RKEY_THEME) {
            g_theme_idx = (g_theme_idx + 1) % NUM_THEMES;
            save_library();
        } else if (key == RKEY_OPEN) {
            open_new_book_dialog();
        } else if (key == RKEY_SHELF || key == RKEY_BACKSPACE) {
            save_bookmark(); g_screen = SCR_HOME;
        } else if (key == RKEY_BACK || key == RKEY_MENU) {
            g_read_menu_sel = 0; g_read_menu_scroll = 0; g_screen = SCR_READ_MENU;
        }
        return;
    }

    switch (key) {
        case RKEY_LEFT:
        case RKEY_UP:
        case RKEY_PGUP:
            if (g_current_page > 0) { g_current_page--; save_bookmark(); }
            break;
        case RKEY_RIGHT:
        case RKEY_DOWN:
        case RKEY_PGDN:
        case RKEY_SPACE:
            if (g_current_page + 1 < g_page_count) {
                g_current_page++; save_bookmark();
            } else {
                int nv = find_next_volume_in_folder(g_filepath);
                if (nv >= 0) { g_next_vol_idx = nv; g_screen = SCR_NEXT_VOL; }
            }
            break;
        case RKEY_ZOOM_IN:
            if (g_font_size_idx < 3) {
                g_font_size_idx++; save_library(); reader_on_resize();
                show_toast(FONT_SIZE_NM[g_font_size_idx]);
            }
            break;
        case RKEY_ZOOM_OUT:
            if (g_font_size_idx > 0) {
                g_font_size_idx--; save_library(); reader_on_resize();
                show_toast(FONT_SIZE_NM[g_font_size_idx]);
            }
            break;
        case RKEY_TOC: open_toc_screen(); break;
        case RKEY_JUMP: if (g_page_count > 0) open_jump_screen(); break;
        case RKEY_BOOKMARK: g_screen = SCR_BOOKMARKS; break;
        case RKEY_SEARCH: g_search_ran = 0; g_screen = SCR_SEARCH; break;
        case RKEY_THEME:
            g_theme_idx = (g_theme_idx + 1) % NUM_THEMES;
            save_library();
            show_toast(theme_get(g_theme_idx)->name);
            break;
        case RKEY_SPACING:
            g_spacing_idx = (g_spacing_idx + 1) % 5;
            save_library();
            reader_on_resize();
            show_toast(SPACING_NM[g_spacing_idx]);
            break;
        case RKEY_OPEN: open_new_book_dialog(); break;
        case RKEY_SHELF:
        case RKEY_BACKSPACE:
            save_bookmark(); save_manual_bookmarks(); g_screen = SCR_HOME;
            break;
        case RKEY_BACK:
        case RKEY_MENU:
            g_read_menu_sel = 0; g_read_menu_scroll = 0; g_screen = SCR_READ_MENU;
            break;
        default: break;
    }
}

/* =========================================================================
 * UI 渲染（小螢幕自適應 320x240 ~ 800x480）
 * ========================================================================= */

static int calc_book_pct(const BookEntry *be) {
    if (be->page_count > 0) return ((be->page_idx + 1) * 100) / be->page_count;
    if (be->total_bytes > 0) return (int)((be->byte_offset * 100u) / be->total_bytes);
    return 0;
}

static void draw_clock_right(const AppTheme *th, int w, int y) {
    int yr, mo, dy, hh, mm, ss;
    hal_get_local_time(&yr, &mo, &dy, &hh, &mm, &ss);
    char clk[16];
    snprintf(clk, sizeof(clk), "%02d:%02d", hh, mm);
    int cw = ui_ascii_width(clk, 1);
    ui_draw_ascii(w - cw - 6, y, clk, 1, th->muted);
}

static void begin_modal(int w, int h, int mx, int my, int mw, int mh) {
    hal_clear_text_in_rect(mx - 4, my - 4, mw + 8, mh + 8);
    hal_dim_rect(0, 0, w, h);
}

static void render_home_top_tabs(const AppTheme *th, int w, int top_h) {
    hal_fill_rect(0, 0, w, top_h, th->bar_bg);
    hal_fill_rect(0, top_h, w, 1, th->card_border);

    int ty = (top_h - 16) / 2;
    if (ty < 2) ty = 2;

    int t1_w = 78;
    uint32_t t1_bg = (g_home_tab == TAB_SHELF) ? th->accent : th->card_bg;
    ui_fill_round_rect(4, 3, t1_w, top_h - 6, 4, t1_bg);
    if (g_home_tab != TAB_SHELF) ui_draw_round_rect(4, 3, t1_w, top_h - 6, 4, th->card_border);
    hal_set_text_bg(t1_bg);
    draw_str(10, ty, "1 書架", t1_w - 8, (g_home_tab == TAB_SHELF) ? 0xFFFFFF : th->text);

    int t2_x = 4 + t1_w + 5;
    int t2_w = 78;
    uint32_t t2_bg = (g_home_tab == TAB_SETTINGS) ? th->accent : th->card_bg;
    ui_fill_round_rect(t2_x, 3, t2_w, top_h - 6, 4, t2_bg);
    if (g_home_tab != TAB_SETTINGS) ui_draw_round_rect(t2_x, 3, t2_w, top_h - 6, 4, th->card_border);
    hal_set_text_bg(t2_bg);
    draw_str(t2_x + 6, ty, "2 設定", t2_w - 8, (g_home_tab == TAB_SETTINGS) ? 0xFFFFFF : th->text);

    int info_x = t2_x + t2_w + 6;
    int info_w = w - info_x - 42;
    if (info_w > 32) {
        hal_set_text_bg(th->bar_bg);
        if (g_cur_folder[0]) {
            char fb[64], el[40];
            snprintf(fb, sizeof(fb), "/%s", g_cur_folder);
            utf8_ellipsis(fb, info_w / 8, el, sizeof(el));
            draw_str(info_x, ty, el, info_w, th->accent);
        } else if (g_filter_fmt != 0) {
            char fb[32];
            snprintf(fb, sizeof(fb), "[%s]", FMT_FILTER_NM[g_filter_fmt]);
            draw_str(info_x, ty, fb, info_w, th->accent);
        } else {
            draw_str(info_x, ty, "CD-358", info_w, th->muted);
        }
    }

    draw_clock_right(th, w, (top_h - 7) / 2);
}

static void render_home_shelf(const AppTheme *th, int w, int h) {
    int sm = is_small_screen();
    int top_h = sm ? 24 : 30;
    int bot_h = sm ? 22 : 26;
    hal_clear(th->bg);
    render_home_top_tabs(th, w, top_h);

    int list_y = top_h + 4;
    int list_h = h - top_h - bot_h - 8;
    int row_h = sm ? 36 : 44;
    int vis = list_h / row_h;
    if (vis < 1) vis = 1;

    int types[MAX_BOOKS + MAX_FOLDERS], idxs[MAX_BOOKS + MAX_FOLDERS];
    int cnt = shelf_build_list(types, idxs, MAX_BOOKS + MAX_FOLDERS);
    if (cnt > 0 && g_shelf_sel >= cnt) g_shelf_sel = cnt - 1;
    if (g_shelf_sel < g_shelf_scroll) g_shelf_scroll = g_shelf_sel;
    if (g_shelf_sel >= g_shelf_scroll + vis) g_shelf_scroll = g_shelf_sel - vis + 1;
    if (g_shelf_scroll < 0) g_shelf_scroll = 0;

    if (cnt == 0) {
        int cw = w - 24;
        if (cw > 360) cw = 360;
        int ch = 104;
        int cx = (w - cw) / 2, cy = list_y + (list_h - ch) / 2;
        if (cy < list_y) cy = list_y;
        ui_card(cx, cy, cw, ch, 8, th->card_bg, th->card_border, th->shadow, 0, th->accent);
        hal_set_text_bg(th->card_bg);
        draw_str(cx + 14, cy + 14, g_cur_folder[0] ? "此資料夾目前無書籍" : "書架目前尚無書籍", cw - 28, th->text);
        draw_str(cx + 14, cy + 40, "按 [O] 或 [Enter] 匯入檔案", cw - 28, th->accent);
        draw_str(cx + 14, cy + 64, "支援 PDF / EPUB / TXT / 圖", cw - 28, th->muted);
        draw_str(cx + 14, cy + 82, "按 [H] 查看完整按鍵說明", cw - 28, th->muted);
    } else {
        int pad_x = sm ? 6 : 12;
        int card_w = w - pad_x * 2 - (cnt > vis ? 6 : 0);

        for (int i = 0; i < vis && (g_shelf_scroll + i) < cnt; i++) {
            int gi = g_shelf_scroll + i;
            int ry = list_y + i * row_h;
            int sel = (gi == g_shelf_sel);
            int ch = row_h - 4;
            uint32_t bg = sel ? ui_blend_color(th->card_bg, th->accent, 16) : th->card_bg;
            uint32_t bd = sel ? th->accent : th->card_border;

            ui_fill_round_rect(pad_x, ry, card_w, ch, 5, bg);
            ui_draw_round_rect(pad_x, ry, card_w, ch, 5, bd);
            if (sel) hal_fill_rect(pad_x + 1, ry + 3, 4, ch - 6, th->accent);

            if (types[gi] == 1) {
                const char *fn = g_folders[idxs[gi]];
                int bc = folder_count_books(fn);
                ui_draw_badge(pad_x + 9, ry + (ch - 14) / 2, "DIR", th->accent, 0xFFFFFF);
                hal_set_text_bg(bg);
                int tx = pad_x + 52;
                int tw = card_w - 60;
                char tbuf[64], info[48];
                utf8_ellipsis(fn, tw / 8, tbuf, sizeof(tbuf));
                draw_str(tx, ry + 3, tbuf, tw, th->text);
                snprintf(info, sizeof(info), "資料夾 · %d 本書 (Enter進入)", bc);
                draw_str(tx, ry + (sm ? 18 : 22), info, tw, th->muted);
            } else {
                const BookEntry *be = &g_books[idxs[gi]];
                int pct = calc_book_pct(be);
                uint32_t fc = ui_format_color(be->format);
                ui_draw_badge(pad_x + 9, ry + 4, book_format_tag(be->format), fc, 0xFFFFFF);

                hal_set_text_bg(bg);
                int tx = pad_x + 54;
                int tw = card_w - 60;
                char tbuf[72];
                utf8_ellipsis(be->title, tw / 8, tbuf, sizeof(tbuf));
                draw_str(tx, ry + 3, tbuf, tw, th->text);

                int bar_y = ry + ch - 8;
                int bar_w = sm ? 86 : 140;
                if (bar_w > tw - 60) bar_w = tw / 2;
                ui_progress_bar(tx, bar_y, bar_w, 4, pct, th->track, fc);

                char meta[48];
                if (be->page_count > 0) {
                    snprintf(meta, sizeof(meta), "%d%% (%d/%d)", pct, be->page_idx + 1, be->page_count);
                } else {
                    snprintf(meta, sizeof(meta), "%d%%", pct);
                }
                ui_draw_ascii(tx + bar_w + 6, bar_y - 2, meta, 1, th->muted);
            }
        }

        if (cnt > vis) {
            int sb_x = w - 5;
            int sb_h = (list_h * vis) / cnt;
            if (sb_h < 14) sb_h = 14;
            int sb_y = list_y + ((list_h - sb_h) * g_shelf_scroll) / (cnt - vis);
            hal_fill_rect(sb_x, list_y, 3, list_h, th->track);
            hal_fill_rect(sb_x, sb_y, 3, sb_h, th->accent);
        }
    }

    int by = h - bot_h;
    hal_fill_rect(0, by, w, bot_h, th->bar_bg);
    hal_fill_rect(0, by, w, 1, th->card_border);
    hal_set_text_bg(th->bar_bg);
    int bty = by + (bot_h - 16) / 2;
    if (bty < by + 2) bty = by + 2;
    if (g_toast_msg[0]) {
        draw_str(6, bty, g_toast_msg, w - 12, th->accent);
    } else if (sm) {
        draw_str(6, bty, "O開檔 F篩 N夾 V移 D刪 T色 H說明", w - 12, th->muted);
    } else {
        draw_str(10, bty, "[Enter]開啟  [O]匯入  [F]篩選  [N]建夾  [V]移夾  [D]刪除  [T]主題  [H]說明", w - 20, th->muted);
    }
}

static void render_home_settings(const AppTheme *th, int w, int h) {
    int sm = is_small_screen();
    int top_h = sm ? 24 : 30;
    int bot_h = sm ? 22 : 26;
    hal_clear(th->bg);
    render_home_top_tabs(th, w, top_h);

    const char *labels[8] = {
        "佈景主題",
        "字體風格",
        "文字大小",
        "文字行距",
        "左右邊距",
        "自動翻頁",
        "PDF 預設縮放",
        "PDF 平移步距"
    };
    char vals[8][48];
    snprintf(vals[0], sizeof(vals[0]), "%s (%d/%d)", theme_get(g_theme_idx)->name, g_theme_idx + 1, NUM_THEMES);
    snprintf(vals[1], sizeof(vals[1]), "%s", FONT_STYLE_NM[g_font_style]);
    snprintf(vals[2], sizeof(vals[2]), "%s", FONT_SIZE_NM[g_font_size_idx]);
    snprintf(vals[3], sizeof(vals[3]), "%s", SPACING_NM[g_spacing_idx]);
    snprintf(vals[4], sizeof(vals[4]), "%s", MARGIN_NM[g_margin_idx]);
    snprintf(vals[5], sizeof(vals[5]), "每 %d 秒", AUTO_SECS[g_auto_sec_idx]);
    snprintf(vals[6], sizeof(vals[6]), "%s", PDF_ZOOM_NM[g_pdf_def_zoom]);
    snprintf(vals[7], sizeof(vals[7]), "%s px", PDF_PAN_NM[g_pdf_pan_idx]);

    int list_y = top_h + 4;
    int list_h = h - top_h - bot_h - 8;
    int row_h = list_h / 8;
    if (row_h > 34) row_h = 34;
    if (row_h < 21) row_h = 21;
    int vis = list_h / row_h;
    if (vis < 1) vis = 1;
    int scroll = 0;
    if (g_set_row >= vis) scroll = g_set_row - vis + 1;

    int pad_x = sm ? 6 : 16;
    int cw = w - pad_x * 2;
    for (int i = 0; i < vis && (scroll + i) < 8; i++) {
        int idx = scroll + i;
        int ry = list_y + i * row_h;
        int sel = (idx == g_set_row);
        uint32_t bg = sel ? ui_blend_color(th->card_bg, th->accent, 18) : th->card_bg;
        ui_fill_round_rect(pad_x, ry, cw, row_h - 2, 4, bg);
        ui_draw_round_rect(pad_x, ry, cw, row_h - 2, 4, sel ? th->accent : th->card_border);

        int ty = ry + (row_h - 18) / 2;
        if (ty < ry + 1) ty = ry + 1;
        hal_set_text_bg(bg);
        int lw = cw / 2 - 4;
        draw_str(pad_x + 8, ty, labels[idx], lw, th->text);
        char vbuf[60];
        snprintf(vbuf, sizeof(vbuf), sel ? "< %s >" : "%s", vals[idx]);
        draw_str(pad_x + cw / 2, ty, vbuf, cw / 2 - 6, sel ? th->accent : th->muted);
    }

    int by = h - bot_h;
    hal_fill_rect(0, by, w, bot_h, th->bar_bg);
    hal_fill_rect(0, by, w, 1, th->card_border);
    hal_set_text_bg(th->bar_bg);
    int bty = by + (bot_h - 16) / 2;
    if (bty < by + 2) bty = by + 2;
    draw_str(6, bty, "↑↓選項目 ←→調整  [1/ESC]回書架 [H]資訊", w - 12, th->muted);
}

static void render_reading_base(const AppTheme *th, int w, int h) {
    int sm = is_small_screen();
    int top_bar = get_top_bar_h();
    int bot_bar = get_bot_bar_h();
    int vy = get_read_view_y();
    int vh = get_read_view_h();

    hal_clear(th->bg);

    if (top_bar > 0) {
        hal_fill_rect(0, 0, w, top_bar, th->bar_bg);
        hal_fill_rect(0, top_bar, w, 1, th->card_border);
        int ty = (top_bar - 16) / 2;
        if (ty < 2) ty = 2;

        ui_draw_badge(4, (top_bar - 14) / 2, book_format_tag(g_doc.format), ui_format_color(g_doc.format), 0xFFFFFF);

        int right_reserved = 42;
        if (g_doc.format == BOOK_FMT_PDF && g_pdf) {
            const char *ztag = g_pdf_reflow ? "重排" : PDF_ZOOM_SHORT[g_pdf_zoom];
            int zw = 44;
            int zx = w - 42 - zw - 4;
            uint32_t zbg = g_pdf_reflow ? th->accent : th->card_bg;
            ui_fill_round_rect(zx, 3, zw, top_bar - 6, 4, zbg);
            if (!g_pdf_reflow) ui_draw_round_rect(zx, 3, zw, top_bar - 6, 4, th->card_border);
            hal_set_text_bg(zbg);
            draw_str(zx + 4, ty, ztag, zw - 6, g_pdf_reflow ? 0xFFFFFF : th->accent);
            right_reserved += zw + 8;
        } else if (g_auto_active) {
            int aw = 46;
            int ax = w - 42 - aw - 4;
            ui_fill_round_rect(ax, 3, aw, top_bar - 6, 4, 0x10B981);
            hal_set_text_bg(0x10B981);
            draw_str(ax + 4, ty, "自動", aw - 6, 0xFFFFFF);
            right_reserved += aw + 8;
        }

        int tx = 48;
        int tw = w - tx - right_reserved;
        if (tw > 24) {
            hal_set_text_bg(th->bar_bg);
            char tbuf[72];
            utf8_ellipsis(g_doc.title[0] ? g_doc.title : get_filename_only(g_filepath), tw / 8, tbuf, sizeof(tbuf));
            draw_str(tx, ty, tbuf, tw, th->text);
        }
        draw_clock_right(th, w, (top_bar - 7) / 2);
    }

    int pct = (g_doc.format == BOOK_FMT_PDF && g_pdf)
              ? (pdf_page_count(g_pdf) > 0 ? ((g_pdf_page + 1) * 100) / pdf_page_count(g_pdf) : 0)
              : (g_page_count > 0 ? ((g_current_page + 1) * 100) / g_page_count : 0);

    if (bot_bar > 0) {
        int by = h - bot_bar;
        hal_fill_rect(0, by, w, bot_bar, th->bar_bg);
        hal_fill_rect(0, by, w, 2, th->track);
        if (pct > 0) hal_fill_rect(0, by, (w * pct) / 100, 2, th->accent);
        if (g_auto_active) {
            int ap = (g_auto_elapsed * 100) / AUTO_SECS[g_auto_sec_idx];
            hal_fill_rect(0, h - 2, (w * ap) / 100, 2, 0x10B981);
        }

        int bty = by + (bot_bar - 16) / 2;
        if (bty < by + 3) bty = by + 3;
        hal_set_text_bg(th->bar_bg);
        char foot[128];
        if (g_toast_msg[0]) {
            draw_str(6, bty, g_toast_msg, w - 12, th->accent);
        } else if (g_doc.format == BOOK_FMT_PDF && g_pdf) {
            if (sm) {
                snprintf(foot, sizeof(foot), "P.%d/%d %d%% Z/X縮放 R重排 M選單",
                         g_pdf_page + 1, pdf_page_count(g_pdf), pct);
            } else {
                snprintf(foot, sizeof(foot), "P.%d/%d (%d%%) · %s · [Z/X]縮放 [0]整頁 [R]重排 [I]全屏 [M]選單",
                         g_pdf_page + 1, pdf_page_count(g_pdf), pct, PDF_ZOOM_NM[g_pdf_zoom]);
            }
            draw_str(6, bty, foot, w - 12, th->text);
        } else if (is_image_fmt(g_doc.format)) {
            draw_str(6, bty, "圖片書 · 左右翻頁 [I]全屏 [M]選單", w - 12, th->text);
        } else if (g_page_count > 0) {
            if (sm) {
                snprintf(foot, sizeof(foot), "%d/%d頁 %d%%  I全屏 M選單 B書架",
                         g_current_page + 1, g_page_count, pct);
            } else {
                snprintf(foot, sizeof(foot), "第 %d/%d 頁 (%d%%) · [Z/X]字大 [L]行距 [I]全屏 [M]選單 [B]書架",
                         g_current_page + 1, g_page_count, pct);
            }
            draw_str(6, bty, foot, w - 12, th->text);
        }
    }

    /* 主閱讀視區 — 原版 PDF 渲染邏輯完全不更動！ */
    if (g_doc.format == BOOK_FMT_PDF && g_pdf) {
        if (!g_pdf_reflow) {
            pdf_render_page(g_pdf, g_pdf_page, 0, vy, w, vh,
                            g_pdf_zoom, g_pdf_pan_x, g_pdf_pan_y,
                            th->is_dark ? 0x0C0F14 : 0xDDD8CE, th->is_dark,
                            &g_pdf_last_pw, &g_pdf_last_ph);
        } else {
            if (g_pdf_reflow_page_cached != g_pdf_page) {
                free(g_pdf_reflow_text);
                g_pdf_reflow_text = pdf_extract_page_text(g_pdf, g_pdf_page, &g_pdf_reflow_len);
                g_pdf_reflow_page_cached = g_pdf_page;
                g_pdf_reflow_scroll = 0;
            }
            int max_u, lines, lh, px;
            get_layout_params(&max_u, &lines, &lh, &px);
            hal_set_body_bounds(vy, vy + vh);
            hal_set_text_bg(th->bg);
            if (!g_pdf_reflow_text || g_pdf_reflow_len == 0) {
                draw_str(12, vy + 20, "此頁為掃描/圖像 PDF，請按 [R] 切回原版視圖", w - 24, th->accent);
            } else {
                size_t pos = 0;
                for (int sk = 0; sk < g_pdf_reflow_scroll && pos < g_pdf_reflow_len; sk++) {
                    size_t dl = 0, step = scan_one_line_buf(g_pdf_reflow_text, g_pdf_reflow_len, pos, max_u, &dl);
                    if (step == 0) break;
                    pos += step;
                }
                int y = vy + (sm ? 4 : 8);
                for (int ln = 0; ln < lines && pos < g_pdf_reflow_len; ln++) {
                    size_t dl = 0, step = scan_one_line_buf(g_pdf_reflow_text, g_pdf_reflow_len, pos, max_u, &dl);
                    if (dl > 0) hal_draw_line_utf8(px, y, g_pdf_reflow_text + pos, (int)dl, w - px * 2, th->text);
                    y += lh;
                    if (step == 0) break;
                    pos += step;
                }
            }
        }
    } else if (is_image_fmt(g_doc.format)) {
        hal_draw_image_file(g_filepath, 0, vy, w, vh);
    } else if (g_doc.text && g_page_offsets && g_page_count > 0) {
        size_t pos = g_page_offsets[g_current_page];
        int img_id = -1;
        if (parse_img_marker(g_doc.text, g_doc.text_len, pos, &img_id, NULL)) {
            int iw = 0, ih = 0;
            const uint8_t *rgb = get_cached_illust(img_id, &iw, &ih);
            int pad = sm ? 6 : 16;
            int ax = pad, ay = vy + pad, aw = w - pad * 2, ah = vh - pad * 2;
            if (rgb && iw > 0 && ih > 0 && aw > 20 && ah > 20) {
                int dw = aw, dh = ah;
                if (iw * ah > aw * ih) dh = (aw * ih) / iw;
                else dw = (ah * iw) / ih;
                int dx = ax + (aw - dw) / 2, dy = ay + (ah - dh) / 2;
                ui_card(dx - 4, dy - 4, dw + 8, dh + 8, 6, th->card_bg, th->card_border, th->shadow, 0, th->accent);
                hal_blit_rgb888_scaled(rgb, iw, ih, dx, dy, dw, dh, dx, dy, dw, dh, 0);
            }
        } else {
            int max_u, lines, lh, px;
            get_layout_params(&max_u, &lines, &lh, &px);
            hal_set_body_bounds(vy, vy + vh);
            hal_set_text_bg(th->bg);
            int y = vy + (sm ? 4 : 8);
            for (int ln = 0; ln < lines && pos < g_doc.text_len; ln++) {
                if (parse_img_marker(g_doc.text, g_doc.text_len, pos, NULL, NULL)) break;
                size_t dl = 0, step = scan_one_line_buf(g_doc.text, g_doc.text_len, pos, max_u, &dl);
                if (dl > 0) hal_draw_line_utf8(px, y, g_doc.text + pos, (int)dl, w - px * 2, th->text);
                y += lh;
                if (step == 0) break;
                pos += step;
            }
        }
    }

    if (g_fullscreen && g_toast_msg[0]) {
        int tw = w - 24;
        if (tw > 260) tw = 260;
        int tx = (w - tw) / 2, ty = h - 28;
        hal_clear_text_in_rect(tx - 2, ty - 2, tw + 4, 24);
        ui_fill_round_rect(tx, ty, tw, 22, 6, th->card_bg);
        ui_draw_round_rect(tx, ty, tw, 22, 6, th->accent);
        hal_set_text_bg(th->card_bg);
        draw_str(tx + 8, ty + 3, g_toast_msg, tw - 16, th->accent);
    }
}

/* =========================================================================
 * 自適應彈窗渲染
 * ========================================================================= */

static void render_read_menu_overlay(const AppTheme *th, int w, int h) {
    int sm = is_small_screen();
    int mw = sm ? (w - 20) : 380;
    if (mw > w - 16) mw = w - 16;
    int mh = sm ? (h - 16) : 360;
    if (mh > h - 16) mh = h - 16;
    int mx = (w - mw) / 2, my = (h - mh) / 2;
    begin_modal(w, h, mx, my, mw, mh);

    ui_card(mx, my, mw, mh, 8, th->card_bg, th->card_border, th->shadow, 1, th->accent);
    int hdr_h = sm ? 24 : 30;
    int ftr_h = 20;
    ui_fill_round_rect(mx + 2, my + 2, mw - 4, hdr_h, 6, th->bar_bg);
    hal_set_text_bg(th->bar_bg);
    draw_str(mx + 10, my + (hdr_h - 16) / 2 + 1, "閱讀選單 (↑↓選 ←→調)", mw - 20, th->text);

    int is_pdf = (g_doc.format == BOOK_FMT_PDF && g_pdf);
    int mcnt = get_read_menu_count();
    char items[12][64];
    if (is_pdf) {
        snprintf(items[0],  64, "繼續閱讀");
        snprintf(items[1],  64, "PDF縮放：< %s >", PDF_ZOOM_NM[g_pdf_zoom]);
        snprintf(items[2],  64, "PDF檢視：%s [R]", g_pdf_reflow ? "文字重排" : "原版頁面");
        snprintf(items[3],  64, "全螢幕模式：%s [I]", g_fullscreen ? "開啟" : "關閉");
        snprintf(items[4],  64, "章節 / 頁碼目錄 [C]");
        snprintf(items[5],  64, "跳至指定頁碼 [G]");
        snprintf(items[6],  64, "書籤管理 (%d) [K]", g_bmark_count);
        snprintf(items[7],  64, "全文搜尋 [F]");
        snprintf(items[8],  64, "主題：< %s >", theme_get(g_theme_idx)->name);
        snprintf(items[9],  64, "自動翻頁：%s [A]", g_auto_active ? "開啟" : "關閉");
        snprintf(items[10], 64, "返回書架 [B]");
    } else {
        snprintf(items[0], 64, "繼續閱讀");
        snprintf(items[1], 64, "字體大小：< %s >", FONT_SIZE_NM[g_font_size_idx]);
        snprintf(items[2], 64, "文字行距：< %s >", SPACING_NM[g_spacing_idx]);
        snprintf(items[3], 64, "全螢幕模式：%s [I]", g_fullscreen ? "開啟" : "關閉");
        snprintf(items[4], 64, "章節目錄 (%d) [C]", g_doc.chapter_count);
        snprintf(items[5], 64, "跳至指定頁 [G]");
        snprintf(items[6], 64, "書籤管理 (%d) [K]", g_bmark_count);
        snprintf(items[7], 64, "全文搜尋 [F]");
        snprintf(items[8], 64, "主題：< %s >", theme_get(g_theme_idx)->name);
        snprintf(items[9], 64, "返回書架 [B]");
    }

    int body_y = my + hdr_h + 4;
    int body_h = mh - hdr_h - ftr_h - 8;
    int row_h = sm ? 22 : 28;
    int vis = body_h / row_h;
    if (vis < 1) vis = 1;
    if (g_read_menu_sel < g_read_menu_scroll) g_read_menu_scroll = g_read_menu_sel;
    if (g_read_menu_sel >= g_read_menu_scroll + vis) g_read_menu_scroll = g_read_menu_sel - vis + 1;

    for (int i = 0; i < vis && (g_read_menu_scroll + i) < mcnt; i++) {
        int idx = g_read_menu_scroll + i;
        int ry = body_y + i * row_h;
        int sel = (idx == g_read_menu_sel);
        uint32_t bg = sel ? th->accent : th->card_bg;
        if (sel) ui_fill_round_rect(mx + 8, ry, mw - 16, row_h - 2, 4, bg);
        hal_set_text_bg(bg);
        draw_str(mx + 14, ry + (row_h - 18) / 2 + 1, items[idx], mw - 28, sel ? 0xFFFFFF : th->text);
    }

    hal_set_text_bg(th->card_bg);
    draw_str(mx + 10, my + mh - ftr_h, "[Enter]確認 [H]按鍵說明 [ESC]關閉", mw - 20, th->muted);
}

static void render_toc_page(const AppTheme *th, int w, int h) {
    int sm = is_small_screen();
    int mw = w - (sm ? 16 : 80);
    int mh = h - (sm ? 16 : 60);
    if (mw > 560) mw = 560;
    int mx = (w - mw) / 2, my = (h - mh) / 2;
    begin_modal(w, h, mx, my, mw, mh);

    ui_card(mx, my, mw, mh, 8, th->card_bg, th->card_border, th->shadow, 1, th->accent);
    int hdr_h = sm ? 24 : 30;
    ui_fill_round_rect(mx + 2, my + 2, mw - 4, hdr_h, 6, th->bar_bg);
    hal_set_text_bg(th->bar_bg);
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "目錄 (%d 項)  [Enter]跳轉", g_doc.chapter_count);
    draw_str(mx + 10, my + (hdr_h - 16) / 2 + 1, hdr, mw - 20, th->text);

    int body_y = my + hdr_h + 4;
    int body_h = mh - hdr_h - 24;
    int row_h = sm ? 22 : 26;
    int vis = body_h / row_h;
    if (vis < 1) vis = 1;

    if (g_doc.chapter_count <= 0) {
        hal_set_text_bg(th->card_bg);
        draw_str(mx + 14, body_y + 12, "此書無目錄，請按 [G] 直接跳頁", mw - 28, th->muted);
    } else {
        if (g_toc_sel < g_toc_scroll) g_toc_scroll = g_toc_sel;
        if (g_toc_sel >= g_toc_scroll + vis) g_toc_scroll = g_toc_sel - vis + 1;
        for (int i = 0; i < vis && (g_toc_scroll + i) < g_doc.chapter_count; i++) {
            int idx = g_toc_scroll + i;
            int ry = body_y + i * row_h;
            int sel = (idx == g_toc_sel);
            uint32_t bg = sel ? th->accent : th->card_bg;
            if (sel) ui_fill_round_rect(mx + 8, ry, mw - 16, row_h - 2, 4, bg);
            hal_set_text_bg(bg);
            char lb[96];
            utf8_ellipsis(g_doc.chapters[idx].title, (mw - 28) / 8, lb, sizeof(lb));
            draw_str(mx + 12, ry + (row_h - 18) / 2 + 1, lb, mw - 24, sel ? 0xFFFFFF : th->text);
        }
    }
    hal_set_text_bg(th->card_bg);
    draw_str(mx + 10, my + mh - 20, "↑↓選章 PgUp/Dn快捲 [ESC]返回", mw - 20, th->muted);
}

static void render_bookmarks_overlay(const AppTheme *th, int w, int h) {
    int sm = is_small_screen();
    int mw = w - (sm ? 16 : 80);
    int mh = h - (sm ? 16 : 60);
    if (mw > 520) mw = 520;
    int mx = (w - mw) / 2, my = (h - mh) / 2;
    begin_modal(w, h, mx, my, mw, mh);

    ui_card(mx, my, mw, mh, 8, th->card_bg, th->card_border, th->shadow, 1, th->accent);
    int hdr_h = sm ? 24 : 30;
    ui_fill_round_rect(mx + 2, my + 2, mw - 4, hdr_h, 6, th->bar_bg);
    hal_set_text_bg(th->bar_bg);
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "書籤 (%d/%d)  [N/A]新增 [D]刪", g_bmark_count, MAX_BOOKMARKS);
    draw_str(mx + 10, my + (hdr_h - 16) / 2 + 1, hdr, mw - 20, th->text);

    int body_y = my + hdr_h + 4;
    int body_h = mh - hdr_h - 24;
    int row_h = sm ? 22 : 26;
    int vis = body_h / row_h;
    if (vis < 1) vis = 1;

    if (g_bmark_count == 0) {
        hal_set_text_bg(th->card_bg);
        draw_str(mx + 14, body_y + 12, "尚無書籤，按 [Enter] 或 [N] 新增目前頁", mw - 28, th->muted);
    } else {
        if (g_bmark_sel < g_bmark_scroll) g_bmark_scroll = g_bmark_sel;
        if (g_bmark_sel >= g_bmark_scroll + vis) g_bmark_scroll = g_bmark_sel - vis + 1;
        for (int i = 0; i < vis && (g_bmark_scroll + i) < g_bmark_count; i++) {
            int idx = g_bmark_scroll + i;
            int ry = body_y + i * row_h;
            int sel = (idx == g_bmark_sel);
            uint32_t bg = sel ? th->accent : th->card_bg;
            if (sel) ui_fill_round_rect(mx + 8, ry, mw - 16, row_h - 2, 4, bg);
            hal_set_text_bg(bg);
            char ln[112];
            snprintf(ln, sizeof(ln), "P.%d(%d%%) %s", g_bmarks[idx].page_num, g_bmarks[idx].pct, g_bmarks[idx].label);
            draw_str(mx + 12, ry + (row_h - 18) / 2 + 1, ln, mw - 24, sel ? 0xFFFFFF : th->text);
        }
    }
    hal_set_text_bg(th->card_bg);
    draw_str(mx + 10, my + mh - 20, "[Enter]跳轉 [N]新增 [D]刪除 [ESC]關", mw - 20, th->muted);
}

static void render_search_overlay(const AppTheme *th, int w, int h) {
    int sm = is_small_screen();
    int mw = w - (sm ? 16 : 80);
    int mh = h - (sm ? 16 : 60);
    if (mw > 540) mw = 540;
    int mx = (w - mw) / 2, my = (h - mh) / 2;
    begin_modal(w, h, mx, my, mw, mh);

    ui_card(mx, my, mw, mh, 8, th->card_bg, th->card_border, th->shadow, 1, th->accent);
    int hdr_h = 24;
    ui_fill_round_rect(mx + 2, my + 2, mw - 4, hdr_h, 6, th->bar_bg);
    hal_set_text_bg(th->bar_bg);
    draw_str(mx + 10, my + 5, "全文搜尋（輸入字詞後按 Enter）", mw - 20, th->text);

    int box_y = my + hdr_h + 6;
    ui_fill_round_rect(mx + 10, box_y, mw - 20, 24, 4, th->bg);
    ui_draw_round_rect(mx + 10, box_y, mw - 20, 24, 4, th->accent);
    hal_set_text_bg(th->bg);
    char qline[80];
    snprintf(qline, sizeof(qline), "%s_", g_search_query);
    draw_str(mx + 16, box_y + 4, qline, mw - 32, th->text);

    int body_y = box_y + 30;
    int body_h = mh - (body_y - my) - 22;
    int row_h = 22;
    int vis = body_h / row_h;
    if (vis < 1) vis = 1;

    hal_set_text_bg(th->card_bg);
    if (!g_search_ran) {
        draw_str(mx + 12, body_y + 6, "打字輸入關鍵字，按 [Enter] 開始搜尋", mw - 24, th->muted);
    } else if (g_search_cnt == 0) {
        draw_str(mx + 12, body_y + 6, "找不到符合的結果", mw - 24, th->accent);
    } else {
        if (g_search_sel < g_search_scroll) g_search_scroll = g_search_sel;
        if (g_search_sel >= g_search_scroll + vis) g_search_scroll = g_search_sel - vis + 1;
        for (int i = 0; i < vis && (g_search_scroll + i) < g_search_cnt; i++) {
            int idx = g_search_scroll + i;
            int ry = body_y + i * row_h;
            int sel = (idx == g_search_sel);
            uint32_t bg = sel ? th->accent : th->card_bg;
            if (sel) ui_fill_round_rect(mx + 8, ry, mw - 16, row_h - 2, 4, bg);
            hal_set_text_bg(bg);
            char ln[120];
            snprintf(ln, sizeof(ln), "P.%d %s", g_search_hits[idx].page_num, g_search_hits[idx].snippet);
            draw_str(mx + 12, ry + 2, ln, mw - 24, sel ? 0xFFFFFF : th->text);
        }
    }
    hal_set_text_bg(th->card_bg);
    draw_str(mx + 10, my + mh - 20, "↑↓選結果 [Enter]搜尋/跳轉 [ESC]返回", mw - 20, th->muted);
}

static void render_jump_overlay(const AppTheme *th, int w, int h) {
    int tot = (g_doc.format == BOOK_FMT_PDF && g_pdf) ? pdf_page_count(g_pdf) : g_page_count;
    int mw = w - 24;
    if (mw > 340) mw = 340;
    int mh = 128;
    int mx = (w - mw) / 2, my = (h - mh) / 2;
    begin_modal(w, h, mx, my, mw, mh);

    ui_card(mx, my, mw, mh, 8, th->card_bg, th->card_border, th->shadow, 1, th->accent);
    hal_set_text_bg(th->card_bg);
    draw_str(mx + 12, my + 10, "快速跳至指定頁碼", mw - 24, th->text);

    char pb[64];
    if (g_jump_digits[0]) snprintf(pb, sizeof(pb), "第 %s_ / %d 頁", g_jump_digits, tot);
    else snprintf(pb, sizeof(pb), "第 %d / %d 頁", g_jump_target + 1, tot);
    draw_str(mx + 12, my + 36, pb, mw - 24, th->accent);

    int pct = tot > 0 ? ((g_jump_target + 1) * 100) / tot : 0;
    ui_progress_bar(mx + 12, my + 64, mw - 24, 6, pct, th->track, th->accent);
    draw_str(mx + 12, my + 80, "可直接打數字鍵，或 ←→±10 ↑↓±50", mw - 24, th->muted);
    draw_str(mx + 12, my + 102, "PgUp/Dn±1  [Enter]跳轉  [ESC]取消", mw - 24, th->muted);
}

static void render_new_folder_overlay(const AppTheme *th, int w, int h) {
    int mw = w - 24;
    if (mw > 340) mw = 340;
    int mh = 112;
    int mx = (w - mw) / 2, my = (h - mh) / 2;
    begin_modal(w, h, mx, my, mw, mh);

    ui_card(mx, my, mw, mh, 8, th->card_bg, th->card_border, th->shadow, 1, th->accent);
    hal_set_text_bg(th->card_bg);
    draw_str(mx + 12, my + 10, "新增資料夾名稱", mw - 24, th->text);
    ui_fill_round_rect(mx + 12, my + 36, mw - 24, 26, 4, th->bg);
    ui_draw_round_rect(mx + 12, my + 36, mw - 24, 26, 4, th->accent);
    hal_set_text_bg(th->bg);
    char b[64];
    snprintf(b, sizeof(b), "%s_", g_new_folder_buf);
    draw_str(mx + 18, my + 41, b, mw - 36, th->text);
    hal_set_text_bg(th->card_bg);
    draw_str(mx + 12, my + 78, "[Enter]建立  [Back]退格  [ESC]取消", mw - 24, th->muted);
}

static void render_move_folder_overlay(const AppTheme *th, int w, int h) {
    int sm = is_small_screen();
    int mw = w - (sm ? 20 : 80);
    if (mw > 380) mw = 380;
    int mh = h - (sm ? 24 : 80);
    if (mh > 300) mh = 300;
    int mx = (w - mw) / 2, my = (h - mh) / 2;
    begin_modal(w, h, mx, my, mw, mh);

    ui_card(mx, my, mw, mh, 8, th->card_bg, th->card_border, th->shadow, 1, th->accent);
    ui_fill_round_rect(mx + 2, my + 2, mw - 4, 24, 6, th->bar_bg);
    hal_set_text_bg(th->bar_bg);
    draw_str(mx + 10, my + 5, "移動書籍至資料夾", mw - 20, th->text);

    int tot = g_folder_count + 1;
    int body_y = my + 30;
    int body_h = mh - 52;
    int row_h = 22;
    int vis = body_h / row_h;
    if (vis < 1) vis = 1;
    if (g_move_folder_sel < g_move_folder_scroll) g_move_folder_scroll = g_move_folder_sel;
    if (g_move_folder_sel >= g_move_folder_scroll + vis) g_move_folder_scroll = g_move_folder_sel - vis + 1;

    for (int i = 0; i < vis && (g_move_folder_scroll + i) < tot; i++) {
        int idx = g_move_folder_scroll + i;
        int ry = body_y + i * row_h;
        int sel = (idx == g_move_folder_sel);
        uint32_t bg = sel ? th->accent : th->card_bg;
        if (sel) ui_fill_round_rect(mx + 8, ry, mw - 16, row_h - 2, 4, bg);
        hal_set_text_bg(bg);
        draw_str(mx + 12, ry + 2, (idx == 0) ? "[根目錄 / 未分類]" : g_folders[idx - 1],
                 mw - 24, sel ? 0xFFFFFF : th->text);
    }
    hal_set_text_bg(th->card_bg);
    draw_str(mx + 10, my + mh - 20, "↑↓選擇 [Enter]移入 [ESC]取消", mw - 20, th->muted);
}

static void render_confirm_modal(const AppTheme *th, int w, int h,
                                 const char *title, const char *line1, const char *line2) {
    int mw = w - 24;
    if (mw > 340) mw = 340;
    int mh = 104;
    int mx = (w - mw) / 2, my = (h - mh) / 2;
    begin_modal(w, h, mx, my, mw, mh);

    ui_card(mx, my, mw, mh, 8, th->card_bg, th->card_border, th->shadow, 1, th->accent);
    hal_set_text_bg(th->card_bg);
    draw_str(mx + 12, my + 12, title, mw - 24, th->accent);
    if (line1) draw_str(mx + 12, my + 40, line1, mw - 24, th->text);
    if (line2) draw_str(mx + 12, my + 72, line2, mw - 24, th->muted);
}

static void render_help_overlay(const AppTheme *th, int w, int h) {
    int sm = is_small_screen();
    int mw = w - (sm ? 14 : 60);
    int mh = h - (sm ? 14 : 50);
    if (mw > 480) mw = 480;
    if (mh > 320) mh = 320;
    int mx = (w - mw) / 2, my = (h - mh) / 2;
    begin_modal(w, h, mx, my, mw, mh);

    ui_card(mx, my, mw, mh, 8, th->card_bg, th->card_border, th->shadow, 1, th->accent);
    ui_fill_round_rect(mx + 2, my + 2, mw - 4, 24, 6, th->bar_bg);
    hal_set_text_bg(th->bar_bg);
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "純按鍵速查 (%d/3)  ←→換頁", g_help_page + 1);
    draw_str(mx + 10, my + 5, hdr, mw - 20, th->text);

    hal_set_text_bg(th->card_bg);
    int y = my + 32;
    int lh = sm ? 20 : 24;
    if (g_help_page == 0) {
        draw_str(mx + 10, y,        "【PDF 閱讀按鍵】", mw - 20, th->accent);
        draw_str(mx + 10, y + lh*1, "Z / + / = : 放大 (5段縮放)", mw - 20, th->text);
        draw_str(mx + 10, y + lh*2, "X / -     : 縮小   0 : 重置整頁", mw - 20, th->text);
        draw_str(mx + 10, y + lh*3, "方向鍵    : 放大平移 / 邊界翻頁", mw - 20, th->text);
        draw_str(mx + 10, y + lh*4, "PgUp/Dn/空白: 直接上下翻頁", mw - 20, th->text);
        draw_str(mx + 10, y + lh*5, "R: 重排檢視  I: 全螢幕模式", mw - 20, th->text);
        draw_str(mx + 10, y + lh*6, "M / ESC   : 開啟閱讀選單", mw - 20, th->text);
    } else if (g_help_page == 1) {
        draw_str(mx + 10, y,        "【書架與文字書按鍵】", mw - 20, th->accent);
        draw_str(mx + 10, y + lh*1, "O: 匯入檔案  F: 篩選/全文搜尋", mw - 20, th->text);
        draw_str(mx + 10, y + lh*2, "N: 建資料夾  V: 移動書  D: 移除", mw - 20, th->text);
        draw_str(mx + 10, y + lh*3, "C: 目錄  G: 跳頁(可打數字)", mw - 20, th->text);
        draw_str(mx + 10, y + lh*4, "K: 書籤  L: 行距  T: 換主題", mw - 20, th->text);
        draw_str(mx + 10, y + lh*5, "A: 自動翻頁  B/Back: 回書架", mw - 20, th->text);
        draw_str(mx + 10, y + lh*6, "1/2/Tab: 切換書架與設定分頁", mw - 20, th->text);
    } else {
        char info1[80], info2[80], info3[80];
        snprintf(info1, sizeof(info1), "實機螢幕：%d x %d (%s)",
                 hal_get_width(), hal_get_height(), hal_is_xrgb() ? "XRGB8888" : "RGB565");
        snprintf(info2, sizeof(info2), "觸控輸入：已強制停用（純鍵盤）");
        snprintf(info3, sizeof(info3), "上次按鍵碼：%d   藏書：%d 本",
                 hal_get_last_keycode(), g_book_count);
        draw_str(mx + 10, y,        "【CD-358 裝置與診斷資訊】", mw - 20, th->accent);
        draw_str(mx + 10, y + lh*1, info1, mw - 20, th->text);
        draw_str(mx + 10, y + lh*2, info2, mw - 20, th->text);
        draw_str(mx + 10, y + lh*3, info3, mw - 20, th->text);
        draw_str(mx + 10, y + lh*4, "書庫索引：C:\\cd358_books.idx", mw - 20, th->muted);
        draw_str(mx + 10, y + lh*5, "提示：按 I 隱藏上下欄看PDF最爽", mw - 20, th->muted);
    }
    draw_str(mx + 10, my + mh - 20, "←→切頁 [Enter/ESC]關閉", mw - 20, th->muted);
}

void reader_render(void) {
    const AppTheme *th = theme_get(g_theme_idx);
    int w = hal_get_width(), h = hal_get_height();

    if (g_screen == SCR_HOME || g_screen == SCR_NEW_FOLDER || g_screen == SCR_MOVE_FOLDER ||
        g_screen == SCR_DELETE_CONFIRM || g_screen == SCR_QUIT_CONFIRM ||
        (g_screen == SCR_HELP && g_return_screen == SCR_HOME)) {
        if (g_home_tab == TAB_SETTINGS) render_home_settings(th, w, h);
        else render_home_shelf(th, w, h);

        if (g_screen == SCR_NEW_FOLDER) render_new_folder_overlay(th, w, h);
        else if (g_screen == SCR_MOVE_FOLDER) render_move_folder_overlay(th, w, h);
        else if (g_screen == SCR_DELETE_CONFIRM) {
            char line1[80];
            if (g_del_is_folder && g_del_target_idx >= 0 && g_del_target_idx < g_folder_count) {
                snprintf(line1, sizeof(line1), "刪除資料夾「%s」？", g_folders[g_del_target_idx]);
            } else if (!g_del_is_folder && g_del_target_idx >= 0 && g_del_target_idx < g_book_count) {
                char tb[48];
                utf8_ellipsis(g_books[g_del_target_idx].title, 20, tb, sizeof(tb));
                snprintf(line1, sizeof(line1), "移出書架「%s」？", tb);
            } else {
                snprintf(line1, sizeof(line1), "確定移除所選項目？");
            }
            render_confirm_modal(th, w, h, "確認移除", line1, "[Enter]確定移除   [ESC]取消");
        } else if (g_screen == SCR_QUIT_CONFIRM) {
            render_confirm_modal(th, w, h, "離開閱讀器", "確定要結束並返回系統嗎？", "[Enter]確定離開   [ESC]取消");
        } else if (g_screen == SCR_HELP) {
            render_help_overlay(th, w, h);
        }
        hal_present();
        return;
    }

    render_reading_base(th, w, h);
    if (g_screen == SCR_READ_MENU) render_read_menu_overlay(th, w, h);
    else if (g_screen == SCR_TOC) render_toc_page(th, w, h);
    else if (g_screen == SCR_BOOKMARKS) render_bookmarks_overlay(th, w, h);
    else if (g_screen == SCR_SEARCH) render_search_overlay(th, w, h);
    else if (g_screen == SCR_JUMP) render_jump_overlay(th, w, h);
    else if (g_screen == SCR_NEXT_VOL) {
        char tb[64] = {0};
        if (g_next_vol_idx >= 0 && g_next_vol_idx < g_book_count) {
            utf8_ellipsis(g_books[g_next_vol_idx].title, 22, tb, sizeof(tb));
        }
        render_confirm_modal(th, w, h, "本卷閱讀完成！", tb, "[Enter]續讀下一卷   [ESC]停留");
    } else if (g_screen == SCR_HELP) {
        render_help_overlay(th, w, h);
    }
    hal_present();
}
