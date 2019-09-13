/**
 * @file ftdispi.h
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

#ifndef __FTDISPI_H__
#define __FTDISPI_H__

#include <libftdi1/ftdi.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#ifdef WIN32
    #ifdef EXPORT_DLL
        #define __dll __declspec(dllexport)
    #else
        #define __dll __declspec(dllimport)
    #endif
#else
    #define __dll
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @brief returned when no error */
#define FTDISPI_ERROR_NONE 0
/** @brief returned when a function is called with a non/bad init context */
#define FTDISPI_ERROR_CTX -1
/** @brief returned when a command is imposible */
#define FTDISPI_ERROR_CMD -2
/** @brief returned on allocation problems */
#define FTDISPI_ERROR_MEM -3
/** @brief returned on libftdi error */
#define FTDISPI_ERROR_LIB -4
/** @brief returned on clock error */
#define FTDISPI_ERROR_CLK -5
/** @brief returned on timeout error */
#define FTDISPI_ERROR_TO  -6

#define CLOCK_MAX_SPEEDX5 30000000  /**< @brief Clock max speed in Hz for H class device */
#define CLOCK_MAX_SPEED    6000000  /**< @brief Clock max speed in Hz */
#define CLOCK_MIN_SPEED        100  /**< @brief Clock min speed in Hz */

#define FTDISPI_GPO0 0x10  /**< @brief General Purpose Output bits 0 (D4) */
#define FTDISPI_GPO1 0x20  /**< @brief General Purpose Output bits 1 (D5) */
#define FTDISPI_GPO2 0x40  /**< @brief General Purpose Output bits 2 (D6) */
#define FTDISPI_GPO3 0x80  /**< @brief General Purpose Output bits 3 (D7) */

/**
 * @brief FDTI SPI context
 */
struct ftdispi_context
{
    struct ftdi_context fc;
    /**< @brief ftdi context */
    uint8_t wr_cmd;  /**< @brief write command */
    uint8_t rd_cmd;  /**< @brief read command */
    uint8_t bitini;  /**< @brief initial states of all bits */
    uint8_t *mem;    /**< @brief memory region for write and read
                                  * functions */
    size_t memsize;  /**< @brief size of the memory region for write
                                  * and read functions */
};

/**
 * @brief Open the a FTDI interface in SPI mode.
 *
 * @param[in,out] fsc the spi context to open
 * @param[in] fc a previously opened usb ftdi device
 * @param[in] interface the interface to use
 *
 * @retval #FTDISPI_ERROR_NONE on success
 * @retval #FTDISPI_ERROR_CTX on context error
 * @retval #FTDISPI_ERROR_LIB on libftdi error
 *
 * @sa ftdi_init
 * @sa ftdi_usb_open
 */
__dll int
ftdispi_open( struct ftdispi_context *fsc,
              struct ftdi_context *fc, int interface );

/**
 * @brief Set the mode for future SPI operations. Returns only when
 * GPO and CS are in wanted state.
 *
 * ftdispi_open set a default mode to CSH=1, CPOL=0, CPHA=1, MSB first.
 *
 * @param[in,out] fsc previously opened spi contex
 * @param[in] csh idle state of chip select
 * @param[in] cpol idle state of clock
 * @param[in] cpha read and write edge settings
 * @param[in] lsbfirst if set data are serialized LSB first
 * @param[in] bitmode set to 1 for mpsse bit mode active
 * @param[in] gpoini initial state of General Purpose Output
 * (#FTDISPI_GPO0 | #FTDISPI_GPO1 |#FTDISPI_GPO2 |#FTDISPI_GPO3)
 *
 * - CPOL=0 CPHA=0 => Mode 0, W=FE R=RE
 * - CPOL=0 CPHA=1 => Mode 1, W=RE R=FE (CLK inverted at each byte for unknown reason)
 * - CPOL=1 CPHA=0 => Mode 2, W=RE R=FE (CLK inverted at each byte for unknown reason)
 * - CPOL=1 CPHA=1 => Mode 3, W=FE R=RE
 *
 * W: Write,
 * R: Read,
 * RE: Rising Edge,
 * FE: Falling Edge,
 *
 * WFE means data clocked out on Falling Edge
 * RRE means data read on Rising Edge
 *
 * @retval #FTDISPI_ERROR_NONE on success
 * @retval #FTDISPI_ERROR_CTX on context error
 * @retval #FTDISPI_ERROR_TO on timeout error
 *
 * @sa ftdispi_open
 */
__dll int
ftdispi_setmode( struct ftdispi_context *fsc,
                 int csh,
                 int cpol,
                 int cpha,
                 int lsbfirst,
                 int bitmode,
                 int gpoini );

