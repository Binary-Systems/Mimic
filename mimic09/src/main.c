/* main.c — mimic09 entry point */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>

#include "cpu.h"
#include "debug.h"
#include "loader.h"
#include "acia.h"
#include "pia.h"

static void usage(const char *argv0)
{
    printf("mimic09 — fast 6809/6309 emulator with C64-style monitor\n");
    printf("Usage: %s [options] [file]\n\n", argv0);
    printf("Options:\n");
    printf("  -i, --interactive     enter monitor immediately\n");
    printf("  -l, --load <file>     load SREC/IHEX/BIN at optional address\n");
    printf("  -a, --addr <hex>      force load address for raw binary\n");
    printf("  -g, --go [addr]       run (from addr or reset vector / PC)\n");
    printf("  -c, --cpu <6809|6309> select CPU model (default 6809)\n");
    printf("  --acia <hex>          map 6850 ACIA at this base address\n");
    printf("  --pia  <hex>          map 6821 PIA at this base address\n");
    printf("  -v, --version\n");
    printf("  -h, --help\n");
    printf("\nExamples:\n");
    printf("  %s -l prog.hex -i\n", argv0);
    printf("  %s -l myprog.s19 --acia 0xA000 -g\n", argv0);
    printf("  %s -i\n", argv0);
}

static void version(void)
{
    printf("mimic09 0.9 (modern re-creation)\n");
}

int main(int argc, char **argv)
{
    int interactive = 0;
    int do_go = 0;
    uint16_t go_addr = 0;
    uint16_t force_addr = 0;
    int have_force = 0;
    cpu_model_t model = CPU_6809;
    uint16_t acia_base = 0;
    uint16_t pia_base = 0;
    const char *loadfile = NULL;

    static struct option longopts[] = {
        {"interactive", no_argument, 0, 'i'},
        {"load", required_argument, 0, 'l'},
        {"addr", required_argument, 0, 'a'},
        {"go", optional_argument, 0, 'g'},
        {"cpu", required_argument, 0, 'c'},
        {"acia", required_argument, 0, 1000},
        {"pia",  required_argument, 0, 1001},
        {"version", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0,0,0,0}
    };

    int ch;
    while ((ch = getopt_long(argc, argv, "il:a:g::c:vh", longopts, NULL)) != -1) {
        switch (ch) {
        case 'i': interactive = 1; break;
        case 'l': loadfile = optarg; break;
        case 'a': have_force = 1; sscanf(optarg, "%hx", &force_addr); break;
        case 'g': do_go = 1; if (optarg) sscanf(optarg, "%hx", &go_addr); break;
        case 'c':
            if (strcasecmp(optarg, "6309") == 0) model = CPU_6309;
            else model = CPU_6809;
            break;
        case 1000: sscanf(optarg, "%hx", &acia_base); break;
        case 1001: sscanf(optarg, "%hx", &pia_base); break;
        case 'v': version(); return 0;
        case 'h': usage(argv[0]); return 0;
        default: usage(argv[0]); return 1;
        }
    }

    if (optind < argc && !loadfile) {
        loadfile = argv[optind];
    }

    cpu_init(model);
    if (acia_base) acia_init(acia_base);
    if (pia_base)  pia_init(pia_base);

    if (loadfile) {
        int n = load_file(loadfile, have_force ? force_addr : 0, have_force);
        if (n < 0) {
            fprintf(stderr, "Failed to load %s\n", loadfile);
        } else {
            printf("Loaded %d bytes from %s\n", n, loadfile);
        }
    }

    /* Default: if no -i and a load happened, set PC from reset vector if it looks good */
    if (!interactive && loadfile) {
        uint16_t rst = mem_read16(0xFFFE);
        if (rst >= 0x0100 && rst <= 0xFEFF) {
            cpu.pc = rst;
            printf("PC set from reset vector to %04X\n", cpu.pc);
        }
    }

    if (do_go) {
        if (go_addr) cpu.pc = go_addr;
        cpu.halted = false;
        cpu.debug_mode = false;

        printf("Running. Press Ctrl-C to return.\n");
        while (!cpu.halted && !cpu.stop_requested) {
            cpu_step();
            acia_poll_input();
        }
        acia_end_run();
    }

    if (interactive || (!loadfile && !do_go)) {
        monitor_run();
    }

    return 0;
}
