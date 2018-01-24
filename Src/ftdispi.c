/**
 * @file ftdispi.c
 *
 * @author Stany MARCEL <stanypub@gmail.com>
 *
 * @brief libftdispi permits to use FTDI component in SPI master
 * mode. Require libftdi
 *
 * BSD License
 *
 * Copyright ©2010, Stany MARCEL <stanypub@gmail.com> All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * Neither the name of the owner nor the names of its contributors may
 * be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include "ftdispi.h"

#define BBMODE_NORMAL 1
#define BBMODE_SPI    2

/* Clock X5 Command for H class component */
#define TCK_X5 0x8a

#define BIT_P_CS 0x08
#define BIT_P_DI 0x04
#define BIT_P_DO 0x02
#define BIT_P_SK 0x01
#define BIT_P_G0 FTDISPI_GPO0
#define BIT_P_G1 FTDISPI_GPO1
#define BIT_P_G2 FTDISPI_GPO2
#define BIT_P_G3 FTDISPI_GPO3
#define BIT_P_GX (FTDISPI_GPO0|FTDISPI_GPO1|FTDISPI_GPO2|FTDISPI_GPO3)

#define BIT_DIR (BIT_P_SK|BIT_P_DO|BIT_P_CS|BIT_P_G0|BIT_P_G1|/*BIT_P_G2|*/BIT_P_G3)

#define SPI_MAX_MSG_SIZE (64*1024)
#define DEFAULT_MEM_SIZE ((SPI_MAX_MSG_SIZE) + 9)

#define RETRY_MAX       10
#define RETRY_TIME      1000

#define FTDI_CHECK(FUN, MSG, CTX) do {              \
        if ((FUN) < 0)                                  \
        {                                               \
            fprintf(stderr,             \
                    "%s: %s\n",         \
                    MSG,                \
                    ftdi_get_error_string(&CTX));   \
            return FTDISPI_ERROR_LIB;       \
        }                                               \
    } while (0)

#define ASSERT_CHECK(TEST, MSG, RVAL) do {      \
        if ((TEST))                             \
        {                                       \
            fprintf(stderr,         \
                    "ASSERT: %s\n", MSG);   \
            return RVAL;            \
        } } while (0)

static int ftdispi_realloc( struct ftdispi_context *fsc, size_t size );
static int ftdispi_wait( struct ftdispi_context *fsc, uint8_t mask,
                         uint8_t value, int maxtry );

__dll int ftdispi_open( struct ftdispi_context *fsc,
                        struct ftdi_context *fc, int interface )
{
    ASSERT_CHECK( !fc || !fsc, "CTX NOT INITIALIZED", FTDISPI_ERROR_CTX );
    memset( fsc, 0, sizeof( *fsc ) );
    memcpy( &fsc->fc, fc, sizeof( *fc ) );

    if ( 0 != ( fsc->mem = malloc( DEFAULT_MEM_SIZE ) ) )
    {
        fsc->memsize = DEFAULT_MEM_SIZE;
    }

    FTDI_CHECK( ftdi_write_data_set_chunksize( &fsc->fc, 512 ),
                "SET CHUNK 512", fsc->fc );
    FTDI_CHECK( ftdi_set_interface( &fsc->fc, INTERFACE_A ), "SET INT",
                fsc->fc );
    FTDI_CHECK( ftdi_usb_reset( &fsc->fc ), "RESET", fsc->fc );
    FTDI_CHECK( ftdi_set_latency_timer( &fsc->fc, 1 ), "SET LAT 1ms", fsc->fc );
    FTDI_CHECK( ftdi_setflowctrl( &fsc->fc, SIO_RTS_CTS_HS ), "RTS/CTS",
                fsc->fc );
    /*FTDI_CHECK(ftdi_set_bitmode(&fsc->fc, 0, 0), "RESET MPSSE", fsc->fc); */
    FTDI_CHECK( ftdi_set_bitmode( &fsc->fc, 0, BBMODE_SPI ), "SET SPI MODE",
                fsc->fc );
    fsc->wr_cmd = MPSSE_DO_WRITE;
    fsc->rd_cmd = MPSSE_DO_READ | MPSSE_READ_NEG;
    fsc->bitini = BIT_P_CS;

    FTDI_CHECK( ftdi_usb_purge_buffers( &fsc->fc ), "PURGE", fsc->fc );

    return FTDISPI_ERROR_NONE;
}

