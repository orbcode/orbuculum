/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ITM Top for Orbuculum
 * =====================
 *
 */

#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdio.h>
#include <assert.h>
#include <inttypes.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/select.h>
#include <termios.h>

#include <generics.h>
#include <uthash.h>
#include <git_version_info.h>
#include <itmDecoder.h>
#include <oflow.h>
#include <symbols.h>
#include <msgSeq.h>
#include <nw.h>
#include <stream.h>
#include <rtos_support.h>
#include <telnet_client.h>
#include <output_handler.h>
#include <output_console.h>
#include <output_json.h>
#include <options.h>
#include <rtos/exceptions.h>

#define CUTOFF              (10)             /* Default cutoff at 0.1% */
#define TOP_UPDATE_INTERVAL (1000)           /* Interval between each on screen update */

#define MAX_EXCEPTIONS      (512)            /* Maximum number of exceptions to be considered */
#define NO_EXCEPTION        (0xFFFFFFFF)     /* Flag indicating no exception is being processed */

#define MSG_REORDER_BUFLEN  (10)             /* Maximum number of samples to re-order for timekeeping */

#define DWT_NUM_EVENTS 6
const char *evName[DWT_NUM_EVENTS] = {"CPI", "Exc", "Slp", "LSU", "Fld", "Cyc"};

/* Protocol enum is defined in options.h */
const char *protString[] = {"OFLOW", "ITM", NULL};



static ProgramOptions options;

/* ----------- LIVE STATE ----------------- */
struct
{
    struct ITMDecoder i;                               /* The decoders and the packets from them */
    struct MSGSeq    d;                                   /* Message (re-)sequencer */
    struct ITMPacket h;
    struct OFLOW c;
    enum timeDelay timeStatus;                         /* Indicator of if this time is exact */
    uint64_t timeStamp;                                /* Latest received time */
    struct Frame cobsPart;                             /* Any part frame that has been received */

    struct SymbolSet *s;                               /* Symbols read from elf */

    struct exceptionRecord er[MAX_EXCEPTIONS];         /* Exceptions we received on this interval */
    uint32_t currentException;                         /* Exception we are currently embedded in */
    uint32_t erDepth;                                   /* Current depth of exception stack */

    int64_t lastReportus;                              /* Last time an output report was generated, in microseconds */
    int64_t lastReportTicks;                           /* Last time an output report was generated, in ticks */
    uint32_t ITMoverflows;                             /* Has an ITM overflow been detected? */
    uint32_t SWPkt;                                    /* Number of SW Packets received */
    uint32_t TSPkt;                                    /* Number of TS Packets received */
    uint32_t HWPkt;                                    /* Number of HW Packets received */
    uint32_t dwt_event_acc[DWT_NUM_EVENTS];            /* Accumulator for DWT events */

    uint32_t interrupts;
    bool ending;                                       /* Flag to exit */

    /* RTOS tracking state */
    struct rtosState *rtos;                            /* RTOS tracking state */
} _r;

static struct termios old_tio;
static bool terminal_modified = false;
static OutputConfig *_outputConfig = NULL;

static void _closeTelnet( void );

static void _initOutput( void )
{
    if (_outputConfig)
    {
        output_cleanup(_outputConfig);
        free(_outputConfig->udp_dest);
        free(_outputConfig);
    }
    
    _outputConfig = calloc(1, sizeof(OutputConfig));
    if (!_outputConfig) return;
    
    _outputConfig->mono = options.mono;
    _outputConfig->udp_socket = -1;
    
    if (options.json)
    {
        if (strncmp(options.json, "udp:", 4) == 0)
        {
            _outputConfig->mode = OUTPUT_JSON_UDP;
            _outputConfig->udp_dest = calloc(1, sizeof(struct sockaddr_in));
            if (_outputConfig->udp_dest)
            {
                _outputConfig->udp_dest->sin_port = htons(options.udpPort);
            }
            output_init(_outputConfig);
        }
        else
        {
            _outputConfig->mode = OUTPUT_JSON_FILE;
            if (strcmp(options.json, "-") == 0)
            {
                _outputConfig->file = stdout;
            }
            else
            {
                _outputConfig->file = fopen(options.json, "w");
                if (!_outputConfig->file)
                {
                    genericsReport(V_ERROR, "Cannot open JSON output file %s" EOL, options.json);
                    _outputConfig->mode = OUTPUT_CONSOLE;
                }
            }
        }
    }
    else
    {
        _outputConfig->mode = OUTPUT_CONSOLE;
    }
}

