/* acia.c — minimal but usable 6850 ACIA
 *
 * Control/Status at base+0, Data at base+1.
 * During cpu_run() the host terminal is put in raw mode so that
 * typed characters appear in the ACIA RX and program output goes to stdout.
 */

#include "acia.h"
#include "cpu.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>

static uint16_t acia_base = 0;
static uint8_t  ctrl = 0;
static uint8_t  status = 0x02; /* TDRE set */
static uint8_t  rxbuf = 0;
static int      rx_full = 0;
static struct termios oldt;
static int      term_raw = 0;

static void set_raw(void)
{
    if (term_raw) return;
    if (!isatty(STDIN_FILENO)) return;   /* never raw a pipe/heredoc */
    struct termios t;
    tcgetattr(STDIN_FILENO, &oldt);
    t = oldt;
    cfmakeraw(&t);
    t.c_lflag &= ~(ECHO | ICANON);
    t.c_lflag |= ISIG;   /* Ctrl-C still raises SIGINT */
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    int fl = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, fl | O_NONBLOCK);
    term_raw = 1;
}

static void restore_term(void)
{
    if (!term_raw) return;
    if (!isatty(STDIN_FILENO)) { term_raw = 0; return; }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    int fl = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, fl & ~O_NONBLOCK);
    term_raw = 0;
}

void acia_init(uint16_t base)
{
    acia_base = base;
    ctrl = 0;
    status = 0x02; /* TDRE */
    rx_full = 0;
    cpu_register_device(base, acia_read, acia_write);
}

uint8_t acia_read(uint16_t off)
{
    if (off == 0) {
        /* status: TDRE always ready for now, RDRF when we have a char */
        uint8_t s = status;
        if (rx_full) s |= 0x01; /* RDRF */
        /* simulate no errors */
        return s;
    } else {
        if (rx_full) {
            rx_full = 0;
            status &= ~0x01;
            return rxbuf;
        }
        return 0;
    }
}

void acia_write(uint16_t off, uint8_t val)
{
    if (off == 0) {
        ctrl = val;
        /* master reset on bits 1:0 == 11 */
        if ((val & 0x03) == 0x03) {
            rx_full = 0;
            status = 0x02;
        }
    } else {
        /* transmit */
        putchar(val);
        fflush(stdout);
        status |= 0x02; /* TDRE */
    }
}

/* Called from the run loop / go command to feed host input into ACIA */
void acia_poll_input(void)
{
    if (!acia_base) return;
    set_raw();
    unsigned char ch;
    ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n == 1) {
        rxbuf = ch;
        rx_full = 1;
        status |= 0x01;
    }
}

/* restore terminal when dropping back to monitor */
void acia_end_run(void)
{
    restore_term();
}