__dll int ftdispi_setclock( struct ftdispi_context *fsc, uint32_t speed )
{
    uint8_t buf[3] = { 0, 0, 0 };
    uint32_t div;
    uint32_t base;

    ASSERT_CHECK( !fsc, "CTX NOT INITIALIZED", FTDISPI_ERROR_CTX );

    if ( speed > CLOCK_MAX_SPEEDX5 || speed < CLOCK_MIN_SPEED )
    {
        return FTDISPI_ERROR_CLK;
    }

    if ( speed > CLOCK_MAX_SPEED )
    {
        /* TODO check if the device can support this */
        base = CLOCK_MAX_SPEEDX5;
    }
    else
    {
        base = CLOCK_MAX_SPEED;
    }

    div = ( base / speed ) - 1;

    if ( div > 0xFFFF )
    {
        return FTDISPI_ERROR_CLK;
    }

    if ( base == CLOCK_MAX_SPEEDX5 )
    {
        buf[0] = TCK_X5;
        FTDI_CHECK( ftdi_write_data( &fsc->fc, buf, 1 ), "SET CLK X5",
                    fsc->fc );
    }

    buf[0] = TCK_DIVISOR;
    buf[1] = ( div >> 0 ) & 0xFF;
    buf[2] = ( div >> 8 ) & 0xFF;
    FTDI_CHECK( ftdi_write_data( &fsc->fc, buf, 3 ), "SET CLK DIV", fsc->fc );
    return FTDISPI_ERROR_NONE;
}

__dll int ftdispi_setloopback( struct ftdispi_context *fsc, int active )
{
    uint8_t buf[1] = { 0 };

    ASSERT_CHECK( !fsc, "CTX NOT INITIALIZED", FTDISPI_ERROR_CTX );

    if ( active )
    {
        buf[0] = LOOPBACK_START;
        FTDI_CHECK( ftdi_write_data( &fsc->fc, buf, 1 ), "SET LOOP",
                    fsc->fc );
    }
    else
    {
        buf[0] = LOOPBACK_END;
        FTDI_CHECK( ftdi_write_data( &fsc->fc, buf, 1 ), "SET NO LOOP",
                    fsc->fc );
    }

    return FTDISPI_ERROR_NONE;
}

__dll int ftdispi_setmode( struct ftdispi_context *fsc,
                           int csh,
                           int cpol,
                           int cpha, int lsbfirst, int bitmode, int gpoini )
{
    uint8_t buf[3] = { 0, 0, 0 };

    ASSERT_CHECK( !fsc, "CTX NOT INITIALIZED", FTDISPI_ERROR_CTX );
    fsc->wr_cmd = MPSSE_DO_WRITE | ( bitmode ? MPSSE_BITMODE : 0 );
    fsc->rd_cmd = MPSSE_DO_READ | ( bitmode ? MPSSE_BITMODE : 0 );
    fsc->bitini = ( csh ? BIT_P_CS : 0 ) | ( BIT_P_GX & gpoini );

    if ( !cpol )
    {
        /* CLK IDLE = 0 */
        if ( !cpha )
        {
            /* W=FE R=RE => NO TX */
            fsc->wr_cmd |= MPSSE_WRITE_NEG;
        }
        else
        {
            /* W=RE R=FE > RX OPT */
            fsc->rd_cmd |= MPSSE_READ_NEG;
        }
    }
    else
    {
        /* CLK IDLE == 1 */
        fsc->bitini |= BIT_P_SK;

        if ( !cpha )
        {
            /* W=RE R=FE => NO TX */
            fsc->rd_cmd |= MPSSE_READ_NEG;
        }
        else
        {
            /* W=FE R=RE => RX OPT */
            fsc->wr_cmd |= MPSSE_WRITE_NEG;
        }
    }

    if ( lsbfirst )
    {
        fsc->wr_cmd |= MPSSE_LSB;
        fsc->rd_cmd |= MPSSE_LSB;
    }
    else
    {
        fsc->wr_cmd &= ~MPSSE_LSB;
        fsc->rd_cmd &= ~MPSSE_LSB;
    }

    buf[0] = SET_BITS_LOW;
    buf[1] = fsc->bitini;
    buf[2] = BIT_DIR;
    FTDI_CHECK( ftdi_write_data( &fsc->fc, buf, 3 ), "WR INI", fsc->fc );

    if ( ftdispi_wait( fsc, BIT_P_CS | BIT_P_GX, fsc->bitini, RETRY_MAX ) )
    {
        /* I still don't know why sometime the command must be resent */
        FTDI_CHECK( ftdi_write_data( &fsc->fc, buf, 3 ), "WR INI",
                    fsc->fc );
    }

    return ftdispi_wait( fsc, BIT_P_CS | BIT_P_GX, fsc->bitini, RETRY_MAX );
}

