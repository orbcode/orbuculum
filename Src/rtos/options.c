#include <options.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <nw.h>
#include <git_version_info.h>
#include <generics.h>

#define TOP_UPDATE_INTERVAL 1000

static ProgramOptions defaultOptions = {
    .forceITMSync = true,
    .tag = 1,
    .demangle = true,
    .displayInterval = TOP_UPDATE_INTERVAL * 1000,
    .port = OFCLIENT_SERVER_PORT,
    .server = "localhost",
    .protocol = PROT_OFLOW,
    .rtos = NULL,
    .rtosSort = "cpu",
    .telnetPort = 4444,
    .udpPort = 0,
    .mono = false,
    .cpuFreq = 0,
    .outputExceptions = false
};

ProgramOptions* options_get_defaults(void) {
    return &defaultOptions;
}

void options_print_help(const char *progName) {
    fprintf(stdout, "Usage: %s [options]\n", progName);
    fprintf(stdout, "\nRequired:\n");
    fprintf(stdout, "  -e, --elf-file:      <ElfFile> ELF file for symbols\n");
    fprintf(stdout, "\nOptional:\n");
    fprintf(stdout, "  -D, --no-demangle:   Switch off C++ symbol demangling\n");
    fprintf(stdout, "  -E, --exceptions:    Include exceptions in output\n");
    fprintf(stdout, "  -F, --cpu-freq:      <Hz> CPU frequency for time calculations (omit to show NA)\n");
    fprintf(stdout, "  -f, --input-file:    <filename> Take input from file\n");
    fprintf(stdout, "  -h, --help:          This help\n");
    fprintf(stdout, "  -I, --interval:      <ms> Display interval (default %dms)\n", TOP_UPDATE_INTERVAL);
    fprintf(stdout, "  -j, --json-output:   <file> or 'udp:port' for JSON output (REQUIRED argument)\n");
    fprintf(stdout, "  -K, --ftrace:        <file> ftrace trace output (use - for stdout or /tmp/trace.pipe for live)\n");
    fprintf(stdout, "  -M, --no-colour:     Suppress colour in output\n");
    fprintf(stdout, "  -n, --itm-sync:      Enforce ITM sync requirement\n");
    fprintf(stdout, "  -O, --objdump-opts:  <options> Options to pass directly to objdump\n");
    fprintf(stdout, "  -p, --protocol:      Protocol (OFLOW|ITM)\n");
    fprintf(stdout, "  -P, --pace:          <microseconds> Delay in data transmission\n");
    fprintf(stdout, "  -s, --server:        <Server>:<Port> (default localhost:%d)\n", OFCLIENT_SERVER_PORT);
    fprintf(stdout, "  -T, --rtos:          <type> RTOS type (rtx5)\n");
    fprintf(stdout, "  -S, --rtos-sort:     Sort: cpu|maxcpu|tcb|name|func|priority|switches\n");
    fprintf(stdout, "  -W, --telnet-port:   <port> Telnet port for OpenOCD (default 4444)\n");
    fprintf(stdout, "  -t, --tag:           <stream> OFLOW tag (default 1)\n");
    fprintf(stdout, "  -v, --verbose:       <level> Verbose 0(errors)..3(debug)\n");
    fprintf(stdout, "  -V, --version:       Print version\n");
    fprintf(stdout, "\nEnvironment Variables:\n");
    fprintf(stdout, "  OBJDUMP:             Use non-standard objdump binary\n");
    fprintf(stdout, "\nRuntime Keys (RTOS mode):\n");
    fprintf(stdout, "  t: Sort by TCB address\n");
    fprintf(stdout, "  c: Sort by current CPU usage\n");
    fprintf(stdout, "  m: Sort by maximum CPU usage\n");
    fprintf(stdout, "  n: Sort by thread name\n");
    fprintf(stdout, "  f: Sort by function name\n");
    fprintf(stdout, "  p: Sort by priority\n");
    fprintf(stdout, "  s: Sort by context switches\n");
    fprintf(stdout, "  r: Reset maximum CPU values\n");
}