static void _reinitializeRTOS( void )
{
    if ( !options.rtos ) return;
    
    /* Save output config before cleaning up */
    OutputConfig *saved_output = NULL;
    if ( _r.rtos && _r.rtos->output_config )
    {
        saved_output = _r.rtos->output_config;
    }
    
    /* Close telnet connection to force fresh reconnect */
    _closeTelnet();
    
    /* Clean up existing RTOS state */
    if ( _r.rtos )
    {
        _r.rtos->output_config = NULL; /* Don't free the output config yet */
        rtosFree( _r.rtos );
        _r.rtos = NULL;
    }
    
    /* Wait for OpenOCD telnet to be ready - retry up to 10 times */
    for ( int retry = 0; retry < 10; retry++ )
    {
        if ( retry > 0 )
        {
            genericsReport( V_INFO, "Waiting for OpenOCD telnet to be ready... (attempt %d/10)" EOL, retry + 1 );
            usleep( 500000 ); /* Wait 500ms between retries */
        }
        
        _r.rtos = rtosDetectAndInit(_r.s, options.rtos, options.telnetPort, options.cpuFreq);
        if ( _r.rtos )
        {
            /* Restore the output config */
            if ( saved_output )
            {
                _r.rtos->output_config = saved_output;
                genericsReport( V_DEBUG, "Restored output_config to RTOS after reinit" EOL );
            }
            genericsReport( V_INFO, "RTOS reconnected and verified for %s" EOL, _r.rtos->name );
            return;
        }
    }
    
    genericsReport( V_ERROR, "RTOS reinitialization failed after 10 attempts" EOL );
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
int64_t _timestamp( void )

{
    struct timeval te;
    gettimeofday( &te, NULL ); // get current time
    int64_t microseconds = te.tv_sec * 1000000LL + ( te.tv_usec ); // accumulate microseconds
    return microseconds;
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Handler for individual message types from SWO
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void _exitEx( int64_t ts )

{
    if ( _r.currentException == NO_EXCEPTION )
    {
        /* This can happen under startup and overflow conditions */
        return;
    }

    /* Calculate total time for this exception as we're leaving it */
    int64_t thisTime = ts - _r.er[_r.currentException].entryTime;
    int64_t thisStealTime = _r.er[_r.currentException].stealTime;

    _r.er[_r.currentException].thisTime += thisTime;
    _r.er[_r.currentException].visits++;
    _r.er[_r.currentException].totalTime += _r.er[_r.currentException].thisTime;

    /* Zero the entryTime as it's used to show when an exception is 'live' */
    _r.er[_r.currentException].entryTime = 0;

    /* ...and account for this time */
    if ( ( !_r.er[_r.currentException].minTime ) || ( _r.er[_r.currentException].thisTime < _r.er[_r.currentException].minTime ) )
    {
        _r.er[_r.currentException].minTime = _r.er[_r.currentException].thisTime;
    }

    if ( _r.er[_r.currentException].thisTime > _r.er[_r.currentException].maxTime )
    {
        _r.er[_r.currentException].maxTime = _r.er[_r.currentException].thisTime;
    }

    const int64_t walltime = _r.er[_r.currentException].thisTime + _r.er[_r.currentException].stealTime;

    if ( walltime > _r.er[_r.currentException].maxWallTime )
    {
        _r.er[_r.currentException].maxWallTime = walltime;
    }

    if ( _r.erDepth > _r.er[_r.currentException].maxDepth )
    {
        _r.er[_r.currentException].maxDepth = _r.erDepth;
    }

    /* Step out of this exception */
    _r.currentException = _r.er[_r.currentException].prev;

    if ( _r.erDepth )
    {
        _r.erDepth--;
    }

    /* If we are still in an exception then carry on accounting */
    if ( _r.currentException != NO_EXCEPTION )
    {
        _r.er[_r.currentException].entryTime = ts;
        _r.er[_r.currentException].stealTime += thisTime + thisStealTime;
    }
}
// ====================================================================================================
void _handleTS( struct TSMsg *m, struct ITMDecoder *i )

{
    assert( m->msgtype == MSG_TS );

    _r.timeStatus = m->timeStatus;
    _r.timeStamp += m->timeInc;
}
// ====================================================================================================
void _handleException( struct excMsg *m, struct ITMDecoder *i )

{
    assert( m->msgtype == MSG_EXCEPTION );
    assert( m->exceptionNumber < MAX_EXCEPTIONS );
    
    genericsReport( V_DEBUG, "Exception event: num=%d, type=%d" EOL, m->exceptionNumber, m->eventType );

    switch ( m->eventType )
    {
        case EXEVENT_ENTER:
            if ( _r.er[m->exceptionNumber].entryTime != 0 )
            {
                /* We beleive we are already in this exception. This can happen when we've lost
                 * messages due to ITM overflow. Don't process the enter. Everything will get
                 * fixed up in the next EXEXIT_RESUME which will reset everything.
                 */
                break;
            }

            if ( _r.currentException != NO_EXCEPTION )
            {
                /* Already in an exception ... account for time until now */
                _r.er[_r.currentException].thisTime += _r.timeStamp - _r.er[_r.currentException].entryTime;
            }

            /* Record however we got to this exception */
            _r.er[m->exceptionNumber].prev = _r.currentException;

            /* Now dip into this exception */
            _r.currentException = m->exceptionNumber;
            _r.er[m->exceptionNumber].entryTime = _r.timeStamp;
            _r.er[m->exceptionNumber].thisTime = 0;
            _r.er[m->exceptionNumber].stealTime = 0;
            _r.erDepth++;
            break;

        case EXEVENT_RESUME: /* Unwind all levels of exception (deals with tail chaining) */
            while ( ( _r.currentException != m->exceptionNumber ) && ( _r.erDepth ) )
            {
                _exitEx( _r.timeStamp );
            }

            break;

        case EXEVENT_EXIT: /* Exit single level of exception */
            _exitEx( _r.timeStamp );
            break;

        default:
        case EXEVENT_UNKNOWN:
            genericsReport( V_INFO, "Unrecognised exception event (%d,%d)" EOL, m->eventType, m->exceptionNumber );
            break;
    };
}
// ====================================================================================================
// ====================================================================================================
void _handleDWTEvent( struct dwtMsg *m, struct ITMPacket *p )

{
    /* DWT events are different from watchpoints - just count them */
    for ( uint32_t i = 0; i < DWT_NUM_EVENTS; i++ )
    {
        if ( m->event & ( 1 << i ) )
        {
            _r.dwt_event_acc[i]++;
        }
    }
}
// ====================================================================================================
void _handleDataAccessWP( struct wptMsg *m, struct ITMDecoder *i )
{
    genericsReport( V_DEBUG, "DWT WP: comp=%d data=0x%08X" EOL, m->comp, m->data );
    
    /* Handle RTX5 thread switch watchpoint */
    if ( _r.rtos && _r.rtos->enabled )
    {
        /* Call RTOS handler with watchpoint data - accept both comp 0 and 1 */
        if ( m->comp == 0 || m->comp == 1 )
        {
            genericsReport( V_DEBUG, "DWT WP: comp=%d data=0x%08X, _r.timeStamp=%llu" EOL, 
                          m->comp, m->data, _r.timeStamp );
            /* Use _r.timeStamp which is the accumulated ITM timestamp */
            rtosHandleDWTMatchWithTimestamp(_r.rtos, _r.s, m->comp, 0, m->data, _r.timeStamp, options.telnetPort);
        }
    }
}
static void _closeTelnet( void )
{
    telnet_disconnect();
}

int options_telnetPort = 0;
int options_udpPort = 0;
void rtosConfigureDWT(uint32_t watch_address)
{
    telnet_configure_dwt(watch_address);
}

void rtosClearMemoryCacheForTCB(uint32_t tcb_addr)
{
    telnet_clear_cache_for_tcb(tcb_addr);
}

/* Public wrappers for memory reading */
uint32_t rtosReadMemoryWord( uint32_t address )
{
    return telnet_read_memory_word( address );
}

char *rtosReadMemoryString( uint32_t address, char *buffer, size_t maxlen )
{
    return telnet_read_memory_string( address, buffer, maxlen );
}



// ====================================================================================================
static void _processOutput( int64_t lastTime )
{
    if (!_outputConfig) return;
    
    IntervalOutput interval = {
        .timestamp = lastTime,
        .interval_us = lastTime - _r.lastReportus,
        .interval_ticks = _r.timeStamp - _r.lastReportTicks,
        .ticks_per_ms = (_r.lastReportTicks && lastTime != _r.lastReportus) ? 
                        ((_r.timeStamp - _r.lastReportTicks) * 1000) / (lastTime - _r.lastReportus) : 0
    };
    
    if (_outputConfig->mode == OUTPUT_CONSOLE)
    {
        output_start_frame(_outputConfig, &interval);
        
        if (_r.rtos && _r.rtos->enabled && _r.rtos->threads)
        {
            uint64_t window_time_us = (lastTime - _r.lastReportus);
            bool itm_overflow = (_r.ITMoverflows != ITMDecoderGetStats(&_r.i)->overflow);
            
            /* Update CPU metrics first (calculates percentages and updates max values) */
            rtosUpdateThreadCpuMetrics(_r.rtos, window_time_us);
            
            /* Then output the data */
            output_console_rtos_threads(_outputConfig, _r.rtos, window_time_us, itm_overflow, options.rtosSort);
        }
        
        if (options.outputExceptions)
        {
            output_console_exception_header(_outputConfig);
            
            bool hasExceptions = false;
            for (uint32_t e = 0; e < MAX_EXCEPTIONS; e++)
            {
                if (_r.er[e].visits)
                {
                    hasExceptions = true;
                    break;
                }
            }
            
            if (!hasExceptions)
            {
                output_console_no_exceptions(_outputConfig);
            }
            else
            {
                for (uint32_t e = 0; e < MAX_EXCEPTIONS; e++)
                {
                    if (_r.er[e].visits)
                    {
                        char nameBuf[30];
                        snprintf(nameBuf, sizeof(nameBuf), "%2d (%s)", e, exceptionGetName(e));
                        
                        ExceptionOutput exc = {
                            .exception_num = e,
                            .exception_name = nameBuf,
                            .visits = _r.er[e].visits,
                            .max_depth = _r.er[e].maxDepth,
                            .total_time = _r.er[e].totalTime,
                            .min_time = _r.er[e].minTime,
                            .max_time = _r.er[e].maxTime,
                            .max_wall_time = _r.er[e].maxWallTime,
                            .util_percent = (_r.timeStamp - _r.lastReportTicks) ? 
                                           ((float)_r.er[e].totalTime / (_r.timeStamp - _r.lastReportTicks) * 100.0f) : 0,
                            .ave_time = _r.er[e].visits ? (_r.er[e].totalTime / _r.er[e].visits) : 0
                        };
                        
                        output_exception_entry(_outputConfig, &exc);
                    }
                }
            }
            output_console_exception_footer(_outputConfig);
        }
        
        output_console_status_indicators(_outputConfig, 
                                        _r.ITMoverflows != ITMDecoderGetStats(&_r.i)->overflow,
                                        _r.SWPkt != ITMDecoderGetStats(&_r.i)->SWPkt,
                                        _r.TSPkt != ITMDecoderGetStats(&_r.i)->TSPkt,
                                        _r.HWPkt != ITMDecoderGetStats(&_r.i)->HWPkt);
        
        uint64_t interval_ms = (lastTime - _r.lastReportus) / 1000;
        output_console_interval_info(_outputConfig, interval_ms, _r.timeStamp - _r.lastReportTicks,
                                    interval.ticks_per_ms, _r.lastReportTicks && lastTime != _r.lastReportus);
        
        output_console_sort_options(_outputConfig, _r.rtos && _r.rtos->enabled);
        
        StatsOutput stats = {
            .overflow = ITMDecoderGetStats(&_r.i)->overflow,
            .sync_count = ITMDecoderGetStats(&_r.i)->syncCount,
            .error_count = ITMDecoderGetStats(&_r.i)->ErrorPkt,
            .sw_packets = _r.SWPkt,
            .ts_packets = _r.TSPkt,
            .hw_packets = _r.HWPkt
        };
        output_stats(_outputConfig, &stats);
    }
    else if (_outputConfig->mode == OUTPUT_JSON_FILE || _outputConfig->mode == OUTPUT_JSON_UDP)
    {
        if (_r.rtos && _r.rtos->enabled && _r.rtos->threads)
        {
            uint64_t window_time_us = (lastTime - _r.lastReportus);
            bool itm_overflow = (_r.ITMoverflows != ITMDecoderGetStats(&_r.i)->overflow);
            
            /* Update CPU metrics first (calculates percentages and updates max values) */
            rtosUpdateThreadCpuMetrics(_r.rtos, window_time_us);
            
            /* Then output the data */
            output_json_rtos_threads(_outputConfig, _r.rtos, window_time_us, itm_overflow);
        }
        
        if (options.outputExceptions)
        {
            output_json_exceptions(_outputConfig, _r.er, MAX_EXCEPTIONS, _r.timeStamp, _r.lastReportTicks);
        }
    }
    
    /* Reset thread counters for next interval */
    rtosResetThreadCounters(_r.rtos);
}

// ====================================================================================================
// Pump characters into the itm decoder
// ====================================================================================================
void _itmPumpProcess( uint8_t c )
{
    typedef void ( *handlers )( void *decoded, struct ITMDecoder * i );

    /* Handlers for each complete message received */
    static const handlers h[MSG_NUM_MSGS] =
    {
        /* MSG_UNKNOWN */         NULL,
        /* MSG_RESERVED */        NULL,
        /* MSG_ERROR */           NULL,
        /* MSG_NONE */            NULL,
        /* MSG_SOFTWARE */        NULL,
        /* MSG_NISYNC */          NULL,
        /* MSG_OSW */             NULL,
        /* MSG_DATA_ACCESS_WP */  ( handlers )_handleDataAccessWP,
        /* MSG_DATA_RWWP */       NULL,
        /* MSG_PC_SAMPLE */       NULL,  /* PC samples no longer used */
        /* MSG_DWT_EVENT */       ( handlers )_handleDWTEvent,
        /* MSG_EXCEPTION */       ( handlers )_handleException,
        /* MSG_TS */              ( handlers )_handleTS
    };

    struct msg *p;

    if ( !MSGSeqPump( &_r.d, c ) )
    {
        return;
    }

    /* We are synced timewise, so empty anything that has been waiting */
    while ( true )
    {
        p = MSGSeqGetPacket( &_r.d );

        if ( !p )
        {
            /* all read */
            break;
        }

        assert( p->genericMsg.msgtype < MSG_NUM_MSGS );

        if ( h[p->genericMsg.msgtype] )
        {
            ( h[p->genericMsg.msgtype] )( p, &_r.i );
        }
    }

    return;
}
// ====================================================================================================
// ====================================================================================================
// Protocol pump for decoding messages
// ====================================================================================================
// ====================================================================================================

static void _OFLOWpacketRxed ( struct OFLOWFrame *p, void *param )
{
    if ( !p->good )
    {
        genericsReport( V_INFO, "Bad packet received" EOL );
    }
    else
    {
        if ( p->tag == options.tag )
        {
            for ( int i = 0; i < p->len; i++ )
            {
                _itmPumpProcess( p->d[i] );
            }
        }
    }
}

// ====================================================================================================

static struct Stream *_openStream( void )
{
    if ( options.file != NULL )
    {
        return streamCreateFile( options.file );
    }
    else
    {
        return streamCreateSocket( options.server, options.port );
    }
}

// ====================================================================================================
static void _intHandler( int sig )

{
    /* CTRL-C exit is not an error... */
    _r.ending = true;
    
    /* Restore terminal settings */
    if (terminal_modified) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
        terminal_modified = false;
    }
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Externally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
int main( int argc, char *argv[] )

{
    uint8_t cbw[TRANSFER_SIZE];

    bool alreadyReported = false;

    int64_t remainTime;
    int64_t thisTime;
    struct timeval tv;
    enum ReceiveResult receiveResult = RECEIVE_RESULT_OK;
    size_t receivedSize = 0;
    enum symbolErr r;

    /* Parse command line options using options module */
    if ( options_parse( argc, argv, &options ) != 0 )
    {
        exit( -EINVAL );
    }

    genericsScreenHandling( !options.mono );

    /* Check we've got _some_ symbols to start from */
    r = SymbolSetCreate( &_r.s, options.elffile, NULL, options.demangle, true, true, options.odoptions );

    switch ( r )
    {
        case SYMBOL_NOELF:
            genericsExit( -1, "Elf file or symbols in it not found" EOL );
            break;

        case SYMBOL_NOOBJDUMP:
            genericsExit( -1, "No objdump found" EOL );
            break;

        case SYMBOL_UNSPECIFIED:
            genericsExit( -1, "Unknown error in symbol subsystem" EOL );
            break;

        default:
            break;
    }

    genericsReport( V_WARN, "Loaded %s" EOL, options.elffile );

    if ( _r.s )
    {
        genericsReport( V_INFO, "Files:      %d" EOL "Functions: %d" EOL "Source:    %d" EOL, _r.s->fileCount, _r.s->functionCount, _r.s->sourceCount );
    }

    /* Initialize RTOS support if requested */
    if ( options.rtos )
    {
        _r.rtos = rtosDetectAndInit(_r.s, options.rtos, options.telnetPort, options.cpuFreq);
        if ( !_r.rtos )
        {
            /* Only exit if there's a real mismatch, not connection issues */
            if (terminal_modified) {
                tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
                terminal_modified = false;
            }
            genericsExit( -1, "RTOS initialization failed - ELF mismatch detected" EOL );
        }
        
        if ( _r.rtos )
        {
            genericsReport( V_INFO, "RTOS tracking enabled for %s" EOL, _r.rtos->name );
        }
        
        if ( _r.rtos )
        {
            
            /* Set up ftrace output if requested */
            if ( options.ftrace )
            {
                OutputConfig *output = calloc(1, sizeof(OutputConfig));
                if ( output )
                {
                    output->mode = OUTPUT_FTRACE;
                    
                    if ( strcmp(options.ftrace, "-") == 0 )
                    {
                        output->file = stdout;
                    }
                    else
                    {
                        output->file = fopen(options.ftrace, "w");
                        if ( !output->file )
                        {
                            genericsReport( V_ERROR, "Cannot open ftrace output file %s" EOL, options.ftrace );
                            free(output);
                            output = NULL;
                        }
                        else
                        {
                            setvbuf(output->file, NULL, _IONBF, 0);
                        }
                    }
                    
                    if ( output && output->file )
                    {
                        _r.rtos->output_config = output;
                        output_init(output);
                        genericsReport( V_INFO, "ftrace output enabled to %s" EOL,
                                      options.ftrace[0] == '-' ? "stdout" : options.ftrace );
                    }
                }
            }
        }
    }

    /* Reset the handlers before we start */
    ITMDecoderInit( &_r.i, options.forceITMSync );
    OFLOWInit( &_r.c );
    MSGSeqInit( &_r.d, &_r.i, MSG_REORDER_BUFLEN );

    /* This ensures the signal handler gets called */
    if ( SIG_ERR == signal( SIGINT, _intHandler ) )
    {
        genericsExit( -1, "Failed to establish Int handler" EOL );
    }

    /* Set terminal to non-canonical mode for immediate key reading */
    struct termios new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);  /* Disable canonical mode and echo */
    new_tio.c_cc[VMIN] = 0;   /* Non-blocking read */
    new_tio.c_cc[VTIME] = 0;  /* No timeout */
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
    terminal_modified = true;

    /* First interval will be from startup to first packet arriving */
    _r.lastReportus = _timestamp();
    _r.currentException = NO_EXCEPTION;

    /* Initialize output configuration */
    _initOutput();

    while ( !_r.ending )
    {
        struct Stream *stream = _openStream();

        if ( stream == NULL )
        {
            if ( !alreadyReported )
            {
                genericsReport( V_ERROR, "No connection" EOL );
                alreadyReported = true;
            }

            usleep( 500 * 1000 );
            continue;
        }

        alreadyReported = false;

        if (_outputConfig && _outputConfig->mode == OUTPUT_CONSOLE)
        {
            output_clear_screen(_outputConfig);
            output_console_message(_outputConfig, "Connected..." EOL);
        }
        
        /* Configure exception trace if requested */
        if ( options.outputExceptions )
        {
            if (_outputConfig && _outputConfig->mode == OUTPUT_CONSOLE)
            {
                output_console_message(_outputConfig, "Exception output ENABLED (-E flag detected)" EOL);
            }
            if ( options.telnetPort > 0 )
            {
                telnet_configure_exception_trace(true);
                if (_outputConfig && _outputConfig->mode == OUTPUT_CONSOLE)
                {
                    output_console_message(_outputConfig, "Sending exception_trace_enable to OpenOCD via telnet" EOL);
                }
            }
            else if (_outputConfig && _outputConfig->mode == OUTPUT_CONSOLE)
            {
                output_console_message(_outputConfig, "Warning: Telnet port not configured, cannot enable HW exception trace" EOL);
            }
        }
        else
        {
            if (_outputConfig && _outputConfig->mode == OUTPUT_CONSOLE)
            {
                output_console_message(_outputConfig, "Exception output DISABLED (use -E to enable)" EOL);
            }
            if ( options.telnetPort > 0 )
            {
                telnet_configure_exception_trace(false);
                if (_outputConfig && _outputConfig->mode == OUTPUT_CONSOLE)
                {
                    output_console_message(_outputConfig, "Sending exception_trace_disable to OpenOCD via telnet" EOL);
                }
            }
        }
        
        /* Reinitialize RTOS on ITM reconnection */
        _reinitializeRTOS();

        thisTime = _r.lastReportus = _timestamp();

        while ( !_r.ending )
        {
            /* Check for keyboard input using select() */
            fd_set readfds;
            struct timeval key_tv = {0, 0};  /* Non-blocking check */
            FD_ZERO(&readfds);
            FD_SET(STDIN_FILENO, &readfds);
            
            if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &key_tv) > 0)
            {
                char key = getchar();
                
                /* Handle sort key commands */
                switch (key)
                {
                    case 't':  /* Sort by TCB address */
                        options.rtosSort = "tcb";
                        genericsReport(V_INFO, "Sorting by TCB address" EOL);
                        break;
                    case 'c':  /* Sort by current CPU usage */
                        options.rtosSort = "cpu";
                        genericsReport(V_INFO, "Sorting by CPU usage" EOL);
                        break;
                    case 'm':  /* Sort by maximum CPU usage */
                        options.rtosSort = "maxcpu";
                        genericsReport(V_INFO, "Sorting by maximum CPU usage" EOL);
                        break;
                    case 'n':  /* Sort by thread name */
                        options.rtosSort = "name";
                        genericsReport(V_INFO, "Sorting by thread name" EOL);
                        break;
                    case 'f':  /* Sort by function name */
                        options.rtosSort = "func";
                        genericsReport(V_INFO, "Sorting by function name" EOL);
                        break;
                    case 'p':  /* Sort by priority */
                        options.rtosSort = "priority";
                        genericsReport(V_INFO, "Sorting by priority" EOL);
                        break;
                    case 's':  /* Sort by context switches */
                        options.rtosSort = "switches";
                        genericsReport(V_INFO, "Sorting by context switches" EOL);
                        break;
                    case 'r':  /* Reset maximum CPU values */
                        if (_r.rtos) {
                            struct rtosThread *thread, *tmp;
                            HASH_ITER(hh, _r.rtos->threads, thread, tmp) {
                                thread->max_cpu_percent = 0;
                            }
                            _r.rtos->max_cpu_usage = 0;
                            genericsReport(V_INFO, "Reset all maximum CPU values" EOL);
                        }
                        break;
                }
            }
            
            remainTime = ( ( _r.lastReportus + options.displayInterval - thisTime ) );

            if ( remainTime > 0 )
            {
                tv.tv_sec = remainTime / 1000000;
                tv.tv_usec  = remainTime % 1000000;
                receiveResult = stream->receive( stream, cbw, TRANSFER_SIZE, &tv, &receivedSize );
            }
            else
            {
                receiveResult = RECEIVE_RESULT_OK;
                receivedSize = 0;
            }

            thisTime = _timestamp();

            if ( receiveResult == RECEIVE_RESULT_ERROR )
            {
                /* Something went wrong in the receive */
                break;
            }

            if ( receiveResult == RECEIVE_RESULT_EOF )
            {
                /* We are at EOF, hopefully next loop will get more data. */
            }

            /* Check to make sure our symbols are still appropriate */
            if ( !SymbolSetValid( &_r.s, options.elffile ) )
            {
                /* Make sure old references are invalidated */

                r = SymbolSetCreate( &_r.s, options.elffile, NULL, options.demangle, true, true, options.odoptions );

                switch ( r )
                {
                    case SYMBOL_NOELF:
                        genericsReport( V_WARN, "Elf file or symbols in it not found" EOL );
                        break;

                    case SYMBOL_NOOBJDUMP:
                        genericsExit( -1, "No objdump found" EOL );
                        break;

                    case SYMBOL_UNSPECIFIED:
                        genericsExit( -1, "Unknown error in symbol subsystem" EOL );
                        break;

                    default:
                        break;
                }

                if ( SYMBOL_NOELF == r )
                {
                    usleep( 1000000L );
                    continue;
                }

                genericsReport( V_WARN, "Loaded %s" EOL, options.elffile );

                if ( _r.s )
                {
                    genericsReport( V_INFO, "Files:      %d" EOL "Functions: %d" EOL "Source:    %d" EOL, _r.s->fileCount, _r.s->functionCount, _r.s->sourceCount );
                }
            }



            if ( receivedSize )
            {
                if ( PROT_OFLOW == options.protocol )
                {
                    OFLOWPump( &_r.c, cbw, receivedSize, _OFLOWpacketRxed, &_r );
                }
                else
                {
                    /* Pump all of the data through the protocol handler */
                    uint8_t *c = cbw;

                    while ( receivedSize > 0 )
                    {
                        _itmPumpProcess( *c++ );
                        receivedSize--;
                    }
                }
            }

            /* See if its time to post-process it */
            if ( receiveResult == RECEIVE_RESULT_TIMEOUT || remainTime <= 0 )
            {
                _processOutput( thisTime );

                /* ...and zero the exception records */
                for ( uint32_t e = 0; e < MAX_EXCEPTIONS; e++ )
                {
                    _r.er[e].visits = _r.er[e].maxDepth = _r.er[e].totalTime = _r.er[e].minTime = _r.er[e].maxTime = _r.er[e].maxWallTime = 0;
                }

                /* ... and the event counters */
                for ( uint32_t i = 0; i < DWT_NUM_EVENTS; i++ )
                {
                    _r.dwt_event_acc[i] = 0;
                }


                /* It's safe to update these here because the ticks won't be updated until more
                 * records arrive. */
                if ( _r.ITMoverflows != ITMDecoderGetStats( &_r.i )->overflow )
                {
                    /* We had an overflow, so can't safely track max depth ... reset it */
                    _r.erDepth = 0;
                }

                _r.ITMoverflows = ITMDecoderGetStats( &_r.i )->overflow;
                _r.SWPkt = ITMDecoderGetStats( &_r.i )->SWPkt;
                _r.TSPkt = ITMDecoderGetStats( &_r.i )->TSPkt;
                _r.HWPkt = ITMDecoderGetStats( &_r.i )->HWPkt;
                _r.lastReportus =  thisTime;
                _r.lastReportTicks = _r.timeStamp;

                /* Check to make sure there's not an unexpected TPIU in here */
                if ( ITMDecoderGetStats( &_r.i )->tpiuSyncCount )
                {
                    genericsReport( V_WARN, "Got a TPIU sync while decoding ITM...did you miss a -t option?" EOL );
                    ITMDecoderGetStats( &_r.i )->tpiuSyncCount = 0;
                }
            }

        }

        stream->close( stream );
        free( stream );
    }

    if ( !_r.ending && ( !ITMDecoderGetStats( &_r.i )->tpiuSyncCount ) )
    {
        genericsReport( V_ERROR, "Read failed" EOL );
    }

    /* Restore terminal settings before exit */
    if (terminal_modified) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
        terminal_modified = false;
    }

    return -ESRCH;
}
// ====================================================================================================
