/* ****************************************************************************
-- Source file: spi_byte2bit.v
-- Date:        June 1,2014   
-- Author:      khubbard
-- Description: Simple interface to SPI that xfers Bytes to Bits.
--              Supports read stalling of MISO stream.
-- Language:    Verilog-2001
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
--                                 spi_byte2bit     
--                                 ----------
--            clk        -------->|          |
--            xfer_start  ------->|          |
--            xfer_tx_bytes[7:0]->|          |
--            xfer_rx_bytes[7:0]->|          |
--            mosi_byte_d[7:0]--->|          |--> miso_byte_d[7:0]
--            mosi_byte_en ------>|          |--> miso_byte_rdy 
--            mosi_byte_req <-----|          |
--                                |          |
--                   spi_miso --->|          |--> spi_mosi
--                                |          |--> spi_sck 
--                                |          |--> spi_cs_l
--                                 ----------
--
-- spi_sck    _/\/\/\/\/\/\/\/\____/\/\/\/\/\/\/\/\/\/\/\/\/
-- spi_cs_l              \_________________________/
-- spi_mosi   -----------< B0 >----< B1 >------------------
-- spi_miso   --------------------------< B2 >< B3>------
--
-- xfer_start_/   \_______________________________________
-- xfer_stall___________________________________/   \_____
-- tx_bytes  -< 2 >---------------------------------------
-- rx_bytes  -< 2 >---------------------------------------
--
-- mosi_byte_en     __/  \__..._/  \_____
-- mosi_byte_d[7:0] --<B0>--...-<B1>-----
-- mosi_byte_req   ______/      \_______
--
-- miso_byte_rdy    ________________________/  \___/  \__
-- miso_byte_d[7:0] ------------------------<B2>---<B3>--
-- 
-- 
-- Note: Starving the MOSI stream is allowed. The block will gate the SPI clock
--       and wait until the MOSI byte arrives.
--
-- Note: Stalling the MISO stream is allowed. Asserting xfer_stall will gate
--       the entire block, allowing for the MISO stream to be stalled.
--
-- Revision History:
-- Ver#  When      Who      What
-- ----  --------  -------- --------------------------------------------------
-- 0.1   06.01.14  khubbard Creation
-- 0.1   06.25.14  khubbard Removed mosi_starved logic. Not worth complication
-- 0.2   01.29.16  khubbard Fixed reset issue when ck_div is 0x00 at reset.
-- ***************************************************************************/
//`default_nettype none // Strictly enforce all nets to be declared

module spi_byte2bit 
(
  input  wire         reset,
  input  wire         clk,
  input  wire [7:0]   ck_divisor,
  input  wire [3:0]   spi_ctrl,

  output reg          spi_sck,
  output wire         spi_cs_l,
  output reg          spi_mosi,
  input  wire         spi_miso,
  output reg          spi_is_idle,

  input  wire         xfer_start,
  input  wire         xfer_stall,
  input  wire [11:0]  xfer_tx_bytes,
  input  wire [11:0]  xfer_rx_bytes,

  input  wire [7:0]   mosi_byte_d,
  input  wire         mosi_byte_en,
  output wire         mosi_byte_req,
  output reg          mosi_err,

  output reg  [7:0]   miso_byte_d,
  output reg          miso_byte_rdy
);

  reg  [7:0]              ck_cnt;
  wire [7:0]              ck_div;
  reg                     ck_loc;
  reg                     ck_off_pre;
  reg                     ck_off;
  reg                     ck_en_fal;
  reg                     ck_en_ris;
  reg                     spi_cs_pre;
  reg                     spi_cs_l_loc;
  reg                     mosi_load;
  reg                     mosi_loaded;
  reg  [7:0]              mosi_sr;
  wire [7:0]              mosi_sr_loc;
  reg  [7:0]              mosi_byte_queue;
  reg                     mosi_queued_jk;
  reg                     mosi_refused;
  reg                     mosi_byte_req_loc;
  reg                     mosi_byte_en_p1;
  reg                     miso_loc;
  reg  [7:0]              miso_sr;
  reg  [11:0]             xfer_cnt;
  reg                     xfer_tx_jk;
  reg                     xfer_tx_jk_p1;
  reg                     xfer_rx_jk;
  reg  [4:0]              xfer_rx_jk_sr;
  reg                     xfer_start_jk;
  reg  [11:0]             xfer_length;
  reg  [11:0]             xfer_rx_length;
  wire [8:0]              xfer_byte;
  wire [2:0]              xfer_bit;
  reg  [2:0]              xfer_bit_p1;
  reg  [2:0]              xfer_bit_p2;
  reg  [2:0]              xfer_bit_p3;
  reg  [2:0]              xfer_bit_p4;

//assign ck_div     = 8'd10;
  assign ck_div     = ck_divisor[7:0];// Num of clocks in 1/2 spi clock period 


