#ifndef _ORBLCD_PROTOCOL_H_
#define _ORBLCD_PROTOCOL_H_

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_DATA_CHANNEL    (28)
#define LCD_COMMAND_CHANNEL (LCD_DATA_CHANNEL+1)

#define ORBLCD_DEPTH_1  (0)
#define ORBLCD_DEPTH_8  (1)
#define ORBLCD_DEPTH_16 (2)
#define ORBLCD_DEPTH_24 (3)

#define ORBLCD_CMD_INIT_LCD     (1)
#define ORBLCD_CMD_CLOSE_SCREEN (2)
#define ORBLCD_CMD_CLEAR        (3)
#define ORBLCD_CMD_GOTOXY       (4)

#define ORBLCD_ENCODE_X(x) ((x&0xfff)<<0)
#define ORBLCD_ENCODE_Y(x) ((x&0xfff)<<12)
#define ORBLCD_ENCODE_D(x) ((x&0x03)<<24)
#define ORBLCD_ENCODE_C(x) ((x&0x3f)<<26)
#define ORBLCD_DECODE_X(x) ((x>>0)&0xfff)
#define ORBLCD_DECODE_Y(x) ((x>>12)&0xfff)
#define ORBLCD_DECODE_D(x) ((x>>24)&0x03)
#define ORBLCD_DECODE_C(x) ((x>>26)&0x3f)

#define ORBLCD_GET_DEPTH(x)        (((int[]){1,8,16,24})[ORBLCD_DECODE_D(x)])
#define ORBLCD_PIXELS_PER_WORD(x)  (((int[]){32,4,2,1})[ORBLCD_DECODE_D(x)])

#define ORBLCD_OPEN_SCREEN(x,y,d) (ORBLCD_ENCODE_C(ORBLCD_CMD_INIT_LCD)|ORBLCD_ENCODE_D(d)|ORBLCD_ENCODE_X(x)|ORBLCD_ENCODE_Y(y))
#define ORBLCD_CLOSE_SCREEN       (ORBLCD_ENCODE_C(ORBLCD_CMD_CLOSE_SCREEN))
#define ORBLCD_CLEAR              (ORBLCD_ENCODE_C(ORBLCD_CMD_CLEAR))

#ifdef __cplusplus
}
#endif
#endif
