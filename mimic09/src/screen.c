/* screen.c — screen editor for a real terminal.
 *
 * Speaks the controls shared by macOS Terminal, xterm, a VT100, the Linux
 * console, and a VK100 GIGI in text mode. No ncurses and no terminfo.
 *
 * Output is VT100 (ANSI) unless the terminal answers the VT52 identify, or
 * TERM says vt52. A VK100 leaves the factory in ANSI (TM1). VT52 is TM0.
 * Cursor keys are accepted in all three forms at once:
 *   ANSI normal (CK0)     ESC [ A/B/C/D
 *   ANSI application      ESC O A/B/C/D
 *   VT52                  ESC A/B/C/D
 * ANSI cursor address is ESC [ row ; col H. VT52 address is ESC Y, with
 * each coordinate stored as 31 plus the 1-based position (octal 37 plus
 * the position: row 1 is 0x20). Erase-to-end-of-line is ESC [ K or ESC K.
 *
 * ESC P is the VK100 device-control introducer (ReGIS and SET-UP strings).
 * This file never writes ESC P, a graphics prefix, or the alternate screen.
 * Non-printable output is shown as a dot so a memory byte cannot enter ReGIS.
 *
 * The editor keeps only the lines it has drawn. A hardware VT100 drops a
 * line that scrolls off the top; so does this buffer. Hex digits on a '>'
 * dump line write that byte immediately. Hex digits on the ';' register
 * line write that register immediately. Return on a '.' line reassembles
 * the mnemonic with the monitor assembler. The ASCII column is display only.
 * There is no command prompt character. The cursor waits on a blank line.
 */

#include "screen.h"
#include "asm.h"
#include "cpu.h"
#include "debug.h"
#include "disasm.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

enum {
    LINE_TEXT = 0,
    LINE_MEM,
    LINE_DIS,
    LINE_CMD,
    LINE_FIELD,
    LINE_REG
};

enum {
    CK_TEXT = 0,
    CK_HEX,
    CK_MNEM,
    CK_CMD,
    CK_REG
};

enum {
    MODE_CMD = 0,
    MODE_FIELD
};

enum {
    K_NONE = 0,
    K_UP,
    K_DOWN,
    K_LEFT,
    K_RIGHT,
    K_ENTER,
    K_BS,
    K_EOF,
    K_CHAR
};

typedef struct {
    char ch;
    unsigned char kind;
    unsigned char nib;
    uint16_t addr;
} cell_t;

typedef struct {
    cell_t *c;
    int n;
    int kind;
    int cmd_at;
    int mnem_at;
    uint16_t addr;
} scr_row;

static scr_row *rows;
static int row_slots;
static int nrows;
static int row_open;
static int term_rows = 24;
static int term_cols = 80;
static int phys_rows = 24;
static int phys_cols = 80;
static int origin = 1;          /* terminal row of buffer row 0 */
static int cur_r;
static int cur_c;
static int pref_c;
static int vt52;
static int opened;
static int active;
static int suspended;
static int raw_on;
static int have_cooked;
static int region_set;
static int autowrap_off;
static int paste_on;
static struct termios cooked;
static struct sigaction old_winch;
static struct sigaction old_cont;
static int have_winch;
static int have_cont;

static volatile sig_atomic_t winch_flag;
static volatile sig_atomic_t cont_flag;

static int ungot[128];
static int nungot;

static void term_write(const char *s, size_t n)
{
    while (n) {
        ssize_t w = write(STDOUT_FILENO, s, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        s += w;
        n -= (size_t)w;
    }
}

static int term_has(const char *term, const char *needle)
{
    size_t nlen;
    if (!term || !needle) return 0;
    nlen = strlen(needle);
    for (; *term; term++) {
        size_t i = 0;
        while (i < nlen && term[i] &&
               tolower((unsigned char)term[i]) == (unsigned char)needle[i])
            i++;
        if (i == nlen) return 1;
    }
    return 0;
}

static int usable_cols(void)
{
    return term_cols > 1 ? term_cols - 1 : 1;
}

static void free_rows(void)
{
    int i;
    if (!rows) return;
    for (i = 0; i < row_slots; i++) free(rows[i].c);
    free(rows);
    rows = NULL;
    row_slots = 0;
    nrows = 0;
    row_open = 0;
}

static void on_winch(int sig)
{
    (void)sig;
    winch_flag = 1;
}

static void on_cont(int sig)
{
    (void)sig;
    cont_flag = 1;
}

static void install_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_winch;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGWINCH, &sa, &old_winch) == 0) have_winch = 1;
    sa.sa_handler = on_cont;
    if (sigaction(SIGCONT, &sa, &old_cont) == 0) have_cont = 1;
}

static void restore_signals(void)
{
    if (have_winch) sigaction(SIGWINCH, &old_winch, NULL);
    if (have_cont) sigaction(SIGCONT, &old_cont, NULL);
    have_winch = 0;
    have_cont = 0;
}

