/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef _MSG_DECODER_
#define _MSG_DECODER_

#ifdef __cplusplus
extern "C" {
#endif

/* Do not change the order of existing message types! */
enum MSGType
{
    MSG_UNKNOWN,
    MSG_RESERVED,
    MSG_ERROR,
    MSG_NONE,
    MSG_SOFTWARE,
    MSG_NISYNC,
    MSG_OSW,
    MSG_DATA_ACCESS_WP,
    MSG_DATA_RWWP,
    MSG_PC_SAMPLE,
    MSG_DWT_EVENT,
    MSG_EXCEPTION,
    MSG_TS,

    /* Add new message types here */

    MSG_NUM_MSGS
};

/* Generic message with no content */
struct genericMsg
{
    enum MSGType msgtype;
    uint64_t ts;
};

struct TSMsg
{
    enum MSGType msgtype;
    uint64_t ts;
    uint8_t timeStatus;
    uint32_t timeInc;
};

/* Software message */
struct swMsg
{
    enum MSGType msgtype;
    uint64_t ts;
    uint8_t srcAddr;
    uint8_t len;
    uint32_t value;
};

/* NISYNC message */
struct nisyncMsg
{
    enum MSGType msgtype;
    uint64_t ts;
    uint8_t type;
    uint32_t addr;
};

/* PC sample message */
struct pcSampleMsg
{
    enum MSGType msgtype;
    uint64_t ts;
    bool sleep;
    uint32_t pc;
};

/*  offset write message */
struct oswMsg
{
    enum MSGType msgtype;
    uint64_t ts;
    uint8_t comp;
    uint32_t offset;
};

/* watchpoint */
struct wptMsg
{
    enum MSGType msgtype;
    uint64_t ts;
    uint8_t comp;
    uint32_t data;
};

/* watch pointer */
struct watchMsg
{
    enum MSGType msgtype;
    uint64_t ts;
    uint8_t comp;
    bool isWrite;
    uint32_t data;
};

/* DWT event */
struct dwtMsg
{
    enum MSGType msgtype;
    uint64_t ts;
    uint8_t event;
};

/* Exception */
struct excMsg
{
    enum MSGType msgtype;
    uint64_t ts;
    uint32_t exceptionNumber;
    uint8_t eventType;
};


struct msg
{
    /* ...all the possible types of message that could be conveyed */
    union
    {
        struct genericMsg genericMsg;
        struct swMsg swMsg;
        struct nisyncMsg nisyncMsg;
        struct oswMsg oswMsg;
        struct wptMsg wptMsg;
        struct watchMsg watchMsg;
        struct dwtMsg dwtMsg;
        struct excMsg excMsg;
        struct pcSampleMsg pcSampleMsg;
    };
};

struct ITMPacket;

// ====================================================================================================

bool msgDecoder( struct ITMPacket *packet, struct msg *decoded );

// ====================================================================================================
#ifdef __cplusplus
}
#endif

#endif
