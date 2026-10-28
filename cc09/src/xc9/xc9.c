/* xc9.c — Wintek C Compiler Driver, V1.0 r3
 *
 * Chains xc_1 → xc9_3 → a9 → xld. The link pulls cstart, chario,
 * clrio, and stdio from lib/src, plus rnlib9.lib (.LINK and printf)
 * through default.dir.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdint.h>
#include <sys/stat.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include "util.h"

static const char *VERSION = "1.0 r3";
static const char *COPYRIGHT = "Copyright 2026 Binary Systems, Inc.";

static char exe_dir[1024];

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [options] file.c ...\n", argv0);
    fprintf(stderr, "  -o file   absolute image (.mik)\n");
    fprintf(stderr, "  -map file linker map\n");
    fprintf(stderr, "  -list     keep the assembly listing\n");
    fprintf(stderr, "  -nh       no banner\n");
    fprintf(stderr, "  -v        show pass banners\n");
    fprintf(stderr, "  -Ipath    include path (passed to pass 1)\n");
    exit(1);
}

static void init_exe_dir(const char *argv0) {
    char buf[1024];
#if defined(__APPLE__)
    uint32_t sz = (uint32_t)sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) != 0) snprintf(buf, sizeof(buf), "%s", argv0);
#else
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n < 0) snprintf(buf, sizeof(buf), "%s", argv0);
    else buf[n] = 0;
#endif
    char real[1024];
    if (!realpath(buf, real)) snprintf(real, sizeof(real), "%s", buf);
    char *sl = strrchr(real, '/');
    if (!sl) snprintf(exe_dir, sizeof(exe_dir), ".");
    else {
        if (sl == real) snprintf(exe_dir, sizeof(exe_dir), "/");
        else { *sl = 0; snprintf(exe_dir, sizeof(exe_dir), "%s", real); }
    }
}

static int runv(char *const arg[]) {
    pid_t pid = fork();
    if (pid < 0) return 127;
    if (pid == 0) {
        execv(arg[0], arg);
        perror(arg[0]);
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return 127;
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return 1;
}

static void stem_of(const char *path, char *out, size_t n) {
    const char *b = path;
    for (const char *p = path; *p; p++) if (*p == '/') b = p + 1;
    snprintf(out, n, "%s", b);
    char *dot = strrchr(out, '.');
    if (dot && (strcmp(dot, ".c") == 0 || strcmp(dot, ".C") == 0)) *dot = 0;
}

static void dir_of(const char *path, char *out, size_t n) {
    snprintf(out, n, "%s", path);
    char *sl = strrchr(out, '/');
    if (!sl) snprintf(out, n, ".");
    else if (sl == out) snprintf(out, n, "/");
    else *sl = 0;
}

static int write_default_dir(const char *path) {
    FILE *d = fopen(path, "w");
    if (!d) {
        fprintf(stderr, "xc9: can't create default linker directive file\n");
        return 0;
    }
    fputs("*  Default linker directives\n", d);
    fputs("LIBRARY rnlib9.lib\n", d);
    fputs("0-DFFF\n", d);
    fputs("SECTION * BSCT @ 0-FF\n", d);
    fputs("END\n", d);
    fclose(d);
    return 1;
}

static int map_has_unsatisfied(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[256];
    int seen = 0, armed = 0, bad = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "UNSATISFIED")) { seen = 1; continue; }
        if (seen && strstr(line, "--------")) { armed = 1; continue; }
        if (!armed) continue;
        if (line[0] == '\n' || line[0] == 0) continue;
        /* a symbol line is indented and not the header */
        if (line[0] == ' ' && line[1] != '-' && line[1] != 'S') {
            char name[64] = {0};
            if (sscanf(line, " %63s", name) == 1 && strcmp(name, "Symbol") != 0 &&
                strcmp(name, "--------") != 0) {
                fprintf(stderr, "xc9: unsatisfied external %s\n", name);
                bad = 1;
            }
        }
    }
    fclose(f);
    return bad;
}