__dll int ftdispi_write_read( struct ftdispi_context *fsc,
                              const void *wbuf,
                              uint16_t wcount,
                              void *rbuf, uint16_t rcount, uint8_t gpo )
{
    int i, n, r;

    ASSERT_CHECK( !fsc, "CTX NOT INITIALIZED", FTDISPI_ERROR_CTX );
    ASSERT_CHECK( !( ( wbuf && wcount ) || ( rbuf && rcount ) ),
                  "NO CMD", FTDISPI_ERROR_CMD );
    n = wcount + ( rcount ? 9 : 6 );
    ASSERT_CHECK( ftdispi_realloc( fsc, n ), "REALLOC", FTDISPI_ERROR_MEM );

    i = 0;
    fsc->mem[i++] = SET_BITS_LOW;
    fsc->mem[i++] = ( ( 0x0F & ( fsc->bitini ^ BIT_P_CS ) ) | ( BIT_P_GX & gpo ) );
    fsc->mem[i++] = BIT_DIR;

    if ( wcount && wbuf )
    {
        fsc->mem[i++] = fsc->wr_cmd;
        fsc->mem[i++] = ( wcount - 1 ) & 0xFF;
        fsc->mem[i++] = ( ( wcount - 1 ) >> 8 ) & 0xFF;
        memcpy( fsc->mem + i, wbuf, wcount );
        i += wcount;
    }

    if ( rcount && rbuf )
    {
        fsc->mem[i++] = fsc->rd_cmd;
        fsc->mem[i++] = ( rcount - 1 ) & 0xFF;
        fsc->mem[i++] = ( ( rcount - 1 ) >> 8 ) & 0xFF;
        FTDI_CHECK( ftdi_write_data( &fsc->fc, fsc->mem, i ), "[WR]RD",
                    fsc->fc );

        for ( n = 0; n < rcount; )
        {
            FTDI_CHECK( r =
                                    ftdi_read_data( &fsc->fc, rbuf + n,
                                                    rcount - n ), "RD", fsc->fc );
            n += r;
        }

        i = 0;
    }

    fsc->mem[i++] = SET_BITS_LOW;
    fsc->mem[i++] = fsc->bitini;
    fsc->mem[i++] = BIT_DIR;
    FTDI_CHECK( ftdi_write_data( &fsc->fc, fsc->mem, i ), "WR", fsc->fc );

    return ftdispi_wait( fsc, BIT_P_CS, fsc->bitini, RETRY_MAX );
}

__dll int ftdispi_write( struct ftdispi_context *fsc,
                         const void *buf, uint16_t count, uint8_t gpo )
{
    return ftdispi_write_read( fsc, buf, count, 0, 0, gpo );
}

__dll int ftdispi_read( struct ftdispi_context *fsc,
                        void *buf, uint16_t count, uint8_t gpo )
{
    return ftdispi_write_read( fsc, 0, 0, buf, count, gpo );
}

__dll int ftdispi_setgpo( struct ftdispi_context *fsc, uint8_t gpo )
{
    uint8_t buf[3];

    ASSERT_CHECK( !fsc, "CTX NOT INITIALIZED", FTDISPI_ERROR_CTX );

    if ( ( fsc->bitini & BIT_P_GX ) != ( gpo & BIT_P_GX ) )
    {
        fsc->bitini = ( ( fsc->bitini & BIT_P_GX ) | ( gpo & BIT_P_GX ) );
    }

    buf[0] = SET_BITS_LOW;
    buf[1] = fsc->bitini;
    buf[2] = BIT_DIR;
    FTDI_CHECK( ftdi_write_data( &fsc->fc, buf, 3 ), "SETGPO", fsc->fc );
    return ftdispi_wait( fsc, BIT_P_GX, fsc->bitini, RETRY_MAX );
}

__dll int ftdispi_close( struct ftdispi_context *fsc, int close_ftdi )
{
    ASSERT_CHECK( !fsc, "CTX NOT INITIALIZED", FTDISPI_ERROR_CTX );

    if ( fsc->mem )
    {
        free( fsc->mem );
        fsc->mem = 0;
        fsc->memsize = 0;
    }

    if ( close_ftdi )
    {
        ftdi_usb_close( &fsc->fc );
        ftdi_deinit( &fsc->fc );
    }

    return FTDISPI_ERROR_NONE;
}

static int ftdispi_realloc( struct ftdispi_context *fsc, size_t size )
{
    uint8_t *p;

    if ( fsc->memsize < size )
    {
        if ( !( p = realloc( fsc->mem, size ) ) )
        {
            return FTDISPI_ERROR_MEM;
        }

        fsc->mem = p;
        fsc->memsize = size;
    }

    return FTDISPI_ERROR_NONE;
}

static int ftdispi_wait( struct ftdispi_context *fsc, uint8_t mask,
                         uint8_t value, int maxtry )
{
    uint8_t cmd = GET_BITS_LOW;
    uint8_t ret = 0;

    FTDI_CHECK( ftdi_write_data( &fsc->fc, &cmd, 1 ), "GBLW", fsc->fc );
    FTDI_CHECK( ftdi_read_data( &fsc->fc, &ret, 1 ), "GBLR", fsc->fc );

    while ( maxtry-- && ( ret & mask ) != ( value & mask ) )
    {
        usleep( RETRY_TIME );
        FTDI_CHECK( ftdi_write_data( &fsc->fc, &cmd, 1 ), "GBLW", fsc->fc );
        FTDI_CHECK( ftdi_read_data( &fsc->fc, &ret, 1 ), "GBLR", fsc->fc );
    }

    if ( ( ret & mask ) == ( value & mask ) )
    {
        return FTDISPI_ERROR_NONE;
    }
    else
    {
        return FTDISPI_ERROR_TO;
    }
}