static int enter_raw(void)
{
    struct termios t;
    if (!have_cooked) {
        if (tcgetattr(STDIN_FILENO, &cooked) != 0) return 0;
        have_cooked = 1;
    }
    t = cooked;
    cfmakeraw(&t);
    /* ISIG keeps Ctrl-C as SIGINT. IXON lets a GIGI XOFF a long dump.
     * OPOST|ONLCR so a newline is CR+LF on a VT100 and a VK100. */
    t.c_lflag |= ISIG;
    t.c_iflag |= IXON;
    t.c_oflag |= OPOST | ONLCR;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) != 0) return 0;
    raw_on = 1;
    return 1;
}

static void leave_raw(void)
{
    if (!raw_on || !have_cooked) {
        raw_on = 0;
        return;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &cooked);
    raw_on = 0;
}

static void measure_term(void)
{
    struct winsize ws;
    const char *term;
    const char *env;
    int nr = 24;
    int nc = 80;
    int got = 0;

    memset(&ws, 0, sizeof ws);
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 2 && ws.ws_row >= 2)
        got = 1;
    if (!got) {
        memset(&ws, 0, sizeof ws);
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 2 && ws.ws_row >= 2)
            got = 1;
    }
    if (got) {
        nr = ws.ws_row;
        nc = ws.ws_col;
    } else {
        term = getenv("TERM");
        /* A serial VK100 does not answer TIOCGWINSZ. HM0 is 84 columns
         * and VM0 is 24 lines. Anything else stays 80 by 24. */
        if (term_has(term, "gigi") || term_has(term, "vk100")) nc = 84;
        env = getenv("LINES");
        if (env && atoi(env) >= 2) nr = atoi(env);
        env = getenv("COLUMNS");
        if (env && atoi(env) >= 2) nc = atoi(env);
    }
    phys_rows = nr;
    phys_cols = nc;
    if (nr > 200) nr = 200;
    if (nc > 256) nc = 256;
    if (nr < 2) nr = 2;
    if (nc < 2) nc = 2;
    term_rows = nr;
    term_cols = nc;
}