//-----------------------------------------------------------------------------
// Generate spi_ck. Counts 1/2 periods and toggles a bit.       
//   ck         _/ \_/ \_/ \_/ \_/ \_/ \_/
//   cnt          1   2   1   2   1   2
//   ck_loc     _/       \_______/       \_
//   ck_en_ris  _/   \___________/   \_____
//   ck_en_fal  _________/   \___________/   
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_spi_ck   
 begin
  if ( xfer_stall == 0 ) begin
    ck_en_fal     <= 0;
    ck_en_ris     <= 0;
    // Clock Divider
//  if ( ck_cnt != 8'hFF ) begin
    if ( 1 == 1 ) begin
      ck_cnt <= ck_cnt + 1;
      if ( ck_cnt == ck_div ) begin
        ck_loc <= ~ ck_loc;
        ck_cnt <= 8'h1;
        if ( ck_loc == 1 ) begin
          ck_en_fal <= 1;
        end else begin
          ck_en_ris <= 1;
        end
      end
    end
//  end else begin
//    ck_cnt <= ck_cnt[7:0];
//    ck_loc <= 0;
//  end
  
    if ( reset == 1 ) begin
      ck_cnt <= 8'd1;
      ck_loc <= 0;
    end
  end
 end
end // proc_spi_ck   


//-----------------------------------------------------------------------------
// Shift in the MISO data
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_miso_sr  
 begin
  if ( xfer_stall == 0 ) begin
// miso_byte_d   <= 8'd0;
   miso_byte_rdy <= 0;