static struct option longOptions[] = {
    {"no-demangle", no_argument, NULL, 'D'},
    {"elf-file", required_argument, NULL, 'e'},
    {"exceptions", no_argument, NULL, 'E'},
    {"cpu-freq", required_argument, NULL, 'F'},
    {"input-file", required_argument, NULL, 'f'},
    {"interval", required_argument, NULL, 'I'},
    {"json-output", required_argument, NULL, 'j'},
    {"ftrace", required_argument, NULL, 'K'},
    {"no-colour", no_argument, NULL, 'M'},
    {"no-color", no_argument, NULL, 'M'},
    {"itm-sync", no_argument, NULL, 'n'},
    {"objdump-opts", required_argument, NULL, 'O'},
    {"protocol", required_argument, NULL, 'p'},
    {"pace", required_argument, NULL, 'P'},
    {"server", required_argument, NULL, 's'},
    {"rtos", required_argument, NULL, 'T'},
    {"rtos-sort", required_argument, NULL, 'S'},
    {"telnet-port", required_argument, NULL, 'W'},
    {"tag", required_argument, NULL, 't'},
    {"verbose", required_argument, NULL, 'v'},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'V'},
    {NULL, 0, NULL, 0}
};

int options_parse(int argc, char *argv[], ProgramOptions *opts) {
    int c;
    
    memcpy(opts, &defaultOptions, sizeof(ProgramOptions));
    
    while ((c = getopt_long(argc, argv, "De:EF:f:I:j:K:MnO:p:P:s:S:T:W:t:v:hV", 
                            longOptions, NULL)) != -1) {
        switch (c) {
            case 'D':
                opts->demangle = false;
                break;
            case 'e':
                opts->elffile = optarg;
                break;
            case 'E':
                opts->outputExceptions = true;
                break;
            case 'F':
                opts->cpuFreq = atoi(optarg);
                break;
            case 'f':
                opts->file = optarg;
                break;
            case 'I':
                opts->displayInterval = (int64_t)(atof(optarg) * 1000);
                break;
            case 'j':
                if (!optarg || strlen(optarg) == 0)
                {
                    fprintf(stderr, "Error: -j/--json-output requires an argument (file path or 'udp:port')\n");
                    return -1;
                }
                opts->json = optarg;
                if (strncmp(optarg, "udp:", 4) == 0)
                {
                    opts->udpPort = atoi(optarg + 4);
                    if (opts->udpPort <= 0 || opts->udpPort > 65535)
                    {
                        fprintf(stderr, "Error: Invalid UDP port number: %s\n", optarg + 4);
                        return -1;
                    }
                }
                break;
            case 'K':
                opts->ftrace = optarg;
                break;
            case 'M':
                opts->mono = true;
                break;
            case 'n':
                opts->forceITMSync = false;
                break;
            case 'O':
                opts->odoptions = optarg;
                break;
            case 'p':
                if (strcmp(optarg, "OFLOW") == 0) {
                    opts->protocol = PROT_OFLOW;
                } else if (strcmp(optarg, "ITM") == 0) {
                    opts->protocol = PROT_ITM;
                } else {
                    fprintf(stderr, "Unknown protocol: %s\n", optarg);
                    return -1;
                }
                break;
            case 'P':
                opts->paceDelay = atoi(optarg);
                if (opts->paceDelay <= 0) {
                    fprintf(stderr, "Pace delay must be positive\n");
                    return -1;
                }
                break;
            case 's':
                opts->server = optarg;
                char *colon = strchr(optarg, ':');
                if (colon) {
                    *colon = 0;
                    opts->port = atoi(++colon);
                }
                break;
            case 'S':
                opts->rtosSort = optarg;
                break;
            case 'T':
                opts->rtos = optarg;
                break;
            case 'W':
                opts->telnetPort = atoi(optarg);
                break;
            case 't':
                opts->tag = atoi(optarg);
                break;
            case 'v':
                genericsSetReportLevel(atoi(optarg));
                break;
            case 'V':
                genericsFPrintf(stdout, "pe-orbtop-rtos version " GIT_DESCRIBE EOL);
                exit(0);
            case 'h':
                options_print_help(argv[0]);
                return -1;
            default:
                fprintf(stderr, "Unknown option\n");
                return -1;
        }
    }
    
    if (!opts->elffile) {
        fprintf(stderr, "Error: ELF file required (-e)\n");
        return -1;
    }
    
    return 0;
}