static int read_byte(int timeout_ms)
{
    unsigned char c;
    struct pollfd p;
    if (nungot) return ungot[--nungot];
    p.fd = STDIN_FILENO;
    p.events = POLLIN;
    for (;;) {
        int rc;
        if (winch_flag || cont_flag) return -3;
        rc = poll(&p, 1, timeout_ms);
        if (rc < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (rc == 0) return -1;
        rc = (int)read(STDIN_FILENO, &c, 1);
        if (rc == 1) return c;
        if (rc == 0) return -2;
        if (errno == EINTR) continue;
        if (errno == EAGAIN) continue;
        return -1;
    }
}

static void unread_byte(int c)
{
    if (c < 0) return;
    if (nungot < (int)(sizeof ungot / sizeof ungot[0])) ungot[nungot++] = c;
}

static int guess_vt52(void)
{
    const char *term = getenv("TERM");
    if (!term) return 0;
    if (strncmp(term, "vt52", 4) == 0 || strncmp(term, "VT52", 4) == 0) return 1;
    return 0;
}

/* ESC Z is identify on a VT100 and a VT52. It is not ESC P. */
static int probe_dialect(void)
{
    unsigned char buf[64];
    int n = 0;
    int c;
    int i;
    int guess = guess_vt52();
    term_write("\033Z", 2);
    c = read_byte(400);
    if (c < 0) return guess;
    if (c != 0x1b) {
        unread_byte(c);
        return guess;
    }
    buf[n++] = (unsigned char)c;
    for (;;) {
        int d = read_byte(150);
        if (d < 0) break;
        if (n < (int)sizeof buf) buf[n++] = (unsigned char)d;
        if (d == 'c' || d == 'Z' || d == 'R') break;
    }
    for (i = 0; i + 2 < n; i++) {
        if (buf[i] == 0x1b && buf[i + 1] == '/' && buf[i + 2] == 'Z') return 1;
    }
    for (i = 0; i + 1 < n; i++) {
        if (buf[i] == 0x1b && buf[i + 1] == '[') return 0;
    }
    return guess;
}

static void cup(int row, int col)
{
    char b[32];
    int n;
    if (row < 1) row = 1;
    if (col < 1) col = 1;
    if (row > term_rows) row = term_rows;
    if (col > term_cols) col = term_cols;
    if (vt52)
        n = sprintf(b, "\033Y%c%c", (char)(31 + row), (char)(31 + col));
    else
        n = sprintf(b, "\033[%d;%dH", row, col);
    term_write(b, (size_t)n);
}

static void erase_eol(void)
{
    if (vt52) term_write("\033K", 2);
    else term_write("\033[K", 3);
}

static void home_clear(void)
{
    if (vt52) term_write("\033H\033J", 4);
    else term_write("\033[H\033[J", 6);
}

static void apply_modes(void)
{
    char b[40];
    int n;
    region_set = 0;
    autowrap_off = 0;
    if (vt52) return;
    if (phys_rows > term_rows) {
        n = sprintf(b, "\033[1;%dr", term_rows);
        term_write(b, (size_t)n);
        region_set = 1;
    }
    term_write("\033[?7l", 5);
    autowrap_off = 1;
}

static void restore_modes(void)
{
    if (region_set) term_write("\033[r", 3);
    if (autowrap_off) term_write("\033[?7h", 5);
    region_set = 0;
    autowrap_off = 0;
}

static void tag_reg_line(scr_row *row);
static void set_row_text(int r, const char *text);

static int hex4(const scr_row *row, int at, unsigned *out)
{
    unsigned v = 0;
    int i;
    if (at < 0 || at + 4 > row->n) return 0;
    for (i = 0; i < 4; i++) {
        int d = hexval(row->c[at + i].ch);
        if (d < 0) return 0;
        v = (v << 4) | (unsigned)d;
    }
    *out = v;
    return 1;
}

static void classify_row(scr_row *row)
{
    unsigned addr = 0;
    int i;
    int nb;
    if (row->kind == LINE_CMD || row->kind == LINE_FIELD) return;
    row->kind = LINE_TEXT;
    row->mnem_at = -1;
    row->cmd_at = 0;
    row->addr = 0;
    for (i = 0; i < row->n; i++) {
        row->c[i].kind = CK_TEXT;
        row->c[i].addr = 0;
        row->c[i].nib = 0;
    }
    if (row->n >= 6 && row->c[0].ch == '.' && row->c[1].ch == ' ' && hex4(row, 2, &addr)) {
        i = 6;
        nb = 0;
        while (i + 2 < row->n && row->c[i].ch == ' ' &&
               hexval(row->c[i + 1].ch) >= 0 && hexval(row->c[i + 2].ch) >= 0) {
            i += 3;
            nb++;
        }
        if (nb >= 1 && i < row->n && row->c[i].ch == ' ') {
            while (i < row->n && row->c[i].ch == ' ') i++;
            if (i < row->n) {
                row->kind = LINE_DIS;
                row->addr = (uint16_t)addr;
                row->mnem_at = i;
                for (; i < row->n; i++) row->c[i].kind = CK_MNEM;
                return;
            }
        }
    }
    if (row->n >= 7 && row->c[0].ch == '>' && hex4(row, 1, &addr)) {
        i = 5;
        nb = 0;
        while (i + 2 < row->n && row->c[i].ch == ' ' &&
               hexval(row->c[i + 1].ch) >= 0 && hexval(row->c[i + 2].ch) >= 0) {
            row->c[i + 1].kind = CK_HEX;
            row->c[i + 1].addr = (uint16_t)(addr + (unsigned)nb);
            row->c[i + 1].nib = 0;
            row->c[i + 2].kind = CK_HEX;
            row->c[i + 2].addr = (uint16_t)(addr + (unsigned)nb);
            row->c[i + 2].nib = 1;
            i += 3;
            nb++;
        }
        if (nb >= 1) {
            row->kind = LINE_MEM;
            row->addr = (uint16_t)addr;
        }
    }
    if (row->kind == LINE_TEXT) tag_reg_line(row);
}

static void tag_reg_line(scr_row *row)
{
    char text[128];
    struct mon_reg_cell cells[80];
    int i;
    int n;
    int k;
    if (row->n <= 0 || row->n >= (int)sizeof text) return;
    for (k = 0; k < row->n; k++) text[k] = row->c[k].ch;
    if (!monitor_line_is_regs(text, row->n)) return;
    n = monitor_reg_cells(cells, (int)(sizeof cells / sizeof cells[0]));
    row->kind = LINE_REG;
    for (i = 0; i < n; i++) {
        int col = cells[i].col;
        if (col < 0 || col >= row->n) continue;
        row->c[col].kind = CK_REG;
        row->c[col].addr = (uint16_t)cells[i].id;
        row->c[col].nib = (unsigned char)cells[i].nib;
    }
}

static void drop_top(void)
{
    scr_row first;
    if (nrows <= 0 || !rows) return;
    first = rows[0];
    if (nrows > 1) memmove(rows, rows + 1, (size_t)(nrows - 1) * sizeof *rows);
    rows[nrows - 1] = first;
    rows[nrows - 1].n = 0;
    rows[nrows - 1].kind = LINE_TEXT;
    rows[nrows - 1].cmd_at = 0;
    rows[nrows - 1].mnem_at = -1;
    nrows--;
    if (cur_r > 0) cur_r--;
}

static void term_newline(void)
{
    term_write("\n", 1);
    if (nrows <= 0) return;
    if (origin + nrows - 1 >= term_rows) {
        origin--;
        if (origin < 1) {
            drop_top();
            origin = 1;
        }
    }
}

static void begin_row(int kind)
{
    scr_row *row;
    if (nrows >= term_rows) drop_top();
    row = &rows[nrows];
    row->n = 0;
    row->kind = kind;
    row->cmd_at = 0;
    row->mnem_at = -1;
    row->addr = 0;
    nrows++;
    row_open = 1;
    cur_r = nrows - 1;
    cur_c = 0;
}

static void close_printed_line(void)
{
    int r;
    int col;
    if (!row_open) begin_row(LINE_TEXT);
    r = nrows - 1;
    classify_row(&rows[r]);
    row_open = 0;
    col = rows[r].n + 1;
    if (col < 1) col = 1;
    cup(origin + r, col);
    term_newline();
}

static void place_cursor(void)
{
    int col;
    if (nrows <= 0) {
        cup(origin, 1);
        return;
    }
    col = cur_c + 1;
    if (col < 1) col = 1;
    cup(origin + cur_r, col);
}

static void write_span(int r, int col)
{
    scr_row *row = &rows[r];
    int i;
    cup(origin + r, col + 1);
    for (i = col; i < row->n; i++) {
        char ch = row->c[i].ch;
        if (ch < 32 || ch > 126) ch = '.';
        term_write(&ch, 1);
    }
    erase_eol();
}

static void draw_cell(int r, int c)
{
    char ch;
    if (r < 0 || r >= nrows) return;
    if (c < 0 || c >= rows[r].n) return;
    ch = rows[r].c[c].ch;
    if (ch < 32 || ch > 126) ch = '.';
    cup(origin + r, c + 1);
    term_write(&ch, 1);
}

static void redraw_all(void)
{
    int i;
    home_clear();
    for (i = 0; i < nrows; i++) {
        int tr = origin + i;
        if (tr < 1 || tr > term_rows) continue;
        if (rows[i].n > 0) write_span(i, 0);
        else {
            cup(tr, 1);
            erase_eol();
        }
    }
    place_cursor();
}

static void clamp_cursor(void)
{
    scr_row *row;
    int minc = 0;
    int maxc;
    if (nrows <= 0) {
        cur_r = 0;
        cur_c = 0;
        return;
    }
    if (cur_r < 0) cur_r = 0;
    if (cur_r >= nrows) cur_r = nrows - 1;
    row = &rows[cur_r];
    maxc = row->n;
    if (row->kind == LINE_CMD || row->kind == LINE_FIELD) minc = row->cmd_at;
    if (row->kind == LINE_MEM || row->kind == LINE_TEXT || row->kind == LINE_REG)
        maxc = row->n > 0 ? row->n - 1 : 0;
    if (cur_c < minc) cur_c = minc;
    if (cur_c > maxc) cur_c = maxc;
    if (cur_c < 0) cur_c = 0;
}

static void move_horiz(int d)
{
    if (nrows <= 0) return;
    cur_c += d;
    clamp_cursor();
    pref_c = cur_c;
    place_cursor();
}

static void move_vert(int d)
{
    int nr;
    if (nrows <= 0) return;
    nr = cur_r + d;
    if (nr < 0 || nr >= nrows) return;
    cur_r = nr;
    cur_c = pref_c;
    clamp_cursor();
    place_cursor();
}

static int ascii_col(const scr_row *row)
{
    int i;
    for (i = 0; i + 1 < row->n; i++) {
        if (row->c[i].ch == ':' && row->c[i + 1].ch == ' ') return i + 2;
    }
    return -1;
}

static void refresh_one_ascii(scr_row *row, int r, uint16_t addr)
{
    int ac = ascii_col(row);
    int bi;
    int dest;
    uint8_t b;
    if (ac < 0) return;
    bi = (int)addr - (int)row->addr;
    dest = ac + bi;
    if (dest < 0 || dest >= row->n) return;
    b = mem_read8(addr);
    row->c[dest].ch = (b >= 32 && b < 127) ? (char)b : '.';
    row->c[dest].kind = CK_TEXT;
    draw_cell(r, dest);
}

static void refresh_mem_ascii(scr_row *row)
{
    int ac = ascii_col(row);
    int i;
    if (ac < 0) return;
    for (i = 0; i < row->n; i++) {
        int bi;
        int dest;
        uint8_t b;
        if (row->c[i].kind != CK_HEX || row->c[i].nib != 0) continue;
        bi = (int)row->c[i].addr - (int)row->addr;
        dest = ac + bi;
        if (dest < 0 || dest >= row->n) continue;
        b = mem_read8(row->c[i].addr);
        row->c[dest].ch = (b >= 32 && b < 127) ? (char)b : '.';
    }
}

static void insert_char(scr_row *row, int at, char ch, int kind)
{
    if (row->n >= usable_cols()) return;
    if (at < 0) at = 0;
    if (at > row->n) at = row->n;
    memmove(&row->c[at + 1], &row->c[at], (size_t)(row->n - at) * sizeof(cell_t));
    row->c[at].ch = ch;
    row->c[at].kind = (unsigned char)kind;
    row->c[at].addr = 0;
    row->c[at].nib = 0;
    row->n++;
    cur_c = at + 1;
    pref_c = cur_c;
    write_span(cur_r, at);
    place_cursor();
}

static void refresh_reg_row(int r)
{
    char vals[96];
    monitor_format_regs(NULL, 0, vals, sizeof vals);
    set_row_text(r, vals);
    write_span(r, 0);
}

static void type_reg(scr_row *row, int ch)
{
    cell_t *cell;
    int d;
    int prev;
    int next;
    if (cur_c < 0 || cur_c >= row->n) return;
    cell = &row->c[cur_c];
    if (cell->kind != CK_REG) return;
    d = hexval((char)ch);
    if (d < 0) return;
    prev = cur_c;
    monitor_poke_reg((int)cell->addr, (int)cell->nib, d);
    refresh_reg_row(cur_r);
    row = &rows[cur_r];
    next = prev + 1;
    while (next < row->n && row->c[next].kind != CK_REG) next++;
    if (next < row->n) cur_c = next;
    else if (prev < row->n) cur_c = prev;
    pref_c = cur_c;
    clamp_cursor();
    place_cursor();
}

static void type_hex(scr_row *row, int ch)
{
    cell_t *cell;
    int d;
    uint8_t b;
    int next;
    if (cur_c < 0 || cur_c >= row->n) return;
    cell = &row->c[cur_c];
    if (cell->kind != CK_HEX) return;
    d = hexval((char)ch);
    if (d < 0) return;
    b = mem_read8(cell->addr);
    if (cell->nib == 0) b = (uint8_t)((d << 4) | (b & 0x0F));
    else b = (uint8_t)((b & 0xF0) | d);
    mem_write8(cell->addr, b);
    cell->ch = "0123456789ABCDEF"[d];
    draw_cell(cur_r, cur_c);
    refresh_one_ascii(row, cur_r, cell->addr);
    next = cur_c + 1;
    while (next < row->n && row->c[next].kind != CK_HEX) next++;
    if (next < row->n) cur_c = next;
    else if (cur_c + 1 < row->n) cur_c++;
    pref_c = cur_c;
    clamp_cursor();
    place_cursor();
}

static void type_mnem(scr_row *row, char ch)
{
    if (row->mnem_at < 0 || cur_c < row->mnem_at) return;
    if (cur_c < row->n) {
        row->c[cur_c].ch = ch;
        row->c[cur_c].kind = CK_MNEM;
        draw_cell(cur_r, cur_c);
        cur_c++;
    } else if (row->n < usable_cols()) {
        row->c[row->n].ch = ch;
        row->c[row->n].kind = CK_MNEM;
        row->c[row->n].addr = 0;
        row->c[row->n].nib = 0;
        row->n++;
        draw_cell(cur_r, cur_c);
        cur_c++;
    }
    pref_c = cur_c;
    clamp_cursor();
    place_cursor();
}

static void do_char(int ch)
{
    scr_row *row;
    if (nrows <= 0) return;
    if (ch < 32 || ch > 126) return;
    row = &rows[cur_r];
    if (cur_c >= 0 && cur_c < row->n && row->c[cur_c].kind == CK_HEX) {
        type_hex(row, ch);
        return;
    }
    if (cur_c >= 0 && cur_c < row->n && row->c[cur_c].kind == CK_REG) {
        type_reg(row, ch);
        return;
    }
    if ((row->kind == LINE_CMD || row->kind == LINE_FIELD) && cur_c >= row->cmd_at) {
        insert_char(row, cur_c, (char)ch, row->kind == LINE_CMD ? CK_CMD : CK_TEXT);
        return;
    }
    if (row->kind == LINE_DIS) type_mnem(row, (char)ch);
}

static void do_bs(void)
{
    scr_row *row;
    int minc = 0;
    int at;
    if (nrows <= 0) return;
    row = &rows[cur_r];
    if (cur_c >= 0 && cur_c < row->n &&
        (row->c[cur_c].kind == CK_HEX || row->c[cur_c].kind == CK_REG)) {
        move_horiz(-1);
        return;
    }
    if (row->kind == LINE_CMD || row->kind == LINE_FIELD) minc = row->cmd_at;
    else if (row->kind == LINE_DIS) minc = row->mnem_at >= 0 ? row->mnem_at : row->n;
    else {
        move_horiz(-1);
        return;
    }
    if (cur_c <= minc) return;
    at = cur_c - 1;
    memmove(&row->c[at], &row->c[at + 1], (size_t)(row->n - at - 1) * sizeof(cell_t));
    row->n--;
    cur_c = at;
    pref_c = cur_c;
    write_span(cur_r, at);
    place_cursor();
}

static void set_row_text(int r, const char *text)
{
    scr_row *row = &rows[r];
    int max = usable_cols();
    int kind = row->kind;
    row->n = 0;
    row->kind = LINE_TEXT;
    row->mnem_at = -1;
    while (text[row->n] && row->n < max && text[row->n] != '\n' && text[row->n] != '\r') {
        char ch = text[row->n];
        if (ch < 32 || ch > 126) ch = '.';
        row->c[row->n].ch = ch;
        row->c[row->n].kind = CK_TEXT;
        row->c[row->n].addr = 0;
        row->c[row->n].nib = 0;
        row->n++;
    }
    (void)kind;
    classify_row(row);
}

static void commit_disasm(int r)
{
    scr_row *row = &rows[r];
    char text[96];
    char shown[160];
    char err[80];
    char mnem[80];
    char ops[96];
    uint16_t next = 0;
    int k = 0;
    int i;
    int n;
    if (row->mnem_at < 0) return;
    for (i = row->mnem_at; i < row->n && k + 1 < (int)sizeof text; i++)
        text[k++] = row->c[i].ch;
    while (k > 0 && text[k - 1] == ' ') k--;
    text[k] = 0;
    (void)asm_instr(row->addr, text, err, sizeof err);
    n = disasm(row->addr, mnem, sizeof mnem, &next);
    if (n < 1) n = 1;
    disasm_opfield(ops, sizeof ops, row->addr, n, mnem);
    snprintf(shown, sizeof shown, ". %04X%s", row->addr, ops);
    set_row_text(r, shown);
    write_span(r, 0);
    cur_r = r;
    cur_c = rows[r].mnem_at >= 0 ? rows[r].mnem_at : 0;
    pref_c = cur_c;
    place_cursor();
}

static int copy_span(const scr_row *row, int from, char *buf, size_t n)
{
    size_t k = 0;
    int i;
    if (n < 2) return 0;
    for (i = from; i < row->n && k + 2 < n; i++) buf[k++] = row->c[i].ch;
    while (k > 0 && (buf[k - 1] == ' ' || buf[k - 1] == '\t')) k--;
    buf[k++] = '\n';
    buf[k] = 0;
    return 1;
}

static int command_empty(const scr_row *row)
{
    int i;
    for (i = row->cmd_at; i < row->n; i++) {
        if (row->c[i].ch != ' ' && row->c[i].ch != '\t') return 0;
    }
    return 1;
}

static void retire_open_prompt(int submitting_open)
{
    scr_row *row;
    if (!row_open || nrows <= 0) return;
    row = &rows[nrows - 1];
    if (!submitting_open &&
        (row->kind == LINE_CMD || row->kind == LINE_FIELD) &&
        row->n == row->cmd_at) {
        cup(origin + nrows - 1, 1);
        erase_eol();
        row->n = 0;
        nrows--;
        row_open = 0;
        return;
    }
    close_printed_line();
}

static int do_enter(int mode, char *buf, size_t n, int field_row)
{
    scr_row *row;
    int open_line;
    if (nrows <= 0) return -1;
    row = &rows[cur_r];
    if (row->kind == LINE_MEM) {
        refresh_mem_ascii(row);
        write_span(cur_r, 0);
        move_vert(1);
        return -1;
    }
    if (row->kind == LINE_REG) {
        refresh_reg_row(cur_r);
        move_vert(1);
        return -1;
    }
    if (row->kind == LINE_DIS && row->mnem_at >= 0) {
        commit_disasm(cur_r);
        return -1;
    }
    if (mode == MODE_FIELD) {
        if (cur_r != field_row) return -1;
        if (!copy_span(row, row->cmd_at, buf, n)) return -1;
        retire_open_prompt(1);
        return 1;
    }
    if (row->kind != LINE_CMD) return -1;
    if (command_empty(row)) return -1;
    open_line = (row_open && cur_r == nrows - 1);
    if (!copy_span(row, row->cmd_at, buf, n)) return -1;
    retire_open_prompt(open_line);
    return 1;
}

static int decode_csi(void)
{
    int param0 = 0;
    int semi = 0;
    int saw_digit = 0;
    for (;;) {
        int d = read_byte(250);
        if (d < 0) return K_NONE;
        if (d >= 0x40 && d <= 0x7E) {
            /* Classic mouse reports are ESC [ M then three bytes. Eat them
             * when they are already sitting in the buffer. */
            if (d == 'M' && !saw_digit) {
                int i;
                for (i = 0; i < 3; i++) {
                    int e = read_byte(10);
                    if (e < 0) break;
                }
                return K_NONE;
            }
            if (d == '~' && param0 == 200) {
                paste_on = 1;
                return K_NONE;
            }
            if (d == '~' && param0 == 201) {
                paste_on = 0;
                return K_NONE;
            }
            if (d == 'A') return K_UP;
            if (d == 'B') return K_DOWN;
            if (d == 'C') return K_RIGHT;
            if (d == 'D') return K_LEFT;
            return K_NONE;
        }
        if (d == ';') semi++;
        else if (d >= '0' && d <= '9' && semi == 0) {
            saw_digit = 1;
            param0 = param0 * 10 + (d - '0');
        }
    }
}

static int decode_esc(void)
{
    int c = read_byte(250);
    if (c < 0) return K_NONE;
    if (c == '[') return decode_csi();
    if (c == 'O') {
        int d = read_byte(250);
        if (d == 'A') return K_UP;
        if (d == 'B') return K_DOWN;
        if (d == 'C') return K_RIGHT;
        if (d == 'D') return K_LEFT;
        return K_NONE;
    }
    if (c == 'A') return K_UP;
    if (c == 'B') return K_DOWN;
    if (c == 'C') return K_RIGHT;
    if (c == 'D') return K_LEFT;
    return K_NONE;
}

static void swallow_lf(void)
{
    int c = read_byte(0);
    if (c >= 0 && c != '\n') unread_byte(c);
}

static int read_key(int *ch)
{
    int c = read_byte(-1);
    if (c == -2) return K_EOF;
    if (c < 0) return K_NONE;
    if (c == 0x04) return K_EOF;
    if (c == '\r' || c == '\n') {
        if (c == '\r') swallow_lf();
        return K_ENTER;
    }
    if (c == 0x7F || c == 0x08) return K_BS;
    if (c == 0x1b) return decode_esc();
    if (c >= 32 && c < 127) {
        *ch = c;
        return K_CHAR;
    }
    return K_NONE;
}

static int alloc_grid(int nr, int nc)
{
    scr_row *n;
    int i;
    n = calloc((size_t)nr, sizeof *n);
    if (!n) return -1;
    for (i = 0; i < nr; i++) {
        n[i].c = calloc((size_t)nc, sizeof(cell_t));
        n[i].mnem_at = -1;
        if (!n[i].c) {
            int j;
            for (j = 0; j < i; j++) free(n[j].c);
            free(n);
            return -1;
        }
    }
    free_rows();
    rows = n;
    row_slots = nr;
    term_rows = nr;
    term_cols = nc;
    return 0;
}

static int copy_grid(int nr, int nc)
{
    scr_row *n;
    int keep;
    int src0;
    int colkeep;
    int was_open;
    int i;
    int k;
    n = calloc((size_t)nr, sizeof *n);
    if (!n) return -1;
    for (i = 0; i < nr; i++) {
        n[i].c = calloc((size_t)nc, sizeof(cell_t));
        n[i].mnem_at = -1;
        if (!n[i].c) {
            int j;
            for (j = 0; j < i; j++) free(n[j].c);
            free(n);
            return -1;
        }
    }
    was_open = row_open;
    keep = nrows < nr ? nrows : nr;
    src0 = nrows > keep ? nrows - keep : 0;
    colkeep = nc > 1 ? nc - 1 : 1;
    for (i = 0; i < keep; i++) {
        scr_row *src = &rows[src0 + i];
        int m = src->n < colkeep ? src->n : colkeep;
        for (k = 0; k < m; k++) n[i].c[k] = src->c[k];
        n[i].n = m;
        n[i].kind = src->kind;
        n[i].cmd_at = src->cmd_at < m ? src->cmd_at : m;
        n[i].mnem_at = src->mnem_at;
        n[i].addr = src->addr;
        if (n[i].kind != LINE_CMD && n[i].kind != LINE_FIELD) classify_row(&n[i]);
    }
    free_rows();
    rows = n;
    row_slots = nr;
    term_rows = nr;
    term_cols = nc;
    nrows = keep;
    row_open = was_open && keep > 0;
    if (cur_r >= nrows) cur_r = nrows > 0 ? nrows - 1 : 0;
    return 0;
}

static void apply_winch(void)
{
    int nr;
    int nc;
    winch_flag = 0;
    measure_term();
    nr = term_rows;
    nc = term_cols;
    if (nr == 0 || nc == 0) return;
    if (copy_grid(nr, nc) != 0) return;
    origin = 1;
    apply_modes();
    clamp_cursor();
    redraw_all();
}

static void apply_cont(void)
{
    cont_flag = 0;
    if (!opened || suspended) return;
    enter_raw();
    apply_modes();
    redraw_all();
}

static void open_prompt(void)
{
    scr_row *row;
    if (row_open && nrows > 0 && rows[nrows - 1].kind == LINE_CMD) {
        cur_r = nrows - 1;
        cur_c = rows[cur_r].n;
        pref_c = cur_c;
        clamp_cursor();
        place_cursor();
        return;
    }
    if (row_open) close_printed_line();
    begin_row(LINE_CMD);
    row = &rows[nrows - 1];
    row->n = 0;
    row->cmd_at = 0;
    cur_r = nrows - 1;
    cur_c = 0;
    pref_c = 0;
    place_cursor();
}

static int edit_loop(int mode, char *buf, size_t n, int field_row)
{
    for (;;) {
        int ch = 0;
        int key;
        int rc;
        if (winch_flag) apply_winch();
        if (cont_flag) apply_cont();
        key = read_key(&ch);
        if (key == K_EOF) return 0;
        if (key == K_UP) move_vert(-1);
        else if (key == K_DOWN) move_vert(1);
        else if (key == K_LEFT) move_horiz(-1);
        else if (key == K_RIGHT) move_horiz(1);
        else if (key == K_BS) do_bs();
        else if (key == K_CHAR) do_char(ch);
        else if (key == K_ENTER) {
            rc = do_enter(mode, buf, n, field_row);
            if (rc > 0) return 1;
        }
    }
}

int screen_mem_bytes(void)
{
    int cols;
    int n = 8;
    if (!active) return 8;
    cols = usable_cols();
    while (n > 0 && (7 + 4 * n) > cols) n--;
    return n;
}

int screen_is_active(void)
{
    return active;
}

void screen_feed(const char *s, size_t n)
{
    size_t i;
    if (!active || !rows) return;
    for (i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)s[i];
        scr_row *row;
        if (ch == '\r') continue;
        if (ch == '\n') {
            close_printed_line();
            continue;
        }
        if (ch < 32 || ch > 126) ch = '.';
        if (!row_open) begin_row(LINE_TEXT);
        row = &rows[nrows - 1];
        if (row->n >= usable_cols()) continue;
        row->c[row->n].ch = (char)ch;
        row->c[row->n].kind = CK_TEXT;
        row->c[row->n].addr = 0;
        row->c[row->n].nib = 0;
        row->n++;
        term_write((const char *)&ch, 1);
    }
}