// if ( ck_en_fal == 1 ) begin
   if ( ck_en_ris == 1 ) begin
     xfer_bit_p1 <= xfer_bit[2:0];
     xfer_bit_p2 <= xfer_bit_p1[2:0];
     xfer_bit_p3 <= xfer_bit_p2[2:0];
     xfer_bit_p4 <= xfer_bit_p3[2:0];
     if ( xfer_rx_jk == 1 || xfer_rx_jk_sr != 5'd0 ) begin
        miso_loc     <= spi_miso;
        miso_sr[0]   <= miso_loc;
        miso_sr[7:1] <= miso_sr[6:0];
        if ( xfer_bit_p4 == 3'd0 && xfer_rx_jk_sr[4] == 1 ) begin
          miso_byte_d   <= miso_sr[7:0];
          miso_byte_rdy <= 1;
        end
     end
   end 
  end 
  if ( reset == 1 ) begin
    miso_byte_rdy <= 0;
    xfer_bit_p1   <= 3'd0;
    xfer_bit_p2   <= 3'd0;
    xfer_bit_p3   <= 3'd0;
    xfer_bit_p4   <= 3'd0;
   end
 end
end // proc_miso_sr


//-----------------------------------------------------------------------------
// On rising edge of xfer_start_jk, load the number of bytes * 8bits into a 
// bit counter and start the counter. Count down to 0 then turn off xfer_tx_jk.
//   xfer_start      __/ \_________________________/ \___
//   tx_bytes     --<2>-------------------------<2>---
//   rx_bytes     --<4>-------------------------<4>---
//   mosi         -------<0><1>-----------------------
//   miso         -------------<0><1><2><3>-----------
//   xfer_tx_jk   _______/     \______________________
//   xfer_rx_jk   _____________/           \__________
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_xfer_req 
 begin
  if ( xfer_stall == 0 ) begin
   if ( xfer_start == 1 ) begin
     xfer_length <= xfer_tx_bytes[11:0];
     xfer_rx_length <= xfer_rx_bytes[11:0];
   end

   if ( ck_en_fal == 1 ) begin
     mosi_err           <= 0;
     mosi_load          <= 0; 
     xfer_tx_jk_p1      <= xfer_tx_jk;
     xfer_rx_jk_sr[0]   <= xfer_rx_jk;
     xfer_rx_jk_sr[4:1] <= xfer_rx_jk_sr[3:0];
     if ( xfer_start_jk != 0 ) begin
       xfer_cnt <= { xfer_length[8:0] , 3'b000 };
       xfer_tx_jk  <= 1;
     end else if ( xfer_cnt != 12'd0 ) begin
       if ( xfer_tx_jk == 1 ) begin
         if ( xfer_bit == 2'd0 ) begin
           if ( mosi_queued_jk == 1 ) begin
             mosi_load       <= 1; // Time for a new Byte
           end else begin
             mosi_err <= 1; // Nothing to send.
           end
         end
         xfer_cnt <= xfer_cnt - 1;
         if ( xfer_cnt == 12'd1 ) begin
           xfer_tx_jk <= 0;
           if ( xfer_rx_length[8:0] != 9'd0 ) begin
             xfer_rx_jk <= 1;
             xfer_cnt   <= { xfer_rx_length[8:0] , 3'b000 };
           end
         end
       end else if ( xfer_rx_jk == 1 ) begin
         xfer_cnt <= xfer_cnt - 1;
         if ( xfer_cnt == 12'd1 ) begin
           xfer_rx_jk <= 0;
         end
       end
     end
   end

   if ( reset == 1 ) begin
     mosi_load       <= 0;
     xfer_cnt        <= 12'd0;
     xfer_tx_jk      <= 0;
     xfer_rx_jk      <= 0;
     xfer_tx_jk_p1   <= 0;
     xfer_rx_jk_sr   <= 5'd0;
   end 
  end 
 end
end // proc_xfer_req
  assign xfer_byte[8:0] = xfer_cnt[11:3];
  assign xfer_bit[2:0]  = xfer_cnt[2:0];


//-----------------------------------------------------------------------------
// Latch the MOSI byte data and start shifting out
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_mosi_sr 
 begin
  if ( xfer_stall == 0 ) begin
   mosi_refused    <= 0;
   mosi_byte_en_p1 <= mosi_byte_en;
   if ( ck_en_fal == 1 ) begin
     xfer_start_jk <= 0;
   end 

   // When new mosi byte comes in, place in queue and drop request
   if ( mosi_byte_en == 1 ) begin
     xfer_start_jk     <= ~ xfer_tx_jk;      // A new Xfer
     mosi_byte_queue   <= mosi_byte_d[7:0];
     mosi_queued_jk    <= 1;
     mosi_byte_req_loc <= 0; 
     // Assert refused if not expecting a byte.
     if ( xfer_tx_jk == 1 ) begin
       mosi_refused  <= ~ mosi_byte_req_loc; 
     end
   end // if ( mosi_byte_en == 1 ) begin

   if ( mosi_loaded == 1 || xfer_start == 1 ) begin
     mosi_byte_queue   <= 8'd0;
     mosi_queued_jk    <= 0;
     if ( xfer_byte != 9'd0 || xfer_start == 1 ) begin
       mosi_byte_req_loc <= 1;
     end
   end 

   if ( reset == 1 ) begin
     mosi_queued_jk    <= 0;
     mosi_byte_req_loc <= 0;
     xfer_start_jk     <= 0;
   end // if ( reset == 1 ) begin
  end
 end
end // proc_mosi_sr 

  assign mosi_sr_loc[7:0] = mosi_sr[7:0];
  assign mosi_byte_req = mosi_byte_req_loc;


//-----------------------------------------------------------------------------
// Either Latch a new MOSI byte or shift existing byte 
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_mosi_shift
 begin
  if ( xfer_stall == 0 ) begin
   mosi_loaded <= 0;
   if ( ck_en_fal == 1 ) begin
     if ( mosi_load == 1 ) begin
       mosi_sr     <= mosi_byte_queue[7:0];  
       mosi_loaded <= 1;
     end else begin
       mosi_sr <= { mosi_sr[6:0], 1'd0 }; // Shift the Byte Out
     end
   end // if ( ck_en_fal == 1 ) begin
   if ( reset == 1 ) begin
     mosi_sr <= 8'd0;
   end 
  end 
 end
end // proc mosi_shift


//-----------------------------------------------------------------------------
// SPI Master Output : The serial bits.
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_mosi_out
 begin
  if ( xfer_stall == 0 ) begin
   if ( spi_ctrl[0] == 0 ) begin
     spi_sck     <= ck_loc & ~ ck_off;
   end else begin
     spi_sck     <= ~ ck_loc & ~ ck_off;
   end
                             
//  if ( ck_en_fal == 1 ) begin
    if ( ( ck_en_fal == 1 && spi_ctrl[1] == 0 ) ||
         ( ck_en_ris == 1 && spi_ctrl[1] == 1 )    ) begin
      ck_off       <= ck_off_pre;
      spi_cs_pre   <= xfer_tx_jk_p1 | xfer_rx_jk_sr[0];
      spi_cs_l_loc <= ~ spi_cs_pre;
      spi_is_idle  <= ~ spi_cs_pre;
      spi_mosi     <= mosi_sr_loc[7];
      if ( spi_cs_pre == 0 && 
           xfer_tx_jk_p1 == 0 && 
           xfer_rx_jk_sr[0] == 0 ) begin 
        ck_off_pre <= 1;
      end else begin
        ck_off_pre <= 0;
        ck_off     <= 0;
      end
    end
  end // if ( xfer_stall == 0 ) begin
  if ( reset == 1 ) begin
    ck_off_pre   <= 0;
    ck_off       <= 0;
    spi_cs_pre   <= 0;
    spi_cs_l_loc <= 1;
  end
 end
end // proc_mosi_out
  assign spi_cs_l = spi_cs_l_loc;


endmodule // spi_byte2bit
