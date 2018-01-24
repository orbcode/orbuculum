/* ****************************************************************************
-- (C) Copyright 2015 Kevin M. Hubbard - Black Mesa Labs
-- Source file: mesa_uart.v                
-- Date:        June 1, 2015   
-- Author:      khubbard
-- Description: A UART that autobauds to first "\n" 0x0A character.
-- Language:    Verilog-2001 
-- License:     This project is licensed with the CERN Open Hardware Licence
--              v1.2.  You may redistribute and modify this project under the
--              terms of the CERN OHL v.1.2. (http://ohwr.org/cernohl).
--              This project is distributed WITHOUT ANY EXPRESS OR IMPLIED
--              WARRANTY, INCLUDING OF MERCHANTABILITY, SATISFACTORY QUALITY
--              AND FITNESS FOR A PARTICULAR PURPOSE. Please see the CERN OHL
--              v.1.2 for applicable Conditions.
-- License:     This project is licensed with the CERN Open Hardware Licence
--              v1.2.  You may redistribute and modify this project under the
--              terms of the CERN OHL v.1.2. (http://ohwr.org/cernohl).
--              This project is distributed WITHOUT ANY EXPRESS OR IMPLIED
--              WARRANTY, INCLUDING OF MERCHANTABILITY, SATISFACTORY QUALITY
--              AND FITNESS FOR A PARTICULAR PURPOSE. Please see the CERN OHL
--              v.1.2 for applicable Conditions.
--
-- RXD    \START/<D0><D1><D2><..><D7>/STOP
-- Design Statistics after Packing
-- en_clr_lock = 1;
--    Number of LUTs  : 190 / 384
--    Number of DFFs  : 110 / 384
--
-- Revision History:
-- Ver#  When      Who      What
-- ----  --------  -------- ---------------------------------------------------
-- 0.1   06.01.15  khubbard Creation
-- 0.2   08.18.16  khubbard rx_sr[2:0] fix for metastable sampling issue.
-- ***************************************************************************/
//`default_nettype none // Strictly enforce all nets to be declared
                                                                                
module mesa_uart
(
  input  wire         reset,
  input  wire         clk,
  input  wire         clr_baudlock,
  input  wire         en_autobaud,
  output wire         flush,
  input  wire         rxd,
  output reg          txd,
  output reg  [7:0]   dbg,
  output wire [7:0]   rx_byte,
  output reg          rx_rdy,
  input  wire [7:0]   tx_byte,
  input  wire         tx_en,
  output reg          tx_busy,
  output reg          tx_idle,
  output wire         rx_idle,
  output wire [15:0]  baud_rate,
  output wire         baud_lock
); // module mesa_uart


  reg  [8:0]    rx_byte_sr;
  reg  [3:0]    rx_bit_cnt;
  reg  [3:0]    tx_bit_cnt;
  reg           sample_now;
  reg           sample_now_p1;
  reg           rxd_meta;
  reg           rxd_loc;
  wire          rxd_sample;
  reg  [7:0]    rxd_sr;
  reg           clr_bit_lock;
  reg           dbg_sample_togl;
  reg           dbg_sample_togl_p1;
  reg           dbg_byte_togl;
  reg           clr_jk;
  reg           bad_jk;
  reg           bit_lock_jk;
  reg           bit_lock_jk_p1;
  reg           baud_lock_jk;
  reg           rx_byte_jk;
  reg           rx_byte_jk_p1;

  wire [7:0]    rx_byte_loc;
  reg  [15:0]   rx_cnt_16b;
  reg  [15:0]   tx_cnt_16b;
  reg  [15:0]   baud_rate_loc;
  reg           rx_cnt_16b_p1;
  reg           rx_cnt_16b_roll;
  reg           rx_start_a;
  reg           rx_start_b;
  reg           rx_cnt_en_a_jk;
  reg           rx_cnt_en_b;
  reg           rx_rdy_loc;
  reg           rx_rdy_pre;
  wire          en_loopback;
  wire          en_fast_autobaud;
  reg  [7:0]    tx_byte_loc;
  reg           tx_en_loc;
  reg  [9:0]    tx_sr;
  reg           txd_loc;
  reg           rxd_fal;
  reg           rxd_ris;
  reg  [7:0]    rxd_fal_sr;
  reg           tx_shift;
  reg           tx_now;
  reg           rx_ascii_cr;
  reg           rx_bad_symbol;
  reg  [7:0]    dbg_loc;
  reg  [7:0]    dbg_loc_p1;
  reg  [3:0]    dbg_cnt;
  reg           sample_first;


// Send RXD to TXD to test circuit
  assign en_loopback = 0;
//assign en_fast_autobaud = 0;
  assign en_fast_autobaud = 1;
  assign rx_idle = 0;