int main(int argc, char **argv) {
    int list = 0, noheader = 0, verbose = 0;
    const char *outfile = NULL;
    const char *mapfile = NULL;
    const char *ipath = NULL;
    const char *srcs[16];
    int nsrc = 0;

    init_exe_dir(argv[0]);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-list") == 0) list = 1;
        else if (strcmp(argv[i], "-nh") == 0) noheader = 1;
        else if (strcmp(argv[i], "-v") == 0) verbose = 1;
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) outfile = argv[++i];
        else if (strcmp(argv[i], "-map") == 0 && i + 1 < argc) mapfile = argv[++i];
        else if (strncmp(argv[i], "-I", 2) == 0) {
            if (argv[i][2]) ipath = argv[i];
            else if (i + 1 < argc) ipath = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "warning: option %s not used by this driver\n", argv[i]);
        } else if (nsrc < 16) srcs[nsrc++] = argv[i];
        else die("too many source files");
    }
    if (!noheader) banner("Binary Systems, Inc. Compiler Driver", VERSION, COPYRIGHT);
    if (nsrc == 0) {
        fprintf(stderr, "error on file: no C source files\n");
        usage(argv[0]);
    }

    char stem[256];
    stem_of(srcs[0], stem, sizeof(stem));
    char srcdir[1024];
    dir_of(srcs[0], srcdir, sizeof(srcdir));
    char mik[1024];
    if (outfile) snprintf(mik, sizeof(mik), "%s", outfile);
    else snprintf(mik, sizeof(mik), "%s/%s.mik", srcdir, stem);

    char work[1024];
    snprintf(work, sizeof(work), "/tmp/mimic-xc9-%d", (int)getpid());
    if (mkdir(work, 0755) != 0) die("can't create %s", work);

    char xc1[1200], xc3[1200], a9[1200], xld[1200];
    snprintf(xc1, sizeof(xc1), "%s/xc_1", exe_dir);
    snprintf(xc3, sizeof(xc3), "%s/xc9_3", exe_dir);
    snprintf(a9, sizeof(a9), "%s/a9", exe_dir);
    snprintf(xld, sizeof(xld), "%s/xld", exe_dir);

    char rels[64][1200];
    int nrel = 0;
    const char *quiet = verbose ? "-v" : "-nh";

    for (int i = 0; i < nsrc; i++) {
        char one[256];
        stem_of(srcs[i], one, sizeof(one));
        char ic1[1200], ic2[1200], asmfn[1200], rel[1200], lst[1200];
        snprintf(ic1, sizeof(ic1), "%s/%s.icf1", work, one);
        snprintf(ic2, sizeof(ic2), "%s/%s.icf2", work, one);
        snprintf(asmfn, sizeof(asmfn), "%s/%s.asm", work, one);
        snprintf(rel, sizeof(rel), "%s/%s.rel", work, one);
        snprintf(lst, sizeof(lst), "%s/%s.lst", srcdir, one);
        char *p1[8];
        int np = 0;
        p1[np++] = xc1; p1[np++] = ic1; p1[np++] = ic2; p1[np++] = (char *)srcs[i];
        p1[np++] = (char *)quiet;
        if (ipath) p1[np++] = (char *)ipath;
        p1[np] = NULL;
        if (runv(p1) != 0) return 1;
        char *p3[] = { xc3, ic1, ic2, asmfn, (char *)quiet, NULL };
        if (runv(p3) != 0) return 1;
        char *pa[8];
        int na = 0;
        pa[na++] = a9; pa[na++] = asmfn; pa[na++] = (char *)quiet; pa[na++] = "-o"; pa[na++] = rel;
        if (list) { pa[na++] = "-l"; pa[na++] = lst; }
        pa[na] = NULL;
        if (runv(pa) != 0) return 1;
        snprintf(rels[nrel], sizeof(rels[nrel]), "%s", rel);
        nrel++;
    }

    const char *rt[] = { "cstart", "chario", "clrio", "stdio" };
    for (int i = 0; i < 4; i++) {
        char srcp[1200], rel[1200];
        snprintf(srcp, sizeof(srcp), "%s/../lib/src/%s.asm", exe_dir, rt[i]);
        snprintf(rel, sizeof(rel), "%s/%s.rel", work, rt[i]);
        char *pa[] = { a9, srcp, (char *)quiet, "-o", rel, NULL };
        if (runv(pa) != 0) {
            fprintf(stderr, "xc9: assemble failed: %s\n", srcp);
            return 1;
        }
        snprintf(rels[nrel], sizeof(rels[nrel]), "%s", rel);
        nrel++;
    }

    char dirp[1200];
    snprintf(dirp, sizeof(dirp), "%s/default.dir", work);
    if (!write_default_dir(dirp)) return 1;
    if (!write_default_dir("default.dir")) return 1;

    char mapp[1200];
    if (!mapfile) {
        snprintf(mapp, sizeof(mapp), "%s/link.map", work);
        mapfile = mapp;
    }
    char *px[80];
    int nx = 0;
    px[nx++] = xld;
    px[nx++] = (char *)quiet;
    px[nx++] = "-d";
    px[nx++] = dirp;
    px[nx++] = "-o";
    px[nx++] = mik;
    px[nx++] = "-map";
    px[nx++] = (char *)mapfile;
    for (int i = 0; i < nrel && nx < 78; i++) px[nx++] = rels[i];
    px[nx] = NULL;
    if (runv(px) != 0) return 1;
    if (map_has_unsatisfied(mapfile)) return 1;

    printf("xc9: %s\n", mik);
    return 0;
}