int screen_open(void)
{
    if (opened) return active ? 1 : 0;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return 0;
    measure_term();
    if (alloc_grid(term_rows, term_cols) != 0) return 0;
    if (!enter_raw()) {
        free_rows();
        return 0;
    }
    install_signals();
    vt52 = probe_dialect();
    apply_modes();
    origin = 1;
    home_clear();
    opened = 1;
    active = 1;
    suspended = 0;
    if (winch_flag) apply_winch();
    return 1;
}

void screen_suspend(void)
{
    if (!opened || suspended) return;
    leave_raw();
    active = 0;
    suspended = 1;
    fflush(stdout);
}

void screen_resume(void)
{
    if (!opened || !suspended) return;
    if (!enter_raw()) return;
    active = 1;
    suspended = 0;
    origin = 1;
    apply_modes();
    clamp_cursor();
    redraw_all();
}

static void unread_span(const unsigned char *buf, int n)
{
    int i;
    for (i = n - 1; i >= 0; i--) unread_byte(buf[i]);
}

/* ANSI cursor-position report, including a VK100 in TM1.
 * Bytes that are not a report go back to the editor. */
static int query_cursor(int *row, int *col)
{
    unsigned char buf[48];
    int n = 0;
    int r = 0;
    int c = 0;
    int semi = 0;
    int d;
    int j;
    term_write("\033[6n", 4);
    d = read_byte(250);
    if (d < 0) return 0;
    if (d != 0x1b) {
        unread_byte(d);
        return 0;
    }
    buf[n++] = (unsigned char)d;
    for (;;) {
        d = read_byte(80);
        if (d < 0) break;
        if (n < (int)sizeof buf) buf[n++] = (unsigned char)d;
        if (d == 'R' || n >= (int)sizeof buf) break;
    }
    if (n >= 2 && buf[1] == '[') {
        for (j = 2; j < n; j++) {
            if (buf[j] >= '0' && buf[j] <= '9') {
                if (!semi) r = r * 10 + (buf[j] - '0');
                else c = c * 10 + (buf[j] - '0');
            } else if (buf[j] == ';') semi = 1;
            else if (buf[j] == 'R' && r >= 1 && c >= 1) {
                *row = r;
                *col = c;
                return 1;
            } else break;
        }
    }
    unread_span(buf, n);
    return 0;
}

