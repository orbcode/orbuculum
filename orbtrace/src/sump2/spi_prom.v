/* ****************************************************************************
-- Source file: spi_prom.v           
-- Date:        June 2014
-- Author:      khubbard
-- Description: A generic LocalBus hardware interface to a SPI PROM that 
--              provides for a reusable software interface for configuring 
--              current and future SPI PROMs. Uses a single BRAM.
-- Language:    Verilog-2001 and VHDL-1993
-- Simulation:  Mentor-Modelsim 
-- Synthesis:   Xilinst-XST 
-- License:     This project is licensed with the CERN Open Hardware Licence
--              v1.2.  You may redistribute and modify this project under the
--              terms of the CERN OHL v.1.2. (http://ohwr.org/cernohl).
--              This project is distributed WITHOUT ANY EXPRESS OR IMPLIED
--              WARRANTY, INCLUDING OF MERCHANTABILITY, SATISFACTORY QUALITY
--              AND FITNESS FOR A PARTICULAR PURPOSE. Please see the CERN OHL
--              v.1.2 for applicable Conditions.
--
--                                --------------
--           reset           --->|  spi_prom.v  |
--           clk_lb          --->|              |
--           ck_divisor[7:0] --->|              |
--                               |              |
--           lb_cs_prom_c ------>|              |
--           lb_cs_prom_d ------>|              |
--           lb_wr ------------->|              |
--           lb_rd ------------->|              |---> lb_rd_rdy
--           lb_wr_d[31:0] ----->|              |---> lb_rd_d[31:0]
--                               |              |
--                               |              |---> spi_sck
--                               |              |---> spi_cs_l
--           spi_miso ---------->|              |---> spi_mosi
--                               |              |
--                               |              |---> reconfig_req
--                               |              |---> reconfig_addr[31:0]
--                                --------------
--
--  [ Control Register Write ]
--    D(7:0)                     D(27:8)
--      0x0 CMD_IDLE              NA         : NOP
--      0x1 CMD_QUERY_ID          NA         : Read PROM ID
--      0x2 CMD_RD_TIMESTAMP      NA         : Read FPGA UNIX Timestamp
--      0x3 CMD_RD_PROM           ADDR(27:8) : Read 256 Bytes from ADDR
--      0x4 CMD_UNLOCK            0x0AA5AA   : Unlock for Erase and Write Ops
--                                0x055A55   : Unlock for Reconfig Operation
--      0x5 CMD_ERASE_BULK        NA         : Erase entire PROM ( Slow )
--      0x6 CMD_ERASE_SECTOR      ADDR(27:8) : Erase Sector at ADDR
--      0x7 CMD_WR_PROM           ADDR(27:8) : Write 256 Bytes to ADDR
--      0x8 CMD_RECONFIG_FPGA     ADDR(27:8) : Reconfig FPGA from ADDR
--      0x9 CMD_RELEASE_POWERDOWN NA         : Release from Deep Power Down
--
--  [ Control Register Read ]
--    D(27:8) spi_addr[27:8]
--    D(7)    err_ram_wr_jk         : MOSI RAM Overwrite Error
--    D(6)    stat_polling_reqd_jk  : Polling is Required
--    D(5)    stat_mosi_buf_free    : MOSI buffer is available.
--    D(4)    stat_miso_buf_rdy_jk  : MISO buffer has data to be read.
--    D(3)    stat_fsm_wr           : Interface in "Write" state.
--    D(2)    stat_unlocked_jk      : PROM is unlocked for Writing and Erasing
--    D(1)    flag_wip              : PROM is Writing or Erasing
--    D(0)    stat_spi_busy         : SPI interface is in use
--
--
--  [ Data Register Read/Write ]
--    Up to 64 DWORD Burst Region for Writes and Reads
--
-- Resources Required in Spartan3A XC3S200A device:
--   Number of RAMB16BWEs:                   1 out of      16    6%
--  prom_is_32b = 1
--   Number of Slice Flip Flops:           410 out of   3,584   11%
--   Number of 4 input LUTs:               485 out of   3,584   13%
--  prom_is_32b = 0
--   Number of Slice Flip Flops:           410 out of   3,584   11%
--   Number of 4 input LUTs:               410 out of   3,584   11%
--
--
--  Note: ck_divisor[7:0] is number of clk_lb cycles per HALF spi_sck period.
--        Example clk_lb = 80 MHz, ck_divisor=10, spi_sck will be 4 MHz.
--
--  Note: Regarding 4-Byte Addressing. The 256Mb Micron datasheet is confusing.
--        There are special 4-Byte commands, but they only exist for certain
--        mask revs of their 256Mb parts ( N25Q256A83 ). All of the other
--        N25Q256A revs require the "Enter 4-Byte" command which then extends
--        the regular 3-Byte commands to 4-Bytes. This module issues the 
--        "Enter 4-Byte" at the beginning of ANY PROM command and then issues 
--        "Exit 4-Byte" after the command is done.
--
-- PROM Spec:
--   Page   = 256 Bytes
--   Sector = 256 pages ( 64 KB )
--   Sector Erase = ~500ms
--   Page Program = ~1ms
-- Measured Program Times:
--   256 0xFFs = 330 us
--   256 0x00s = 800 us
--    64 0x00s = 200 us
--    32 0x00s = 100 us
--     8 0x00s =   0 us
--
-- Revision History:
-- Ver#  When      Who      What
-- ----  --------  -------- ---------------------------------------------------
-- 0.1   06.01.14  khubbard Creation
-- 0.2   07.08.14  khubbard Added 4-byte addressing support
-- 0.3   08.18.14  khubbard Added root protection for Slot-0 BootLoader
-- ***************************************************************************/
`default_nettype none // Strictly enforce all nets to be declared
  
module spi_prom #
(
   parameter depth_len  = 128,
   parameter depth_bits = 7
// parameter depth_len  = 8,
// parameter depth_bits = 3
)
(
  input  wire         reset,
  input  wire         prom_is_32b,
  input  wire [31:0]  slot_size,
  input  wire         protect_1st_slot,
  input  wire         clk_lb,
  input  wire [7:0]   ck_divisor,
  input  wire [3:0]   spi_ctrl,

  input  wire         lb_cs_prom_c,
  input  wire         lb_cs_prom_d,
  input  wire         lb_wr,
  input  wire         lb_rd,
  input  wire [31:0]  lb_wr_d,
  output reg  [31:0]  lb_rd_d,
  output reg          lb_rd_rdy,
  output reg          flag_wip,
  input  wire         reconfig_2nd_slot,
  output reg          reconfig_req,
  output reg  [31:0]  reconfig_addr,
  output reg          bist_req,

  output wire         spi_sck,
  output wire         spi_cs_l,
  output wire         spi_mosi,
  input  wire         spi_miso
);// module spi_prom 

  wire [31:0]             all_zeros;
  wire [31:0]             all_ones;

  reg                     flag_wip_loc;
  reg                     xfer_start;
  reg                     xfer_start_p1;
  reg                     xfer_mosi_jk;
  reg  [11:0]             xfer_tx_bytes;
  reg  [11:0]             xfer_rx_bytes;

  reg  [3:0]              mosi_fsm;
  reg  [11:0]             spi_byte_cnt;
  reg  [7:0]              mosi_byte_d;
  reg                     mosi_byte_pre;
  reg                     mosi_byte_en;
  wire                    mosi_byte_req;
  reg                     mosi_byte_req_p1;
  reg                     mosi_buf_rdy_jk;
  reg  [1:0]              mosi_byte_cnt;

  wire [7:0]              miso_byte_d;
  wire                    miso_byte_rdy;
  reg  [1:0]              miso_byte_cnt;
  reg                     err_ram_wr;
  reg                     err_ram_wr_jk;
  reg                     bist_req_jk;
  reg                     req_timestamp;
  reg                     req_timestamp_p1;
  reg                     req_timestamp_p2;

  wire                    spi_is_idle;
  reg                     spi_is_idle_p1;
  reg                     spi_is_idle_jk;
  wire [11:0]             page_size;
  reg  [31:0]             spi_addr;
  reg                     spi_addr_page_inc;
  reg                     spi_addr_page_inc_p1;
  reg                     cmd_en;
  reg                     cmd_en_p1;
  reg  [3:0]              cmd_nib;
  reg  [7:0]              cmd_byte;

  reg  [31:0]             ram_array[depth_len-1:0];
  reg  [depth_bits-1:0]   a_addr;
  reg  [depth_bits-1:0]   a_addr_p1;
  reg  [depth_bits-1:0]   b_addr;
  reg                     a_we;
  reg                     a_we_p1;
  reg  [31:0]             a_di;
  reg  [31:0]             a_di_p1;
  reg                     a_we_miso;
  reg  [31:0]             a_di_miso;
  reg  [31:0]             a_do;
  reg  [31:0]             b_do;
  reg  [31:0]             b_do_p1;

  reg  [31:0]             prom_ctrl_reg;
  reg  [31:0]             prom_data_reg;
  wire [31:0]             time_stamp_d;
  reg  [7:0]              post_rst_cnt;

  reg                     strb_clear_fsm;
  reg                     strb_wr_data;
  reg                     miso_rd_inc;
  reg                     mosi_rd_inc;
  reg                     wr_ping_pong;
  reg                     rd_ping_pong_pend;
  reg  [1:0]              rd_ping_pong_actv;
  reg                     buffer_free_jk;
  reg                     prom_wr_locked_jk;
  reg                     prom_wr_locked_jk_p1;
  reg                     fpga_boot_locked_jk;
  reg                     fpga_root_locked_jk;
  reg                     stat_spi_busy;
  reg                     stat_polling_reqd_jk;
  reg                     stat_mosi_buf_free;
  reg                     stat_miso_buf_rdy_jk;
  reg                     stat_fsm_wr;
  reg                     stat_unlocked_jk;
  reg                     we_reqd_jk;
  reg                     rd_reqd_jk;
  reg                     addr_reqd_jk;
  reg                     done_enter_4byte;
  reg                     in_reset;
  reg                     in_reset_p1;

  `define CMD_IDLE              4'h0
  `define CMD_QUERY_ID          4'h1
  `define CMD_RD_TIMESTAMP      4'h2
  `define CMD_RD_PROM           4'h3
  `define CMD_UNLOCK            4'h4
  `define CMD_ERASE_BULK        4'h5
  `define CMD_ERASE_SECTOR      4'h6
  `define CMD_WR_PROM           4'h7
  `define CMD_RECONFIG_FPGA     4'h8
  `define CMD_RELEASE_POWERDOWN 4'h9
  `define CMD_REL_PD            4'h9

  assign all_zeros = 32'h00000000;
  assign all_ones  = 32'hFFFFFFFF;

  assign page_size = ( 2*depth_len );// Number of Bytes in a Page ( 1/2 RAM )