/**
 * @brief Set the clock divisor from the given speed.
 *
 * @param[in,out] fsc previously opened spi contex
 * @param[in] speed the wanted in Hz from #CLOCK_MIN_SPEED to
 * #CLOCK_MAX_SPEEDX5
 *
 * For the moment no check are done to verify that your device support
 * the max speed x 5
 *
 * @retval #FTDISPI_ERROR_NONE on success
 * @retval #FTDISPI_ERROR_CTX on context error
 * @retval #FTDISPI_ERROR_LIB on libftdi error
 */
__dll int
ftdispi_setclock( struct ftdispi_context *fsc, uint32_t speed );

/**
 * @brief set the loopback for TDI/DO TDO/DI
 *
 * @param[in,out] fsc previously opened spi contex
 * @param[in] active set to 0 to diable loopback
 *
 * @retval #FTDISPI_ERROR_NONE on success
 * @retval #FTDISPI_ERROR_CTX on context error
 * @retval #FTDISPI_ERROR_LIB on libftdi error
 */
__dll int
ftdispi_setloopback( struct ftdispi_context *fsc, int active );

/**
 * @brief Set general purpose output and then write from wbuf to spi
 * and read from spi to rbuf. Returns only when CS get back to idle
 * state.
 *
 * @param[in,out] fsc previously opened spi contex
 * @param[in] wbuf buffer to write
 * @param[in] wcount number of bytes to write
 * @param[out] rbuf buffer to read to
 * @param[in] rcount number of bytes to read
 * @param[in] gpo general purpose output states for the duration of
 * the operation
 *
 * @retval #FTDISPI_ERROR_NONE on success
 * @retval #FTDISPI_ERROR_CTX on context error
 * @retval #FTDISPI_ERROR_LIB on libftdi error
 * @retval #FTDISPI_ERROR_MEM on allocation error
 * @retval #FTDISPI_ERROR_CMD on invalid parameters
 * @retval #FTDISPI_ERROR_TO on timeout error
 */
__dll int
ftdispi_write_read( struct ftdispi_context *fsc,
                    const void *wbuf,
                    uint16_t wcount,
                    void *rbuf,
                    uint16_t rcount,
                    uint8_t gpo );

/**
 * @brief Set general purpose output and then write from buf to
 * spi. Returns only when CS get back to idle state.
 *
 * @param[in,out] fsc previously opened spi contex
 * @param[in] buf buffer to write
 * @param[in] count number of bytes to write
 * @param[in] gpo general purpose output states for the duration of
 * the operation
 *
 * @retval #FTDISPI_ERROR_NONE on success
 * @retval #FTDISPI_ERROR_CTX on context error
 * @retval #FTDISPI_ERROR_LIB on libftdi error
 * @retval #FTDISPI_ERROR_MEM on allocation error
 * @retval #FTDISPI_ERROR_CMD on invalid parameters
 * @retval #FTDISPI_ERROR_TO on timeout error
 */
__dll int
ftdispi_write( struct ftdispi_context *fsc,
               const void *buf,
               uint16_t count,
               uint8_t gpo );
/**
 * @brief Set general purpose output and then read from spi to
 * buf. Returns only when CS get back to idle state.
 *
 * @param[in,out] fsc previously opened spi contex
 * @param[out] buf buffer to read to
 * @param[in] count number of bytes to read
 * @param[in] gpo general purpose output states for the duration of
 * the operation
 *
 * @retval #FTDISPI_ERROR_NONE on success
 * @retval #FTDISPI_ERROR_CTX on context error
 * @retval #FTDISPI_ERROR_LIB on libftdi error
 * @retval #FTDISPI_ERROR_MEM on allocation error
 * @retval #FTDISPI_ERROR_CMD on invalid parameters
 * @retval #FTDISPI_ERROR_TO on timeout error
 */
__dll int
ftdispi_read( struct ftdispi_context *fsc,
              void *buf,
              uint16_t count,
              uint8_t gpo );

/**
 * @brief Set the new general purpose default state, and change the device
 * output accordingly. Returns only when GPO status have changed.
 *
 * @param[in,out] fsc previously opened spi contex
 * @param[in] gpo general purpose output new states
 *
 * @retval #FTDISPI_ERROR_NONE on success
 * @retval #FTDISPI_ERROR_CTX on context error
 * @retval #FTDISPI_ERROR_LIB on libftdi error
 * @retval #FTDISPI_ERROR_TO on timeout error
 */
__dll int
ftdispi_setgpo( struct ftdispi_context *fsc,
                uint8_t gpo );

/**
 * @brief Close the ftdispi context.
 *
 * @param[in,out] fsc previously opened spi contex
 * @param[in] close_ftdi if set the ftdi_context is also closed and deinit
 *
 * @retval #FTDISPI_ERROR_NONE on success
 * @retval #FTDISPI_ERROR_CTX on context error
 */
__dll int
ftdispi_close( struct ftdispi_context *fsc,
               int close_ftdi );

#ifdef __cplusplus
}
#endif
#endif