void screen_after_external(void)
{
    int row = 1;
    int col = 1;
    if (!opened) return;
    if (!enter_raw()) return;
    active = 1;
    suspended = 0;
    if (!vt52 && query_cursor(&row, &col)) {
        if (col > 1) {
            term_write("\n", 1);
            row++;
        }
        if (row < 1) row = 1;
        if (row > term_rows) row = term_rows;
        nrows = 0;
        row_open = 0;
        origin = row;
        cur_r = 0;
        cur_c = 0;
        pref_c = 0;
        return;
    }
    origin = 1;
    apply_modes();
    redraw_all();
}

void screen_close(void)
{
    if (!opened) return;
    if (!raw_on) enter_raw();
    restore_modes();
    term_write("\r\n", 2);
    leave_raw();
    restore_signals();
    free_rows();
    opened = 0;
    active = 0;
    suspended = 0;
    paste_on = 0;
    nungot = 0;
}

int screen_readline(char *buf, size_t n)
{
    if (!active) return 0;
    open_prompt();
    return edit_loop(MODE_CMD, buf, n, -1);
}

int screen_read_field(char *buf, size_t n)
{
    int field_row;
    int at;
    if (!active || nrows <= 0 || !row_open) return 0;
    field_row = nrows - 1;
    at = rows[field_row].n;
    rows[field_row].kind = LINE_FIELD;
    rows[field_row].cmd_at = at;
    cur_r = field_row;
    cur_c = at;
    pref_c = at;
    place_cursor();
    return edit_loop(MODE_FIELD, buf, n, field_row);
}