//assign sump_events[15:12] = mosi_fsm[3:0];
//assign sump_events[11:8]  = cmd_nib[3:0];
//assign sump_events[7]     = err_ram_wr_jk;
//assign sump_events[6]     = stat_polling_reqd_jk;
//assign sump_events[5]     = stat_miso_buf_rdy_jk;
//assign sump_events[4]     = stat_mosi_buf_free;
//assign sump_events[3]     = stat_fsm_wr;
//assign sump_events[2]     = stat_unlocked_jk;
//assign sump_events[1]     = flag_wip_loc;
//assign sump_events[0]     = stat_spi_busy;

//assign slot_size = 32'h00020000; // 1 Mbit slots for XC3S200A

//-----------------------------------------------------------------------------
// PROM_CTRL_REG : 
// Write :
//  D(27:8) : Address(27:8) for Write or Read
//  D(7:0)  : Control Nibble:
//
// Read :
//     D(7)  : 1 = MOSI Buffer Overrun Error
//     D(6)  : 1 = MOSI Polling Required      ( Read D(4) before Writing )
//     D(5)  : 1 = MOSI Buffer Free           ( OK to Write 256 Bytes ) 
//     D(4)  : 1 = MISO Buffer Read Ready     ( OK to Read 256 Bytes )
//
//     D(3)  : 1 = Write Bursting State      
//     D(2)  : 1 = Unlocked for Writing
//     D(1)  : 1 = PROM Write In Progress     ( WIP )
//     D(0)  : 1 = SPI is Busy 
//
// PROM_DATA_REG : 
//    D(31:0) : Write or Read Data ( 64 DWORDs at a time for Write and Read )
//-----------------------------------------------------------------------------
always @ ( posedge clk_lb ) begin : proc_lb_regs
 begin
   bist_req         <= bist_req_jk;
   strb_clear_fsm   <= 0;
   strb_wr_data     <= 0;
   cmd_en           <= 0;
   cmd_en_p1        <= cmd_en;
   reconfig_req     <= 0;
   req_timestamp    <= 0;
   req_timestamp_p1 <= req_timestamp;
   req_timestamp_p2 <= req_timestamp_p1;
   in_reset         <= 0;
   in_reset_p1      <= in_reset;
  
   stat_unlocked_jk     <= ~ prom_wr_locked_jk;
   prom_wr_locked_jk_p1 <= prom_wr_locked_jk;

   if ( spi_addr_page_inc == 1 ) begin
     spi_addr <= spi_addr[31:0] + page_size[11:0];
   end

   // Process new Control Nibble when written
   if ( lb_wr == 1 && lb_cs_prom_c == 1 ) begin 
     prom_ctrl_reg  <= lb_wr_d[31:0];
     cmd_nib        <= lb_wr_d[3:0];
     cmd_en         <= 1;
     strb_clear_fsm <= 1;
     spi_addr[27:0]  <= { lb_wr_d[27:8], 8'd0 };
     spi_addr[31:28] <= 4'd0;

     if ( lb_wr_d[3:0] == `CMD_RD_TIMESTAMP ) begin
       req_timestamp <= 1;
       cmd_en        <= 0;
     end 

     if ( lb_wr_d[3:0] == `CMD_RECONFIG_FPGA ) begin
       cmd_en        <= 0;
       if ( fpga_boot_locked_jk == 0 ) begin
         reconfig_req         <= 1;
         reconfig_addr[27:0]  <= { lb_wr_d[27:8], 8'd0 };
         reconfig_addr[31:28] <= 4'd0;
       end
     end 

     // Handle the Lock. Lock on idle or bad unlock code
     if ( lb_wr_d[3:0] == 4'h0 ) begin
       prom_wr_locked_jk <= 1;
       cmd_en            <= 0;
     end

     if ( lb_wr_d[3:0] == `CMD_UNLOCK  ) begin
       fpga_boot_locked_jk <= 1;
       prom_wr_locked_jk   <= 1;
       bist_req_jk         <= 0;
       cmd_en              <= 0;
       if ( lb_wr_d[27:8] == 20'haa5aa ) begin
         prom_wr_locked_jk <= 0;
       end
       if ( lb_wr_d[27:8] == 20'h55a55 ) begin
         fpga_boot_locked_jk <= 0;
       end
       if ( lb_wr_d[27:8] == 20'h5aaa5 ) begin
         fpga_root_locked_jk <= 0;// Note: This is sticky
       end
       if ( lb_wr_d[27:8] == 20'h11111 ) begin
         bist_req_jk         <= 1;
       end
     end 

   end // if ( lb_wr == 1 && lb_cs_prom_c == 1 ) begin 

   // Load BRAM for MOSI
   if ( lb_wr == 1 && lb_cs_prom_d == 1 ) begin 
     prom_data_reg <= lb_wr_d[31:0];
     strb_wr_data  <= 1;
   end // if ( lb_wr == 1 && lb_cs_prom_d == 1 ) begin 
 
   // Leaving Reset, if reconfig_2nd_slot==1, request a reconfig at slot_size 
   if          ( post_rst_cnt == 8'hF0 ) begin
     if ( reconfig_2nd_slot == 1 ) begin
       reconfig_req         <= 1;
       reconfig_addr[27:0]  <= { slot_size[27:8], 8'd0 };
       reconfig_addr[31:28] <= 4'd0;
     end
     post_rst_cnt <= post_rst_cnt + 1;
   end else if ( post_rst_cnt == 8'hFF ) begin
   end else begin
     post_rst_cnt <= post_rst_cnt + 1;
   end 
  
   if ( reset == 1 ) begin
     post_rst_cnt        <= 8'd0;
     in_reset            <= 1;
     in_reset_p1         <= 1;
     prom_ctrl_reg       <= 32'd0;
     prom_data_reg       <= 32'd0;
     prom_wr_locked_jk   <= 1;
     fpga_boot_locked_jk <= 1; // Prevents Reconfig
     fpga_root_locked_jk <= 1; // Prevents Erasing Slot-0
     reconfig_req        <= 0;
     bist_req_jk         <= 0; // Request Built In Self Test mode
     bist_req            <= 0; 
   end // if ( reset == 1 ) begin

 end
end // proc_lb_regs


//-----------------------------------------------------------------------------
// LocalBus Readback
//-----------------------------------------------------------------------------
always @ ( posedge clk_lb ) begin : proc_lb_rd
 begin
   lb_rd_d        <= 32'd0;
   lb_rd_rdy      <= 0;
   miso_rd_inc    <= 0;

   // Read Control Reg   
   if ( lb_rd == 1 && lb_cs_prom_c == 1 ) begin 
     lb_rd_d[27:8] <= spi_addr[27:8];
     lb_rd_d[7]    <= err_ram_wr_jk;
     lb_rd_d[6]    <= stat_polling_reqd_jk;
     lb_rd_d[5]    <= stat_mosi_buf_free;
     lb_rd_d[4]    <= stat_miso_buf_rdy_jk;
     lb_rd_d[3]    <= stat_fsm_wr;
     lb_rd_d[2]    <= stat_unlocked_jk;
     lb_rd_d[1]    <= flag_wip_loc;
     lb_rd_d[0]    <= stat_spi_busy;
     lb_rd_rdy     <= 1;
   end // if ( lb_rd == 1 && lb_cs_prom_c == 1 ) begin 

   // Read BRAM for MISO
   if ( lb_rd == 1 && lb_cs_prom_d == 1 ) begin 
     lb_rd_d     <= b_do_p1[31:0];
     lb_rd_rdy   <= 1;
     miso_rd_inc <= 1;
   end // if ( lb_wr == 1 && lb_cs_prom_d == 1 ) begin 

 end
end // proc_lb_rd


//-----------------------------------------------------------------------------
// Convert Generic Command Nibbles into Device Specific Command Bytes and Leng
//-----------------------------------------------------------------------------
always @ ( posedge clk_lb ) begin : proc_cmd_lut 
 begin
  // Latch as cmd_nib returns to 0x0 immediately
    
    // Translate User Nibble Commands to Device Specific Byte Commands
    case( cmd_nib[3:0] )
      `CMD_WR_PROM      : cmd_byte <= 8'h02;// Micron Page_Program
      `CMD_RD_PROM      : cmd_byte <= 8'h03;// Micron Read_Data
      `CMD_QUERY_ID     : cmd_byte <= 8'h9F;// Micron Read_ID
      `CMD_REL_PD       : cmd_byte <= 8'hAB;// Micron Release from PowerDown
      `CMD_ERASE_SECTOR : cmd_byte <= 8'hd8;// Micron Sector_Erase 
      `CMD_ERASE_BULK   : cmd_byte <= 8'hc7;// Micron Bulk_Erase   
      default           : cmd_byte <= 8'h00;// NULL
    endcase // case( cmd_nib[3:0] )

    // Is a WriteEnable required before User Command ?
    case( cmd_nib[3:0] )
      `CMD_WR_PROM      : we_reqd_jk <= 1;  // Page_Program
      `CMD_RD_PROM      : we_reqd_jk <= 0;  // Read_Data
      `CMD_QUERY_ID     : we_reqd_jk <= 0;  // Read_ID
      `CMD_REL_PD       : we_reqd_jk <= 0;  // Release PowerDown
      `CMD_ERASE_SECTOR : we_reqd_jk <= 1;  // Sector_Erase 
      `CMD_ERASE_BULK   : we_reqd_jk <= 1;  // Bulk_Erase   
      default           : we_reqd_jk <= 0;  // NULL
    endcase // case( cmd_nib[3:0] )

    // Will there be an address state ?
    case( cmd_nib[3:0] )
      `CMD_WR_PROM      : addr_reqd_jk <= 1;  // Page_Program
      `CMD_RD_PROM      : addr_reqd_jk <= 1;  // Read_Data
      `CMD_QUERY_ID     : addr_reqd_jk <= 0;  // Read_ID
      `CMD_REL_PD       : addr_reqd_jk <= 0;  // Release PowerDown
      `CMD_ERASE_SECTOR : addr_reqd_jk <= 1;  // Sector_Erase 
      `CMD_ERASE_BULK   : addr_reqd_jk <= 0;  // Bulk_Erase   
      default           : addr_reqd_jk <= 0;  // NULL
    endcase // case( cmd_nib[3:0] )

    // Will there be MISO data to capture ?
    case( cmd_nib[3:0] )
      `CMD_WR_PROM      : rd_reqd_jk <= 0;  // Page_Program
      `CMD_RD_PROM      : rd_reqd_jk <= 1;  // Read_Data
      `CMD_QUERY_ID     : rd_reqd_jk <= 1;  // Read_ID
      `CMD_REL_PD       : rd_reqd_jk <= 0;  // Release Power Down
      `CMD_ERASE_SECTOR : rd_reqd_jk <= 0;  // Sector_Erase 
      `CMD_ERASE_BULK   : rd_reqd_jk <= 0;  // Bulk_Erase   
      default           : rd_reqd_jk <= 0;  // NULL
    endcase // case( cmd_nib[3:0] )


  // UserCommand : Decide How Many MOSI and MISO Bytes to Xfer
  if ( mosi_fsm[3:0] == 4'h2 ) begin
   if ( prom_is_32b == 0 ) begin
    case( cmd_nib[3:0] )
      `CMD_WR_PROM      : xfer_tx_bytes <= 12'd4 + page_size[11:0];
      `CMD_RD_PROM      : xfer_tx_bytes <= 12'd4;
      `CMD_QUERY_ID     : xfer_tx_bytes <= 12'd1;
      `CMD_REL_PD       : xfer_tx_bytes <= 12'd1;
      `CMD_ERASE_SECTOR : xfer_tx_bytes <= 12'd4;
      `CMD_ERASE_BULK   : xfer_tx_bytes <= 12'd1;
      default           : xfer_tx_bytes <= 12'd0;          
    endcase // case( cmd_nib[3:0] )
   end else begin
    case( cmd_nib[3:0] )
      `CMD_WR_PROM      : xfer_tx_bytes <= 12'd5 + page_size[11:0];
      `CMD_RD_PROM      : xfer_tx_bytes <= 12'd5;
      `CMD_QUERY_ID     : xfer_tx_bytes <= 12'd1;
      `CMD_REL_PD       : xfer_tx_bytes <= 12'd1;
      `CMD_ERASE_SECTOR : xfer_tx_bytes <= 12'd5;
      `CMD_ERASE_BULK   : xfer_tx_bytes <= 12'd1;
      default           : xfer_tx_bytes <= 12'd0;          
    endcase // case( cmd_nib[3:0] )
   end
    case( cmd_nib[3:0] )
      `CMD_WR_PROM      : xfer_rx_bytes <= 12'd0;
      `CMD_RD_PROM      : xfer_rx_bytes <= page_size[11:0];
      `CMD_QUERY_ID     : xfer_rx_bytes <= 12'd4;
      `CMD_REL_PD       : xfer_rx_bytes <= 12'd0;
      `CMD_ERASE_SECTOR : xfer_rx_bytes <= 12'd0;
      `CMD_ERASE_BULK   : xfer_rx_bytes <= 12'd0;
      default           : xfer_rx_bytes <= 12'd0;
    endcase // case( cmd_nib[3:0] )
  end

  // ReadStatus WIP Poll   
  if ( mosi_fsm[3:0] == 4'h8 ) begin
    xfer_tx_bytes <= 12'd1;
    xfer_rx_bytes <= 12'd1;
  end

  // Single Byte Commands 
  if ( mosi_fsm[3:0] == 4'h1 ||
       mosi_fsm[3:0] == 4'h9 ||
       mosi_fsm[3:0] == 4'ha ||
       mosi_fsm[3:0] == 4'hb ||
       mosi_fsm[3:0] == 4'hc   ) begin
    xfer_tx_bytes <= 12'd1;
    xfer_rx_bytes <= 12'd0;
  end

 end
end // proc_cmd_lut


//-----------------------------------------------------------------------------
// Decide when SPI is idle or not
//-----------------------------------------------------------------------------
always @ ( posedge clk_lb ) begin : proc_spi_idle   
 begin
  spi_is_idle_p1    <= spi_is_idle;
  if ( xfer_start == 1 ) begin
    spi_is_idle_jk <= 0;
  end else if ( spi_is_idle == 1 && spi_is_idle_p1 == 0 ) begin
    spi_is_idle_jk <= 1;
  end
  if ( reset == 1 ||
       mosi_fsm == 4'hF ) begin 
    spi_is_idle_jk    <= 1;
  end
  if ( mosi_fsm[3:0] == 4'h0 ) begin
    stat_spi_busy <= 0;
  end else begin
    stat_spi_busy <= 1;
  end 
 end
end // proc_spi_idle


//-----------------------------------------------------------------------------
// When cmd_nib[3:0] goes non-zero start a SPI command and continue until
// the xfer count is tx+rx count.
//
// mosi_fsm[3:0]
//   0x0 : idle
//   0x1 : WriteEnable
//   0x2 : Command Byte
//   0x3 : spi_addr[31:24] ( Skipped for 3-Byte )
//   0x4 : spi_addr[23:16] 
//   0x5 : spi_addr[15:8] 
//   0x6 : spi_addr[7:0] 
//   0x7 : Data Stage
//   0x8 : ReadStatus
//   0x9 : Write-Enable for Enter 4-Byte Addr
//   0xA : Enter 4-Byte Addr
//   0xB : Write-Enable for Exit 4-Byte Addr
//   0xC : Exit 4-Byte Addr
//-----------------------------------------------------------------------------
always @ ( posedge clk_lb ) begin : proc_spi_len_fsm
 begin
  mosi_byte_en      <= mosi_byte_pre;
  mosi_byte_req_p1  <= mosi_byte_req;
  xfer_start        <= 0;
  xfer_start_p1     <= xfer_start;
  spi_addr_page_inc <= 0;
  flag_wip_loc      <= 0;
  flag_wip          <= flag_wip_loc;
  stat_fsm_wr       <= 0;
  done_enter_4byte  <= 0;

  if ( cmd_nib == `CMD_WR_PROM ) begin
    stat_fsm_wr <= 1;
  end

  // Either Kickoff new SPI Xfer, or advance the Xfer in progress 
  // If 32b PROM, Enable 4-Byte Mode 1st
//if ( mosi_fsm == 4'h0 && spi_is_idle_jk == 1 ) begin 
  if ( ( mosi_fsm == 4'h0 && spi_is_idle_jk == 1 ) ||    
       ( prom_is_32b == 1 && done_enter_4byte == 1 )   ) begin
    if (
         ( cmd_en_p1 == 1  || done_enter_4byte == 1 ) &&
           cmd_nib != `CMD_IDLE         &&
           cmd_nib != `CMD_RD_TIMESTAMP &&
           cmd_nib != `CMD_WR_PROM      &&
           cmd_nib != `CMD_UNLOCK       &&    
           cmd_nib != `CMD_RECONFIG_FPGA              ) begin
      xfer_start        <= 1;
      if ( we_reqd_jk == 1 ) begin
        mosi_fsm <= 4'h1;// Start with WriteEnable 
      end else begin
        mosi_fsm <= 4'h2;// Read can skip the WriteEnable
      end
      if ( prom_is_32b == 1 && done_enter_4byte == 0 ) begin
        xfer_start <= 1;
        mosi_fsm   <= 4'h9;// Enter 4-Byte Mode and defer cmd_nib
      end
    end

    // If Write Command, start when RAM buffer is full, not when cmd is issued
    if ( 
         ( mosi_buf_rdy_jk == 1 || done_enter_4byte == 1 ) && 
           cmd_nib == `CMD_WR_PROM                            ) begin
      // Only actually write if no overrun error has happened
      if ( err_ram_wr_jk == 0 ) begin
        xfer_start <= 1;
        mosi_fsm   <= 4'h1;// Start with WriteEnable 
      end
      if ( prom_is_32b == 1 && done_enter_4byte == 0 ) begin
        xfer_start <= 1;
        mosi_fsm   <= 4'h9;// Enter 4-Byte Mode and defer cmd_nib
      end
    end 
  end 

  if ( prom_is_32b == 1 ) begin 
    if ( spi_is_idle == 1 && spi_is_idle_p1 == 0 ) begin
      // Finished Write Enable for Entering 4-Byte Addr  
      if ( mosi_fsm == 4'h9 ) begin
        xfer_start <= 1;
        mosi_fsm   <= 4'ha;
      end 
      // Finished Entering 4-Byte Addr, time for actual command
      if ( mosi_fsm == 4'ha ) begin
//      mosi_fsm         <= 4'h0;// Done
        done_enter_4byte <= 1;
      end 
      // Finished Write Enable for Exiting 4-Byte Addr  
      if ( mosi_fsm == 4'hb ) begin
        xfer_start <= 1;
        mosi_fsm   <= 4'hc;
      end 
      // Finished Exit 4-Byte Addr
      if ( mosi_fsm == 4'hc ) begin
        mosi_fsm   <= 4'h0;
      end 
    end // if ( spi_is_idle == 1 && spi_is_idle_p1 == 0 ) begin
  end // if ( prom_is_32b == 1 ) begin 

  if ( xfer_start == 1 ) begin
    spi_byte_cnt <= 12'd0;
    xfer_mosi_jk <= 1;
  end 

  // Finished Write Enable for Erase or Programming
  if ( mosi_fsm == 4'h1 ) begin
    if ( spi_is_idle == 1 && spi_is_idle_p1 == 0 ) begin
      xfer_start <= 1;
      mosi_fsm   <= 4'h2;// Start Command Byte
    end
  end

  // Full Command,Addr     
  if ( mosi_fsm == 4'h2 ||
       mosi_fsm == 4'h3 ||
       mosi_fsm == 4'h4 ||
       mosi_fsm == 4'h5 ||
       mosi_fsm == 4'h6 
     ) begin
    if ( mosi_byte_pre == 1 ) begin
      if ( mosi_fsm == 4'h2 ) begin
        if ( prom_is_32b == 0 ) begin
          mosi_fsm <= 4'h4;// Skip 0x3 for 3-Byte Addressing
        end else begin
          mosi_fsm <= 4'h3;// 0x3 is for 4-Byte Addressing
        end
      end else begin
        mosi_fsm <= mosi_fsm[3:0] + 1;
      end
      if ( mosi_fsm == 4'h2 && addr_reqd_jk == 0 ) begin
        mosi_fsm <= 4'h7;// Skip Address Entirely
      end
    end
  end

  // Data Burst   
  if ( mosi_fsm == 4'h7 ) begin
    if ( spi_is_idle == 1 && spi_is_idle_p1 == 0 ) begin
      if ( we_reqd_jk == 1 ) begin
        xfer_start        <= 1;
        mosi_fsm          <= 4'h8;// Poll WIP 
        spi_addr_page_inc <= 1;
      end else begin
        if ( prom_is_32b == 1 ) begin 
          xfer_start <= 1;
          mosi_fsm   <= 4'hb;// WriteEnable then Exit 4-Byte
        end else begin
          mosi_fsm   <= 4'h0;// Done 
        end
      end
    end
  end

  // Poll WIP
  if ( mosi_fsm == 4'h8 ) begin
    flag_wip_loc <= 1;
    if ( miso_byte_rdy == 1 ) begin
      if ( miso_byte_d[0] == 0 ) begin
        if ( prom_is_32b == 1 ) begin 
          xfer_start <= 1;
          mosi_fsm   <= 4'hb;// WriteEnable then Exit 4-Byte
        end else begin
          mosi_fsm   <= 4'h0;// Done 
        end
      end else begin
        xfer_start   <= 1;
        mosi_fsm     <= 4'h8;// Poll WIP 
      end
    end
  end

  // If we just xferred a SPI TX or RX Byte count it
  if ( ( xfer_mosi_jk == 1 && mosi_byte_pre == 1 ) ||   
       ( xfer_mosi_jk == 0 && miso_byte_rdy == 1 )    )begin
    spi_byte_cnt <= spi_byte_cnt + 1;
  end

  // Once all MOSI Bytes are transferred, switch to MISO Bytes
  if ( mosi_byte_en == 1 && spi_byte_cnt == xfer_tx_bytes ) begin
    xfer_mosi_jk <= 0;
  end

  if ( reset == 1 ) begin
    mosi_fsm <= 4'h0;
  end
 
 end
end // proc_spi_len_fsm


//-----------------------------------------------------------------------------
// MOSI Mux. Mux in a MOSI Byte based on FSM stage ( Cmd,Addr,Data)
//
//      cmd_byte[7:0]->|\
//   spi_addr[31:0]--->| |---> mosi_byte_d[7:0]
//      ram_d[31:0]--->|/
//                      |
//        mosi_fsm[3:0]-
//-----------------------------------------------------------------------------
always @ ( posedge clk_lb ) begin : proc_mosi_mux
 begin
  mosi_byte_pre <= 0;
  mosi_rd_inc   <= 0;
  // When SPI wants a new MOSI byte, Mux Byte into SPI path based on FSM state
  if ( mosi_byte_req == 1 && mosi_byte_req_p1 == 0 ) begin
    mosi_byte_pre <= 1;
    if ( mosi_fsm[3:0] != 4'h7 ) begin
      case( mosi_fsm[3:0] )
        4'h1    : mosi_byte_d <= 8'h06;// Micron WriteEnable
        4'h2    : mosi_byte_d <= cmd_byte[7:0];
        4'h3    : mosi_byte_d <= spi_addr[31:24]; 
        4'h4    : mosi_byte_d <= spi_addr[23:16]; 
        4'h5    : mosi_byte_d <= spi_addr[15:8]; 
        4'h6    : mosi_byte_d <= spi_addr[7:0]; 
        4'h8    : mosi_byte_d <= 8'h05;// Micron ReadStatus
        4'h9    : mosi_byte_d <= 8'h06;// Micron WriteEnable
        4'ha    : mosi_byte_d <= 8'hb7;// Micron Enter 4-Byte Addr Mode
        4'hb    : mosi_byte_d <= 8'h06;// Micron WriteEnable
        4'hc    : mosi_byte_d <= 8'he9;// Micron Exit  4-Byte Addr Mode
        default : mosi_byte_d <= 8'h00;// NULL
      endcase // case( mosi_fsm[3:0] )
      mosi_byte_cnt <= 2'd0;

      // Prevent WriteEnable from happening if things are locked by issuing
      // a Micron ReadStatus command instead of the WriteEnable command
      if ( mosi_fsm[3:0] == 4'h1 ) begin

        // If PROM is locked, issue ReadStatus instead of WriteEnable
        if ( prom_wr_locked_jk_p1 == 1 ) begin
          mosi_byte_d <= 8'h05;// Micron ReadStatus
        end

        // Protect BootLoader design at Slot-0 
        // If protect_1st_slot==1 and fpga_root_locked_jk==1 check to see if
        // spi_addr < slot_size and issue ReadStatus instead of WriteEnable
        if ( protect_1st_slot == 1 && fpga_root_locked_jk == 1 &&
             spi_addr[31:0] < slot_size[31:0]                     ) begin
          mosi_byte_d <= 8'h05;// Micron ReadStatus
        end

      end // if ( mosi_fsm[3:0] == 4'h1 ) begin

    end else begin
      case( mosi_byte_cnt[1:0] )
        2'h0    : mosi_byte_d <= b_do_p1[31:24];
        2'h1    : mosi_byte_d <= b_do_p1[23:16];
        2'h2    : mosi_byte_d <= b_do_p1[15:8];
        default : mosi_byte_d <= b_do_p1[7:0];
      endcase // case( mosi_byte_cnt[1:0] )

      mosi_byte_cnt <= mosi_byte_cnt + 1;
      if ( mosi_byte_cnt[1:0] == 2'd3 ) begin
        mosi_rd_inc <= 1;// Finished last Byte, Get next DWORD from BRAM
      end
    end
  end // if ( mosi_byte_req == 1 && mosi_byte_req_p1 == 0 ) begin

 end
end // proc_mosi_mux


//-----------------------------------------------------------------------------
// MISO State Machine. Take MISO data and push to BRAM. 
// Depending on the command either 1 byte or series of 4 bytes
//-----------------------------------------------------------------------------
always @ ( posedge clk_lb ) begin : proc_miso_fsm
 begin
   a_we_miso <= 0;
   if ( miso_byte_rdy == 1 ) begin
     // Count MISO Bytes for storing DWORDs to BRAM
     miso_byte_cnt <= miso_byte_cnt[1:0] + 1;
     if ( xfer_rx_bytes == 12'd1 ) begin 
       a_we_miso <= 1;
       a_di_miso <= { 24'd0, miso_byte_d[7:0] };
     end else begin
       a_di_miso <= { miso_byte_d[7:0], a_di_miso[31:8] };
       if ( miso_byte_cnt == 2'd3 ) begin
         a_we_miso <= 1; // Write DWORD after shifting 4 bytes
       end
     end 
   end // if ( miso_byte_rdy == 1 ) begin

   // At end of Read Command, assert MISO Buffer Ready JK
   if ( cmd_nib == `CMD_RD_PROM ) begin
     if ( spi_is_idle == 1 && spi_is_idle_p1 == 0 ) begin
       stat_miso_buf_rdy_jk <= 1;
     end
   end

   if ( strb_clear_fsm == 1 ) begin
     miso_byte_cnt        <= 2'd0;
     stat_miso_buf_rdy_jk <= 0;
   end
 end
end // proc_miso_fsm


//-----------------------------------------------------------------------------
// BRAM Write Access.
// This is a ping-pong buffer so that Backdoor or PCIe can fill 256 bytes 
// while previous 256 bytes are transferred over SPI. 
// A FIFO would work too, but this is more portable using inferred BRAM.
// As soon as there are 256 Bytes, a SPI xfer begins.
// The next 256 bytes can be loaded while the 1st are shipped out.
// If a new buffer is written while previous buffer is being read, a flag is
// set to tell software it should poll for free buffer status after each 256
// byte burst. Another flag indicates if the buffer was overrun.
//
// Good:
// (BD)   |- 6ms -|------ 32 ms -----|
//  LB   -<  Ping  >-----------------< Pong >----------------------
//  SPI  ----------<  Ping  >---------------< Pong >---------------
//  WIP             |--3ms--|<  Ping >-------------< Pong >--------
//                           |-800us-|
// Bad:
// (BD)   
//  LB   -<Ping>--<Pong>--<Ping>---------------
//  SPI  ----------<  Ping  >< Pong >---------------
//  WIP                     < Ping >-< Pong >--------
//  PollReqd _______/
//  ErrRAMwr _____________/
//-----------------------------------------------------------------------------
always @ ( posedge clk_lb ) begin : proc_ram_wr_fsm
 begin
  stat_mosi_buf_free <= buffer_free_jk;
  a_we               <= 0;

  if ( strb_wr_data == 1 && mosi_buf_rdy_jk == 0 ) begin
    a_we                   <= 1;
    a_di                   <= prom_data_reg[31:0];
    a_addr[depth_bits-2:0] <= a_addr[depth_bits-2:0] + 1;
    a_addr[depth_bits-1]   <= wr_ping_pong;
    if ( a_addr[depth_bits-2:0] == { all_ones[depth_bits-2:1], 1'b0 } ) begin
      buffer_free_jk    <= 1; // Whole new Buffer available
      mosi_buf_rdy_jk   <= 1; // Need to dump this buffer to SPI
      wr_ping_pong      <= ~ wr_ping_pong;
      rd_ping_pong_pend <=   wr_ping_pong;
    end else begin
      buffer_free_jk  <= 0; // Filling buffer, can't start a new 256 burst
    end
  end // if ( strb_wr_data == 1 ) begin

  // Check to see if writing to buffer while one is being read.
  if ( strb_wr_data == 1 && rd_ping_pong_actv[1] == 1 ) begin
    if ( wr_ping_pong == rd_ping_pong_actv[0] ) begin
      err_ram_wr_jk        <= 1;
    end else begin
      stat_polling_reqd_jk <= 1;
    end
  end

  // Determine if this buffer has been claimed by SPI controller
  if ( xfer_start == 1 && mosi_fsm == 4'h1 ) begin
    if ( mosi_buf_rdy_jk == 1 ) begin 
      rd_ping_pong_actv <= { 1'b1, rd_ping_pong_pend };
      mosi_buf_rdy_jk   <= 0;
    end
  end else if ( mosi_fsm == 4'h0 ) begin
    rd_ping_pong_actv[1] <= 0;
  end

  if ( reset == 1 || strb_clear_fsm == 1 ) begin
    wr_ping_pong           <= 0;
    rd_ping_pong_pend      <= 0;
    rd_ping_pong_actv      <= 2'b01;
    buffer_free_jk         <= 1;
    mosi_buf_rdy_jk        <= 0;
    a_addr[depth_bits-2:0] <= all_ones[depth_bits-2:0];// Roll from Ones to Zero
    a_addr[depth_bits-1]   <= 0;
    err_ram_wr_jk          <= 0;
    stat_polling_reqd_jk   <= 0;
  end

  // Normally BRAM writes are from LB, but If MISO has a byte, write it to BRAM
  if ( a_we_miso == 1 && rd_reqd_jk == 1 ) begin
    a_we                   <= 1;
    a_di                   <= a_di_miso[31:0];
    a_addr[depth_bits-2:0] <= a_addr[depth_bits-2:0] + 1;
    a_addr[depth_bits-1]   <= 1;
  end

  // Normally BRAM writes are from LB, but load 32bit UNIX timetstamp on reqst
  if ( req_timestamp_p1 == 1 ) begin
    a_we                   <= 1;
    a_di                   <= time_stamp_d[31:0];
    a_addr[depth_bits-2:0] <= a_addr[depth_bits-2:0] + 1;
    a_addr[depth_bits-1]   <= 1;
  end
  // After the TimeStamp, provide the slot size
  if ( req_timestamp_p2 == 1 ) begin
    a_we                   <= 1;
    a_di                   <= slot_size[31:0];
    a_addr[depth_bits-2:0] <= a_addr[depth_bits-2:0] + 1;
    a_addr[depth_bits-1]   <= 1;
  end

 end
end // proc_ram_wr_fsm


//-----------------------------------------------------------------------------
// BRAM Read Port FSM : b_addr starts at 0x0 at beginning of a SPI command and
// increments after a dword has been read.
//-----------------------------------------------------------------------------
always @ ( posedge clk_lb ) begin : proc_ram_rd_fsm
 begin
  b_addr[depth_bits-2:0] <= b_addr[depth_bits-2:0];
  b_addr[depth_bits-1]   <= rd_ping_pong_actv[0];
  if ( strb_clear_fsm == 1 ) begin
    b_addr[depth_bits-2:0] <= all_zeros[depth_bits-2:0];
    b_addr[depth_bits-1]   <= rd_ping_pong_actv[0];
  end
  if ( miso_rd_inc == 1 || mosi_rd_inc == 1 ) begin
    b_addr[depth_bits-2:0] <= b_addr[depth_bits-2:0] + 1;
    b_addr[depth_bits-1]   <= rd_ping_pong_actv[0];
  end
 end
end // proc_ram_rd_fsm


//-----------------------------------------------------------------------------
// Dual Port RAM - Infer RAM here to make easy to change depth on the fly
// Really small RAM is used for simulations
//-----------------------------------------------------------------------------
always @( posedge clk_lb )
begin
  a_we_p1   <= a_we;
  a_addr_p1 <= a_addr[depth_bits-1:0];
  a_di_p1   <= a_di[31:0];
  if ( a_we_p1 ) begin
    ram_array[a_addr_p1] <= a_di_p1[31:0];
  end // if ( a_we )
end // always

always @( posedge clk_lb )
begin
  a_do <= ram_array[a_addr];
end // always

always @( posedge clk_lb )
begin
  b_do <= ram_array[b_addr];
  b_do_p1 <= b_do[31:0];
end // always


//-----------------------------------------------------------------------------
// 32bit UNIX TimeStamp of when the design was synthesized
//-----------------------------------------------------------------------------
time_stamp u_time_stamp
(
  .time_dout                        ( time_stamp_d                   )
);


//-----------------------------------------------------------------------------
// Convert MOSI Bytes into SPI Bits and SPI Bits into MISO Bytes
//-----------------------------------------------------------------------------
spi_byte2bit u_spi_byte2bit
(
  .reset                           ( reset                          ),
  .clk                             ( clk_lb                         ),
  .ck_divisor                      ( ck_divisor[7:0]                ),
  .spi_ctrl                        ( spi_ctrl[3:0]                  ),

  .spi_sck                         ( spi_sck                        ),
  .spi_cs_l                        ( spi_cs_l                       ),
  .spi_mosi                        ( spi_mosi                       ),
  .spi_miso                        ( spi_miso                       ),
  .spi_is_idle                     ( spi_is_idle                    ),

  .xfer_start                      ( xfer_start_p1                  ),
  .xfer_stall                      ( 1'b0                           ),
  .xfer_tx_bytes                   ( xfer_tx_bytes[11:0]            ),
  .xfer_rx_bytes                   ( xfer_rx_bytes[11:0]            ),

  .mosi_byte_d                     ( mosi_byte_d[7:0]               ),
  .mosi_byte_en                    ( mosi_byte_en                   ),
  .mosi_byte_req                   ( mosi_byte_req                  ),
  .mosi_err                        (                                ),

  .miso_byte_d                     ( miso_byte_d[7:0]               ),
  .miso_byte_rdy                   ( miso_byte_rdy                  )
);


endmodule // spi_prom.v
