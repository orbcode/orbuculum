/* ****************************************************************************
-- (C) Copyright 2016 Kevin M. Hubbard - All rights reserved.
-- Source file: sump2.v
-- Date:        July 2016
-- Author:      khubbard
-- Description: SUMP2 is a fast and simple logic analyzer.
--              It captures N/2 samples before and after a specified trigger
--              event where N is length of BRAM specified when instantiated. 
--              It is designed to scale easily from small and simple to wide 
--              and deep block RAM primiting. 
-- Language:    Verilog-2001
-- Simulation:  Mentor-Modelsim
-- Synthesis:   Xilinst-XST, Lattice-Synplify
-- License:     This project is licensed with the CERN Open Hardware Licence 
--              v1.2.  You may redistribute and modify this project under the 
--              terms of the CERN OHL v.1.2. (http://ohwr.org/cernohl). 
--              This project is distributed WITHOUT ANY EXPRESS OR IMPLIED 
--              WARRANTY, INCLUDING OF MERCHANTABILITY, SATISFACTORY QUALITY 
--              AND FITNESS FOR A PARTICULAR PURPOSE. Please see the CERN OHL
--              v.1.2 for applicable Conditions.
--
-- Design Instance Parameters with examples:
--   depth_len      =  1024 : Depth of RAM in number of cell, example 1024,4096
--   depth_bits     =  10   : Number of RAM address lines,    example 10,12
--   event_bytes    =  4    : Number of event bytes 1,2,3 or 4
--   data_dwords    =  16   : Number of DWORDs for Data 0,4,8,12 or 16
--
--   nonrle_en      =  1 : If 0, removes Event RAM (for small RLE only designs)
--   rle_en         =  1 : Adds logic for Event RLE captures.
--   pattern_en     =  1 : Adds logic for 32bit event pattern triggers
--   trigger_nth_en =  1 : Adds logic for triggering on Nth trigger event
--   trigger_dly_en =  1 : Adds logic for triggering after a delay from trigger
--
--   freq_mhz       =  16'd80   : Freq integer 0 MHz up to 65 GHz ( 16bit )
--   freq_fracts    =  16'h0000 : Freq fraction bits, example C000 = 0.75 MHz
--   sump_id        =  16'hABBA : Don't Change
--   sump_rev       =   8'h01   : Don't Change
--
-- LocalBus 2 DWORD Register Interface
--   lb_cs_ctrl : PCI addr sel for Control 
--   lb_cs_data : PCI addr sel for Data transfers. +0x4 offset from lb_cs_ctrl
--
-- Software Interface
-- 5bit Control Commands:
--   0x00 : Idle + Read Status
--   0x01 : ARM  + Read Status
--   0x02 : Reset
--              
--   0x04 : Load Trigger Type  ( AND,OR,Ext )
--   0x05 : Load Trigger Field ( AND/OR bits )
--   0x06 : Load Trigger Delay & nth        
--   0x07 : Load Trigger Position ( Num Post Trigger Samples to capture ).
--
--   0x08 : Load RLE Event Enable
--   0x09 : Load Read Pointer
--   0x0a : Load Read Page
--              
--   0x0b : Read HW ID + Revision
--   0x0c : Read HW Config RAM Width+Length
--   0x0d : Read HW Config Sample Clock Frequency
--   0x0e : Read Trigger Location
--   0x0f : Read RAM Data ( address auto incrementing )
--
--   0x10 : Load User Controls
--   0x11 : Load User Pattern0
--   0x12 : Load User Pattern1
--   0x13 : Load Data Enable Field
--
-- Trigger Types:
--   AND Rising            = 0x00;
--   AND Falling           = 0x01;
--   OR  Rising            = 0x02;
--   OR  Falling           = 0x03;
--   Pattern Rising        = 0x04;
--   Pattern Falling       = 0x05;
--   Input Trigger Rising  = 0x06;
--   Input Trigger Falling = 0x07;
--
-- Revision History:
-- Ver#  When      Who      What
-- ----  --------  -------- --------------------------------------------------
-- 0.1   07.01.16  khubbard Creation
-- ***************************************************************************/
`default_nettype none // Strictly enforce all nets to be declared


module sump2 #
(
   parameter depth_len      =  1024,
   parameter depth_bits     =  10,
   parameter event_bytes    =  4,
   parameter data_dwords    =  16,

   parameter nonrle_en      =  1,
   parameter rle_en         =  1,
   parameter pattern_en     =  1,
   parameter trigger_nth_en =  1,
   parameter trigger_dly_en =  1,
   parameter trigger_wd_en  =  1,

   parameter freq_mhz       =  16'd80,
   parameter freq_fracts    =  16'h0000,
   parameter sump_id        =  16'hABBA,
   parameter sump_rev       =   8'h01
)
(
  input  wire         reset,
  input  wire         clk_cap,
  input  wire         clk_lb,
  input  wire         lb_cs_ctrl,
  input  wire         lb_cs_data,
  input  wire         lb_wr,
  input  wire         lb_rd,
  input  wire [31:0]  lb_wr_d,
  output reg  [31:0]  lb_rd_d,
  output reg          lb_rd_rdy,
  output reg          active,

  output wire [31:0]  user_ctrl,
  output wire [31:0]  user_pat0,
  output wire [31:0]  user_pat1,

  input  wire         trigger_in,
  output reg          trigger_out,

  input  wire [31:0]  events_din,

  input  wire [127:0] dwords_3_0,
  input  wire [127:0] dwords_7_4,
  input  wire [127:0] dwords_11_8,
  input  wire [127:0] dwords_15_12,
  output reg  [3:0]   led_bus
);


  wire                    reset_loc;
  wire [15:0]             zeros;
  wire [15:0]             ones;
  wire [7:0]              cap_status;
  wire [4:0]              ctrl_cmd;
  reg  [4:0]              ctrl_cmd_loc;
  reg                     ctrl_cmd_xfer;
  wire [4:0]              ctrl_rd_page;
  wire [15:0]             ctrl_rd_ptr;
  wire                    ctrl_trigger_edge;
  wire [4:0]              ctrl_trigger;
  reg  [4:0]              ctrl_trigger_p1;
  reg  [4:0]              ctrl_reg;
  reg  [31:0]             events_loc;
  reg                     trigger_loc;
  reg                     trigger_or;
  reg                     trigger_or_p1;
  reg                     trigger_and;
  reg                     trigger_and_p1;
  reg                     trigger_pat;
  reg                     trigger_pat_p1;
  reg                     xfer_clr;
  reg                     rd_inc;
  reg                     armed_jk;
  reg                     triggered_jk;
  reg                     acquired_jk;
  reg                     complete_jk;

  reg  [31:0]             ctrl_04_reg;
  reg  [31:0]             ctrl_05_reg;
  reg  [31:0]             ctrl_06_reg;
  reg  [31:0]             ctrl_07_reg;
  reg  [31:0]             ctrl_08_reg;
  reg  [31:0]             ctrl_09_reg;
  reg  [31:0]             ctrl_0a_reg;
  reg  [31:0]             ctrl_0b_reg;
  reg  [31:0]             ctrl_10_reg;
  reg  [31:0]             ctrl_11_reg;
  reg  [31:0]             ctrl_12_reg;
  reg  [31:0]             ctrl_13_reg;

  reg  [31:0]             ctrl_14_reg;
  wire [31:0]             watchdog_reg;
  reg  [31:0]             watchdog_cnt;
  reg                     wd_armed_jk;
  reg                     trigger_wd;

  wire [31:0]             trigger_bits;
  wire [3:0]              trigger_type;
  wire [31:0]             trigger_pos;
  wire [15:0]             trigger_nth;
  wire [15:0]             trigger_delay;
  reg  [15:0]             trigger_dly_cnt;
  wire [31:0]             rle_event_en;
  reg  [15:0]             trigger_cnt;
  reg  [31:0]             ram_rd_d;
  reg  [depth_bits-1:0]   post_trig_cnt;
  reg  [depth_bits-1:0]   trigger_ptr;
  reg                     trigger_in_meta;
  reg                     trigger_in_p1;
  reg                     trigger_in_p2;
  wire [31:0]             pat0;
  wire [31:0]             pat1;

// Variable Size Capture BRAM
  reg  [31:0]             event_ram_array[depth_len-1:0];
  reg  [127:0]            dwords_3_0_ram_array[depth_len-1:0];
  reg  [127:0]            dwords_7_4_ram_array[depth_len-1:0];
  reg  [127:0]            dwords_11_8_ram_array[depth_len-1:0];
  reg  [127:0]            dwords_15_12_ram_array[depth_len-1:0];

  reg  [127:0]            dwords_3_0_p1;
  reg  [127:0]            dwords_7_4_p1;
  reg  [127:0]            dwords_11_8_p1;
  reg  [127:0]            dwords_15_12_p1;

  reg  [127:0]            dwords_3_0_p2;
  reg  [127:0]            dwords_7_4_p2;
  reg  [127:0]            dwords_11_8_p2;
  reg  [127:0]            dwords_15_12_p2;

  reg  [127:0]            dwords_3_0_do;
  reg  [127:0]            dwords_7_4_do;
  reg  [127:0]            dwords_11_8_do;
  reg  [127:0]            dwords_15_12_do;

  reg  [depth_bits-1:0]   c_addr;
  reg  [depth_bits-1:0]   c_addr_p1;
  reg                     c_we;
  reg                     c_we_p1;
  wire [31:0]             c_di;
  reg  [31:0]             c_di_p1;
  reg  [depth_bits-1:0]   d_addr;
  reg  [31:0]             d_do;

  reg  [63:0]             rle_ram_array[depth_len-1:0];
  reg  [depth_bits-1:0]   a_addr;
  reg  [depth_bits-1:0]   a_addr_p1;
  reg                     a_we;
  reg                     a_we_p1;
  reg  [63:0]             a_di;
  reg  [63:0]             a_di_p1;
  reg  [depth_bits-1:0]   b_addr;
  reg  [63:0]             b_do;

  wire [31:0]             data_en_bits;
  reg                     data_en_loc;
  reg                     data_en_loc_p1;
  reg  [31:0]             events_pre;
  reg  [31:0]             events_p1;
  reg  [31:0]             events_p2;
  reg  [31:0]             rle_time;
  reg  [31:0]             rle_time_p1;
  reg                     rle_wd_sample;
  reg                     rle_pre_jk;
  reg                     rle_done_jk;
  wire [7:0]              data_4x_dwords;


  assign zeros = 16'd0;
  assign ones  = 16'hFFFF;
  assign reset_loc = reset;

 
  assign cap_status = { rle_en, 1'b0, rle_done_jk, ~rle_pre_jk,
                        complete_jk, acquired_jk, triggered_jk, armed_jk }; 


//-----------------------------------------------------------------------------
// Flop the input events and support reduction for much smaller designs.
//-----------------------------------------------------------------------------
always @ ( posedge clk_cap ) begin : proc_din 
  if          ( event_bytes == 1 ) begin
    events_loc <= { 24'd0, events_din[7:0] };
  end else if ( event_bytes == 2 ) begin
    events_loc <= { 16'd0, events_din[15:0] };
  end else if ( event_bytes == 3 ) begin
    events_loc <= {  8'd0, events_din[23:0] };
  end else begin
    events_loc <= {        events_din[31:0] };
  end

  if ( rle_en == 1 ) begin
    events_pre <= events_loc[31:0] & rle_event_en[31:0];
    events_p1  <= events_pre[31:0];
    events_p2  <= events_p1[31:0];
  end 
end // proc_din


//-----------------------------------------------------------------------------
// Capture Logic. When armed, capture in a continuous loop until trigger is
// detected and then capture N/2 samples more.
// Note: Software Must go from Idle to Arm to clear the JKs.
//-----------------------------------------------------------------------------
always @ ( * ) begin : proc_data_en
  if ( data_en_bits == 32'h00000000 ||
       ( data_en_bits[31:0] & events_din[31:0] ) != 32'h00000000 ) begin
    data_en_loc <= 1;// Capture sample in time 
  end else begin
    data_en_loc <= 0;// Prevent sample capture
  end
end // proc_data_en


//-----------------------------------------------------------------------------
// Capture Logic. When armed, capture in a continuous loop until trigger is
// detected and then capture N/2 samples more.
// Note: Software Must go from Idle to Arm to clear the JKs.
//-----------------------------------------------------------------------------
integer i;
always @ ( posedge clk_cap ) begin : proc_trig 
  trigger_in_meta <= trigger_in;
  trigger_in_p1   <= trigger_in_meta;
  trigger_in_p2   <= trigger_in_p1;
  trigger_or      <= 0;
  trigger_and     <= 0;
  trigger_pat     <= 0;
  trigger_or_p1   <= trigger_or;
  trigger_and_p1  <= trigger_and;
  trigger_pat_p1  <= trigger_pat;
  data_en_loc_p1  <= data_en_loc;

  for ( i = 0; i <= 31; i=i+1 ) begin
    if ( trigger_bits[i] == 1 && events_loc[i] == 1 ) begin
      trigger_or <= 1;// This is a 32bit OR
    end
  end
  
  if ( ( events_loc[31:0] & trigger_bits[31:0] ) == trigger_bits[31:0] ) begin
    trigger_and <= 1;
  end 

  if ( 
      (( events_loc[31:0] & pat0[31:0] ) ^  
       ( pat1[31:0]       & pat0[31:0] )   ) == 32'h00000000  ) begin

    if ( pattern_en == 1 ) begin
      trigger_pat <= 1;// Exact 32bit Pattern Match
    end 
  end 
 
end // proc_trig


//-----------------------------------------------------------------------------
// Capture Logic. When armed, capture in a continuous loop until trigger is
// detected and then capture N/2 samples more.
// Note: Software Must go from Idle to Arm to clear the JKs.
//-----------------------------------------------------------------------------
always @ ( posedge clk_cap ) begin : proc_cap  
  c_we        <= 0;
  trigger_out <= 0;
  trigger_loc <= 0;
  trigger_wd  <= 0;
  active      <= 0;

  // CMD_ARM   
  if ( ctrl_cmd_loc == 5'h01 ) begin
    active <= 1;

    // Watchdog gets armed on 1st kick. Every kick after clears count.
    // If count expires, assert trigger_wd.
    if ( trigger_wd_en == 1 && ( trigger_or != trigger_or_p1 ) ) begin
      wd_armed_jk <= 1;
    end
    if ( wd_armed_jk == 0 || ( trigger_or != trigger_or_p1 ) ) begin
      watchdog_cnt <= 32'd0;
      trigger_wd   <= 0;
    end else begin
      watchdog_cnt <= watchdog_cnt[31:0] + 1;
      if ( watchdog_cnt == watchdog_reg[31:0] ) begin
        trigger_wd  <= 1;
        wd_armed_jk <= 0;
      end  
    end 

    if ( triggered_jk == 0 && acquired_jk == 0 ) begin
      // PreTrigger Acquire
      armed_jk <= 1;
      if ( data_en_loc == 1 || data_en_loc_p1 == 1 ) begin
        c_we     <= 1;
        c_addr   <= c_addr + 1;
      end

      if ( trigger_dly_cnt != 16'hFFFF ) begin
        trigger_dly_cnt <= trigger_dly_cnt + 1;
      end

      if ( ( trigger_type==4'h0 && trigger_and==1    && trigger_and_p1==0 ) ||
           ( trigger_type==4'h1 && trigger_and==0    && trigger_and_p1==1 ) ||
           ( trigger_type==4'h2 && trigger_or ==1    && trigger_or_p1 ==0 ) ||
           ( trigger_type==4'h3 && trigger_or ==0    && trigger_or_p1 ==1 ) ||
           ( trigger_type==4'h4 && trigger_pat==1    && trigger_pat_p1==0 ) ||
           ( trigger_type==4'h5 && trigger_pat==0    && trigger_pat_p1==1 ) ||
           ( trigger_type==4'h6 && trigger_in_p1 ==1 && trigger_in_p2 ==0 ) ||
           ( trigger_type==4'h7 && trigger_in_p1 ==0 && trigger_in_p2 ==1 ) ||
           ( trigger_type==4'h8 && trigger_wd == 1                        )
         ) begin
        if ( trigger_dly_cnt == 16'hFFFF || trigger_dly_en==0 ) begin
          trigger_dly_cnt <= 16'd0;
          trigger_loc     <= 1;// Only used if trigger delay is removed
          c_we            <= 1;// Store Trigger even if data_en_loc == 0
          c_addr          <= c_addr + 1;
        end
      end

      // Don't allow trigger until pre-trig buffer is full
      if ( complete_jk == 1 ) begin
        if ( ( trigger_dly_cnt == trigger_delay[15:0] ) || 
             (  trigger_dly_en==0 && trigger_loc == 1 ) ) begin
          trigger_dly_cnt <= 16'hFFFF;
          if ( trigger_cnt == trigger_nth[15:0] || trigger_nth_en==0 ) begin
            armed_jk     <= 0;
            trigger_ptr  <= c_addr[depth_bits-1:0];
            trigger_out  <= 1;
            triggered_jk <= 1;
          end
          trigger_cnt <= trigger_cnt + 1;
        end
      end

    end else if ( triggered_jk == 1 && acquired_jk == 0 ) begin
      // PostTrigger Acquire
      trigger_out <= 1;
      if ( data_en_loc == 1 || data_en_loc_p1 == 1 ) begin
        c_we          <= 1;
        c_addr        <= c_addr + 1;
        post_trig_cnt <= post_trig_cnt + 1;
      end
      if ( post_trig_cnt == trigger_pos[depth_bits-1:0] ) begin
        acquired_jk <= 1;
        c_we        <= 0;
      end 
    end

    // If RAM has rolled, then pre-trigger buffer is full. Assert status bit.
    // If RAM hasn't rolled, then pre-trigger samples start at 0x0.
    if ( c_addr[depth_bits-1] == 0 && c_addr_p1[depth_bits-1] == 1 ) begin
      complete_jk <= 1;
    end

  // CMD_RESET
  end else if ( ctrl_cmd_loc == 5'h02 ) begin
    c_addr             <= zeros[depth_bits-1:0];
    post_trig_cnt      <= zeros[depth_bits-1:0];
    post_trig_cnt[1:0] <= 2'b11;// Subtracts 3 from trigger_pos for alignment
    trigger_cnt        <= 16'd1;
    trigger_dly_cnt    <= 16'hFFFF;
    armed_jk           <= 0;
    triggered_jk       <= 0;
    acquired_jk        <= 0;
    complete_jk        <= 0;
    wd_armed_jk        <= 0;
  end

  // Cleanly xfer clock domains
  xfer_clr <= 0;
  if ( ctrl_cmd_xfer == 1 ) begin
    ctrl_cmd_loc <= ctrl_cmd[4:0];
    xfer_clr     <= 1;
  end

end // proc_cap
  assign c_di = events_loc[31:0];


//-----------------------------------------------------------------------------
// led_bus : Spins while waiting for trigger event. 2 Solids when done.
//-----------------------------------------------------------------------------
always @ ( posedge clk_cap ) begin : proc_led_bus
  if ( rle_done_jk == 0 ) begin
    led_bus <= 4'h0;
  end
  if ( ctrl_cmd_loc == 5'h01 ) begin
    if          ( rle_time[25:24] == 2'd0 ) begin
      led_bus <= 4'h1;
    end else if ( rle_time[25:24] == 2'd1 ) begin
      led_bus <= 4'h2;
    end else if ( rle_time[25:24] == 2'd2 ) begin
      led_bus <= 4'h4;
    end else begin
      led_bus <= 4'h8;
    end 
    // After Trigger, but before completion, only flash Left and Right
    if ( triggered_jk == 1 ) begin
      led_bus[2] <= 1'b0;
      led_bus[0] <= 1'b0;
    end
  end
  if ( rle_done_jk == 1 ) begin
    led_bus <= 4'hA;
  end
end


//-----------------------------------------------------------------------------
// RLE Capture Logic. This captures and stores event changes along with 
// time stamps to a x64 BRAM. 1st half of RAM is any pre-trigger activity.
// 2nd half of RAM is post-trigger activity. Pre-trig is circular and must
// be unrolled by software in correct order. Enabling RLE block is optional.
//-----------------------------------------------------------------------------
always @ ( posedge clk_cap ) begin : proc_rle  
  a_we        <= 0;
  rle_time_p1 <= rle_time[31:0];
  // Prevent RLE from hanging in cases where no activity happens after the
  // trigger event by storing a non-changing sample periodically every
  // 2^24 clock cycles about 100ms at 100 MHz 
  // 2^20 clock cycles about 10ms at 100 MHz x 1024 RAM = 10Sec
  // rle_wd_sample  <= rle_time[23] & ~ rle_time_p1[23];
  rle_wd_sample  <= rle_time[19] & ~ rle_time_p1[19];// About 5sec

  // CMD_ARM   
  if ( ctrl_cmd_loc == 5'h01 ) begin
    rle_time <= rle_time[31:0] + 1;
    if ( triggered_jk == 0 ) begin
      a_addr[depth_bits-1] <= 0;// Pre-Trigger Half
      // If the prebuffer is invalid, store everything, change or no change
      // as to immediately fill up RAM with valid samples
      // Once prebuffer is valid, only store event deltas ( RLE )
      if ( rle_pre_jk == 1 || rle_wd_sample == 1 || 
           ( events_p1 != events_p2[31:0] ) ) begin
        a_we <= 1;
        a_addr[depth_bits-2:0] <= a_addr[depth_bits-2:0] + 1; 
        if ( a_addr[depth_bits-2:0] == ones[depth_bits-2:0] ) begin
          rle_pre_jk <= 0;// PreBuffer is completely valid - and rolling
        end
      end
    end else if ( triggered_jk == 1 && rle_done_jk == 0 ) begin
      if ( ( events_p1 != events_p2[31:0] ) || ( rle_wd_sample == 1) ) begin
        a_we <= 1;
        a_addr[depth_bits-2:0] <= a_addr[depth_bits-2:0] + 1; 
        // If previous write was to last address in RAM, then call it quits
        if ( a_addr[depth_bits-2:0] == ones[depth_bits-2:0] ) begin
          rle_done_jk            <= 1;// Post-Trig RAM is full
          a_we                   <= 0;
          a_addr[depth_bits-2:0] <= a_addr[depth_bits-2:0];
        end
        // If previous cycle was pre-trig, set address to start of post trig
        if ( a_addr[depth_bits-1] == 0 ) begin
          a_addr[depth_bits-1]   <= 1;// Post-Trigger Half
          a_addr[depth_bits-2:0] <= zeros[depth_bits-2:0];
        end
      end
    end

  // CMD_RESET
  end else if ( ctrl_cmd_loc == 5'h02 ) begin
    rle_time    <= 32'd0;// 43 seconds at 100 MHz
    a_addr      <= zeros[depth_bits-1:0];
    rle_pre_jk  <= 1;
    rle_done_jk <= 0;
  end
  a_di[31:0]  <= events_p1[31:0];
  a_di[63:32] <= rle_time[31:0];
end // proc_rle
//assign a_di[31:0]  = events_p1[31:0];
//assign a_di[63:32] = rle_time[31:0];


//-----------------------------------------------------------------------------
// LocalBus Write Ctrl register
//-----------------------------------------------------------------------------
always @ ( posedge clk_lb ) begin : proc_lb_wr
  if ( lb_wr == 1 && lb_cs_ctrl == 1 ) begin
    ctrl_reg[4:0] <= lb_wr_d[4:0];
    ctrl_cmd_xfer <= 1;
  end 

  if ( lb_wr == 1 && lb_cs_data == 1 ) begin
    case( ctrl_cmd[4:0] )
      5'h04 : ctrl_04_reg <= lb_wr_d[31:0];
      5'h05 : ctrl_05_reg <= lb_wr_d[31:0];
      5'h06 : ctrl_06_reg <= lb_wr_d[31:0];
      5'h07 : ctrl_07_reg <= lb_wr_d[31:0];

      5'h08 : ctrl_08_reg <= lb_wr_d[31:0];
      5'h09 : ctrl_09_reg <= lb_wr_d[31:0];
      5'h0A : ctrl_0a_reg <= lb_wr_d[31:0];
      5'h0B : ctrl_0b_reg <= lb_wr_d[31:0];

      5'h10 : ctrl_10_reg <= lb_wr_d[31:0];
      5'h11 : ctrl_11_reg <= lb_wr_d[31:0];
      5'h12 : ctrl_12_reg <= lb_wr_d[31:0];
      5'h13 : ctrl_13_reg <= lb_wr_d[31:0];
      5'h14 : ctrl_14_reg <= lb_wr_d[31:0];
    endcase
  end

  if ( xfer_clr == 1 ) begin
    ctrl_cmd_xfer  <= 0;
  end

  if ( ctrl_cmd == 5'h01 ) begin
    d_addr  <= c_addr[depth_bits-1:0];// When Acq stops, d_addr will be last 
  end
  if ( lb_wr == 1 && lb_cs_data == 1 && ctrl_cmd == 5'h09 ) begin
    d_addr <= lb_wr_d[depth_bits-1:0];// Load user specified address
  end 
  if ( rd_inc == 1 ) begin
    d_addr  <= d_addr[depth_bits-1:0] + 1;// Auto Increment on each read
  end 

  if ( reset_loc == 1 ) begin
    ctrl_reg[4:0] <= 5'd0;
    ctrl_cmd_xfer <= 1;// Flag to xfer ctrl_reg into other clock domain
  end 
end

  assign ctrl_cmd[4:0]     = ctrl_reg[4:0];

  assign trigger_type      = ctrl_04_reg[3:0];
  assign trigger_bits      = ctrl_05_reg[31:0];
  assign trigger_nth       = ctrl_06_reg[15:0];
  assign trigger_delay     = ctrl_06_reg[31:16];
  assign trigger_pos       = ctrl_07_reg[31:0];
  assign rle_event_en      = ctrl_08_reg[31:0];

  assign user_ctrl[31:0]   = ctrl_10_reg[31:0];
  assign user_pat0[31:0]   = ctrl_11_reg[31:0];
  assign user_pat1[31:0]   = ctrl_12_reg[31:0];
  assign pat0[31:0]        = ctrl_11_reg[31:0];
  assign pat1[31:0]        = ctrl_12_reg[31:0];
  assign data_en_bits[31:0] = ctrl_13_reg[31:0];
  assign watchdog_reg[31:0] = ctrl_14_reg[31:0];

  assign ctrl_rd_ptr[15:0] = ctrl_09_reg[15:0];
  assign ctrl_rd_page[4:0] = ctrl_0a_reg[4:0];

  assign data_4x_dwords = data_dwords;


//-----------------------------------------------------------------------------
// LocalBus readback of ctrl_reg and data_reg
//-----------------------------------------------------------------------------
always @ ( posedge clk_lb ) begin : proc_lb_rd
  lb_rd_d   <= 32'd0;
  lb_rd_rdy <= 0;
  rd_inc    <= 0;

  if ( lb_rd == 1 && lb_cs_ctrl == 1 ) begin
    lb_rd_d[4:0] <= ctrl_reg[4:0];
    lb_rd_rdy    <= 1;
  end 

  if ( lb_rd == 1 && lb_cs_data == 1 ) begin
    lb_rd_rdy <= 1;
    if ( ctrl_cmd == 5'h00 ||
         ctrl_cmd == 5'h01    
       ) begin
      lb_rd_d[7:0] <= cap_status[7:0];
    end
    if ( ctrl_cmd == 5'h0b ) begin
      lb_rd_d[31:16] <= sump_id;// Identification
      lb_rd_d[15:8]  <= sump_rev;// Revision
      lb_rd_d[7:5]   <= 3'd0;
      lb_rd_d[4]     <= ~ nonrle_en;// Invert to disable backwards SW comptbl
      lb_rd_d[3]     <= rle_en;
      lb_rd_d[2]     <= pattern_en;
      lb_rd_d[1]     <= trigger_nth_en;
      lb_rd_d[0]     <= trigger_dly_en;
    end
    if ( ctrl_cmd == 5'h0c ) begin
      lb_rd_d[31:28] <= rle_en     ;// 1 if RLE RAM exists
      lb_rd_d[27:24] <= event_bytes;// How Many Event Bytes 1-4
      lb_rd_d[23:16] <= data_4x_dwords[5:2];// How Many 32bit BRAMs data 4x 
      lb_rd_d[15:0]  <= depth_len;  // How deep RAMs are
    end
    if ( ctrl_cmd == 5'h0d ) begin
      lb_rd_d[15:0]  <= freq_fracts;// Fractional MHz bits 1/2,1/4,etc.
      lb_rd_d[31:16] <= freq_mhz   ;// Integer MHz
    end
    if ( ctrl_cmd == 5'h0e ) begin
      lb_rd_d[depth_bits-1:0] <= trigger_ptr[depth_bits-1:0];// Where Trig Is
    end
    if ( ctrl_cmd == 5'h0f ) begin
      lb_rd_d <= ram_rd_d[31:0];
      rd_inc  <= 1;// Auto Increment RAM Address
    end
  end 

  if ( data_dwords != 0 ) begin
    // Mux between the BRAMs
    case( ctrl_rd_page[4:0] )
      5'H02   : ram_rd_d <= b_do[31:0];   // RLE Data
      5'H03   : ram_rd_d <= b_do[63:32];  // RLE Time

      5'H10   : ram_rd_d <= dwords_3_0_do[31:0];
      5'H11   : ram_rd_d <= dwords_3_0_do[63:32];
      5'H12   : ram_rd_d <= dwords_3_0_do[95:64];
      5'H13   : ram_rd_d <= dwords_3_0_do[127:96];

      5'H14   : ram_rd_d <= dwords_7_4_do[31:0];
      5'H15   : ram_rd_d <= dwords_7_4_do[63:32];
      5'H16   : ram_rd_d <= dwords_7_4_do[95:64];
      5'H17   : ram_rd_d <= dwords_7_4_do[127:96];

      5'H18   : ram_rd_d <= dwords_11_8_do[31:0];
      5'H19   : ram_rd_d <= dwords_11_8_do[63:32];
      5'H1a   : ram_rd_d <= dwords_11_8_do[95:64];
      5'H1b   : ram_rd_d <= dwords_11_8_do[127:96];

      5'H1c   : ram_rd_d <= dwords_15_12_do[31:0];
      5'H1d   : ram_rd_d <= dwords_15_12_do[63:32];
      5'H1e   : ram_rd_d <= dwords_15_12_do[95:64];
      5'H1f   : ram_rd_d <= dwords_15_12_do[127:96];

      default : ram_rd_d <= d_do[31:0];    // Events
    endcase
  end else begin
    // Mux between the BRAMs
    case( ctrl_rd_page[4:0] )
      5'H02   : ram_rd_d <= b_do[31:0]; // RLE Data
      5'H03   : ram_rd_d <= b_do[63:32];// RLE Time
      default : ram_rd_d <= d_do[31:0]; // Events
    endcase
  end 

end // proc_lb_rd


//-----------------------------------------------------------------------------
// Data Dual Port RAM - Infer RAM here to make easy to change depth on the fly
//-----------------------------------------------------------------------------
always @( posedge clk_cap  )
begin
  c_we_p1   <= c_we;
  c_addr_p1 <= c_addr;
  c_di_p1   <= c_di;
  if ( c_we_p1 ) begin
    if ( nonrle_en == 1 ) begin
      event_ram_array[c_addr_p1] <= c_di_p1;
    end 
  end // if ( c_we )

  dwords_3_0_p1   <= dwords_3_0[127:0];
  dwords_7_4_p1   <= dwords_7_4[127:0];
  dwords_11_8_p1  <= dwords_11_8[127:0];
  dwords_15_12_p1 <= dwords_15_12[127:0];

  dwords_3_0_p2   <= dwords_3_0_p1[127:0];
  dwords_7_4_p2   <= dwords_7_4_p1[127:0];
  dwords_11_8_p2  <= dwords_11_8_p1[127:0];
  dwords_15_12_p2 <= dwords_15_12_p1[127:0];

  if ( c_we_p1 ) begin
    if ( data_dwords >= 4 ) begin
      dwords_3_0_ram_array[ c_addr_p1 ]  <= dwords_3_0_p2[127:0];
    end
    if ( data_dwords >= 8 ) begin
      dwords_7_4_ram_array[ c_addr_p1 ]  <= dwords_7_4_p2[127:0];
    end
    if ( data_dwords >= 12 ) begin
      dwords_11_8_ram_array[ c_addr_p1 ] <= dwords_11_8_p2[127:0];
    end
    if ( data_dwords >= 16 ) begin
      dwords_15_12_ram_array[ c_addr_p1 ] <= dwords_15_12_p2[127:0];
    end
  end // if ( c_we )
end // always


//-----------------------------------------------------------------------------
// 2nd Port of RAM is clocked from local bus
//-----------------------------------------------------------------------------
always @( posedge clk_lb )
begin
  if ( nonrle_en == 1 ) begin
    d_do    <= event_ram_array[d_addr] ;
  end

  if ( data_dwords >= 4 ) begin
    dwords_3_0_do   <= dwords_3_0_ram_array[ d_addr ];
  end
  if ( data_dwords >= 8 ) begin
    dwords_7_4_do   <= dwords_7_4_ram_array[ d_addr ];
  end
  if ( data_dwords >= 12 ) begin
    dwords_11_8_do  <= dwords_11_8_ram_array[ d_addr ];
  end
  if ( data_dwords >= 16 ) begin
    dwords_15_12_do <= dwords_15_12_ram_array[ d_addr ];
  end
end // always


//-----------------------------------------------------------------------------
// RLE Dual Port RAM - Infer RAM here to make easy to change depth on the fly
//-----------------------------------------------------------------------------
always @( posedge clk_cap  )
begin
  if ( rle_en == 1 ) begin
    a_we_p1   <= a_we;
    a_addr_p1 <= a_addr;
    a_we_p1   <= a_we;
    a_addr_p1 <= a_addr;
    a_di_p1   <= a_di;
//  if ( a_we_p1 ) begin
//    rle_ram_array[a_addr_p1] <= a_di_p1;
//  end // if ( a_we )
    if ( a_we    ) begin
      rle_ram_array[a_addr   ] <= a_di;
    end // if ( a_we )
  end
end // always


//-----------------------------------------------------------------------------
// 2nd Port of RAM is clocked from local bus
//-----------------------------------------------------------------------------
always @( posedge clk_lb )
begin
  if ( rle_en == 1 ) begin
    b_do <= rle_ram_array[d_addr];
  end
end // always


endmodule // sump2