// When baud_lock asserts, send "\n" to downstream nodes for detection
  assign baud_rate = baud_rate_loc[15:0];
  assign baud_lock = baud_lock_jk;

//-----------------------------------------------------------------------------
// Output some debug signals for Saleae. Pulse extend short events
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_dbg
 begin
// dbg_loc[0] <= ~ rxd_meta;
   dbg_loc[0] <= rxd_sample;
   dbg_loc[1] <= txd_loc;
   dbg_loc[2] <= bit_lock_jk;
   dbg_loc[3] <= baud_lock_jk;
// dbg_loc[4] <= sample_now;
   dbg_loc[4] <= reset;
// dbg_loc[5] <= rx_rdy_loc;
   dbg_loc[5] <= clr_jk;
   dbg_loc[6] <= dbg_sample_togl;
   dbg_loc[7] <= dbg_byte_togl;
// dbg_loc[7] <= bad_jk;
// dbg_loc[7] <= rx_ascii_cr;
   dbg_loc_p1 <= dbg_loc[7:0];

   // Extend transitions for Saleae
   if ( dbg_cnt != 4'hF ) begin
     dbg_cnt <= dbg_cnt + 1;
   end

   if ( dbg_loc[7:4] != dbg_loc_p1[7:4] && dbg_cnt == 4'hF ) begin
     dbg     <= dbg_loc[7:0];
     dbg_cnt <= 4'h0;
   end
   if ( reset == 1 ) begin
     dbg_cnt <= 4'h0;
   end

   dbg[0] <= dbg_loc[0];
   dbg[1] <= dbg_loc[1];
   dbg[2] <= dbg_loc[2];
   dbg[3] <= dbg_loc[3];

 end 
end // proc_din


//-----------------------------------------------------------------------------
// Asynchronous sampling of RXD into 4 bit Shift Register
// Note: RXD is immediately inverted to prevent runt '0's from pipeline 
// being detected as start bits. FPGA Flops powerup to '0', so inverting fixes.
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_din
 begin
   rxd_meta    <= ~ rxd;// Note Inversion to prevent runt post config
   rxd_loc     <= rxd_meta;
// rxd_sr[7:0] <= { rxd_sr[6:0], rxd_meta };
   rxd_sr[7:0] <= { rxd_sr[6:0], rxd_loc  };
   rxd_fal <= 0;
   rxd_ris <= 0;

// if ( rxd_sr[3:0] == 4'b0001 ) begin      
// if ( rxd_sr[2:0] == 3'b001 ) begin      
// if ( rxd_sr[2:0] == 3'b011 ) begin      
   if ( rxd_sr[7:6] == 2'b00 && rxd_sr[2:0] == 3'b011 ) begin      
     rxd_fal <= 1;
   end 
// if ( rxd_sr[3:0] == 4'b1110 ) begin      
// if ( rxd_sr[2:0] == 3'b110 ) begin      
// if ( rxd_sr[2:0] == 3'b100 ) begin      
   if ( rxd_sr[7:6] == 2'b11 && rxd_sr[2:0] == 3'b100 ) begin      
     rxd_ris <= 1;
   end 

 end 
end // proc_din
  assign rxd_sample = ~ rxd_sr[2];


//-----------------------------------------------------------------------------
// 16bit Counter used for both rx_start_bit width count and sample count
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_cnt
 begin

  if ( rx_start_a == 1 || rx_start_b == 1 ) begin
    rx_cnt_16b <= 16'd0;
  end else if ( rx_cnt_en_a_jk == 1 || 
                rx_cnt_en_b    == 1    ) begin
    rx_cnt_16b <= rx_cnt_16b + 1;
  end

  if ( en_fast_autobaud == 1 ) begin
    rx_cnt_16b[15:12] <= 4'd0;
    rx_cnt_16b_p1    <= rx_cnt_16b[11];
    rx_cnt_16b_roll  <= rx_cnt_16b_p1 & ~ rx_cnt_16b[11];
  end else begin
    rx_cnt_16b_p1    <= rx_cnt_16b[15];
    rx_cnt_16b_roll  <= rx_cnt_16b_p1 & ~ rx_cnt_16b[15];
  end

  rx_cnt_16b[15:8] <= 8'd0;// 2016_10_17

 end
end // proc_cnt



//-----------------------------------------------------------------------------
// Autobaud off "\n" character. Tricky, as both start bit at D0 are 0.
// When there is no bit_lock, look for falling edge of RXD and count width of 
// StartBit and the 1st '0' of 0xA "\n" "01010000". 
// bit_lock remains on unless we don't get baud_lock 
// bit_lock  == Think we know where to sample each symbol.
// baud_lock == We sampled 8 symbols and got 0x0A or "\n"
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_s1 
 begin
  sample_first   <= 0;
  rx_start_a     <= 0;
  bit_lock_jk_p1 <= bit_lock_jk;
  if ( bit_lock_jk == 0 && baud_lock_jk == 0 ) begin
    if ( rxd_fal == 1 ) begin
      rx_start_a     <= 1;
      rx_cnt_en_a_jk <= 1;
    end else if ( rxd_ris == 1 && rx_cnt_en_a_jk == 1 ) begin
      baud_rate_loc  <= { 1'b0, rx_cnt_16b[15:1] } - 16'd1;// Div2 Shift
      bit_lock_jk    <= 1;
      rx_start_a     <= 1;
      rx_cnt_en_a_jk <= 0;
      sample_first   <= 1;
    end
  end else begin
    rx_cnt_en_a_jk <= 0;
  end

  if (   clr_bit_lock == 1 ) begin
    bit_lock_jk <= 0;
  end

  if ( en_fast_autobaud == 1 ) begin
    baud_rate_loc[15:12] <= 4'd0;
  end

//if ( en_autobaud == 0 ) begin
//  baud_rate_loc <= 16'd43;
//  bit_lock_jk   <= 1;
//end

// fix baud rate for smaller design. Note the subtract-2
//baud_rate_loc[15:0] <= 16'd109;// 100 MHz / 921600 baud
//baud_rate_loc[15:0] <= 16'd104;//  96 MHz / 921600 baud = WORKS
//baud_rate_loc[15:0] <= 16'd104;//  12 MHz / 115200 baud = WORKS 
  baud_rate_loc[15:0] <= 16'd24;//   24 MHz / 921600 baud = WORKS
//baud_rate_loc[15:0] <= 16'd12;//   12 MHz / 921600 baud = DOESNT WORK
//bit_lock_jk   <= 1;

 end
end // proc_s1 
  assign flush = rx_ascii_cr;


//-----------------------------------------------------------------------------
// Decode the character bytes
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_s3 
 begin
   rx_ascii_cr   <= 0;
   rx_bad_symbol <= 0;
   clr_bit_lock  <= 0; 
   rx_rdy_loc    <= 0;
   rx_rdy        <= rx_rdy_loc & baud_lock_jk;

   // Assert baud_lock if 1st char is "\n" else clear and start over.
   if ( sample_now_p1  == 1 && rx_bit_cnt == 4'd9 ) begin
     if ( rx_byte_loc[7:0] == 8'h0a ) begin
       baud_lock_jk <= 1;
       rx_ascii_cr  <= 1;
       clr_jk <= 0;
       bad_jk <= 0;
     end else if ( baud_lock_jk == 0 ) begin
       clr_bit_lock <= 1;
     end
     // Only expect ASCII 0x20 - 0x7F, detect 0x80+ as bad and clear baud lock
     if ( rx_byte_loc[7] == 1 ) begin     
       rx_bad_symbol <= 1;
     end
     // Only expect ASCII 0x20 - 0x7F, detect under 0x20 and clear baud lock
     if ( rx_byte_loc != 8'h0A && rx_byte_loc < 8'h20 ) begin
       rx_bad_symbol <= 1;
     end
   end

   // Grab received byte after last bit received
   if ( sample_now_p1  == 1 && rx_bit_cnt == 4'd9 ) begin
     rx_rdy_loc <= 1;
   end

   if ( reset == 1 || rx_bad_symbol == 1 || clr_baudlock == 1 ) begin
     baud_lock_jk <= 0;
     clr_bit_lock <= 1;
     rx_rdy_loc   <= 0;
   end

// if ( dbg_sample_togl != dbg_sample_togl_p1 ) begin
//   clr_jk <= 0;
//   bad_jk <= 0;
// end
   if ( rx_bad_symbol == 1 ) begin
     bad_jk <= 1;
   end 
   if ( clr_baudlock == 1 ) begin
     clr_jk <= 1;
   end 
   if ( reset == 1 ) begin
     clr_jk <= 0;
     bad_jk <= 0;
   end
   

 end
end // proc_s3 

  assign rx_byte_loc = rx_byte_sr[8:1];
  assign rx_byte     = rx_byte_sr[8:1];


//-----------------------------------------------------------------------------
// look for falling edge of rx_start_bit and count 1/2 way into bit and sample
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_s2 
 begin
  tx_en_loc     <= 0;
  sample_now    <= 0;
  rx_start_b    <= 0;
  rx_cnt_en_b   <= 0;
  rx_byte_jk_p1    <= rx_byte_jk;
  sample_now_p1 <= sample_now;
  dbg_sample_togl_p1 <= dbg_sample_togl;

  if ( rx_byte_jk == 0 ) begin
    rx_bit_cnt <= 4'd0;
    if ( rxd_fal == 1 ) begin
      rx_start_b <= 1;
      rx_byte_jk <= 1;// Starting a new Byte
      dbg_byte_togl <= ~dbg_byte_togl;
    end
  end

  // When to start sample_now is tricky as for bit_lock, it is 1/2 baud into
  // the D1 data bit of \n, after lock, it is 1/2 baud into D0 data bit.
  // cnt=0 is StartBit, cnt=1 is D0, cnt=2 is D1
  if ( bit_lock_jk == 1 && rx_byte_jk == 1 ) begin
    rx_cnt_en_b  <= 1;
    if ( 
         ( baud_lock_jk == 0 && rx_bit_cnt == 4'd2 ) ||
         ( baud_lock_jk == 1 && rx_bit_cnt == 4'd1 )    ) begin
      // Div-2 baud count to sample middle of eye
      if ( rx_cnt_16b[15:0] == { 1'b0, baud_rate_loc[15:1] } ) begin
        rx_bit_cnt <= rx_bit_cnt + 1;
        rx_start_b <= 1;
        sample_now <= 1;
      end
    end else if ( rx_cnt_16b[15:0] == baud_rate_loc[15:0] ) begin
      rx_bit_cnt <= rx_bit_cnt + 1;
      rx_start_b <= 1;
      sample_now <= 1;
    end
  end

  //  Assume "\n" and stuff the 1st 0 since it already flew by
  if ( sample_first == 1 ) begin
    rx_bit_cnt <= 4'd2;
    rx_byte_sr[8]   <= 0;
    rx_byte_sr[7:0] <= rx_byte_sr[8:1];
  end

  if ( sample_now == 1 ) begin
    rx_byte_sr[8]   <= rxd_sample;
    rx_byte_sr[7:0] <= rx_byte_sr[8:1];
    dbg_sample_togl <= ~dbg_sample_togl;
  end

  if ( sample_now_p1 == 1 && rx_bit_cnt == 4'd9  ) begin
    rx_byte_jk <= 0;
    rx_bit_cnt <= 4'd0;
  end

  if ( rx_rdy_loc == 1 && baud_lock_jk == 1 && en_loopback == 1 ) begin
    tx_byte_loc <= rx_byte_loc[7:0];  
    tx_en_loc   <= 1;
  end else if ( tx_en == 1 ) begin
    tx_byte_loc <= tx_byte[7:0];  
    tx_en_loc   <= 1;
  end

  if ( reset == 1 ) begin
    rx_byte_jk <= 0;
    rx_byte_sr[8:0] <= 9'd0;
    dbg_sample_togl <= 0;
    dbg_byte_togl <= 0;
  end

 end
end // proc_s2 


//-----------------------------------------------------------------------------
// TX : Load 8bits into 10bit SR and shift out at the RX baud rate    
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_tx 
 begin
  tx_shift <= 0;
  tx_busy  <= 0;
//txd      <= txd_loc;
  txd      <= tx_sr[0];
  txd_loc  <= tx_sr[0];
//txd_loc  <= tx_sr[0] | ~ baud_lock;
  tx_now   <= 0;

  // Load a new Byte to send
  if ( tx_en_loc == 1 ) begin
    tx_bit_cnt <= 4'd10;
    tx_cnt_16b <= 16'h0000;
    tx_idle    <= 0;
    tx_busy    <= 1;
    tx_shift   <= 1;
    tx_sr[9:0] <= { 1'b1, tx_byte_loc[7:0], 1'b0 };

  // Shift and send the byte until bit_cnt goes to 0
  end else if ( tx_bit_cnt != 4'd0 ) begin
    tx_cnt_16b <= tx_cnt_16b + 1;
    tx_busy    <= 1;
    tx_idle    <= 0;
    if ( tx_now == 1 ) begin
      tx_shift   <= 1;
      tx_bit_cnt <= tx_bit_cnt[3:0] - 1;
      tx_cnt_16b <= 16'h0000;
      tx_sr[9:0] <= { 1'b1, tx_sr[9:1] };
    end
  end else begin
    tx_sr <= 10'H3FF;
    if ( tx_now == 1 ) begin
      tx_idle <= 1;
    end else begin
      tx_cnt_16b <= tx_cnt_16b + 1;
    end
  end

  if ( tx_cnt_16b[15:0] == baud_rate_loc[15:0] ) begin
    tx_now <= 1;
  end

  if ( reset == 1 ) begin
    tx_bit_cnt <= 4'd0;
    tx_idle    <= 1;
  end

  if ( en_fast_autobaud == 1 ) begin
    tx_cnt_16b[15:12] <= 4'd0;
  end

 end
end // proc_tx


endmodule // mesa_uart
