/* ****************************************************************************
-- (C) Copyright 2015 Kevin M. Hubbard @ Black Mesa Labs
-- Source file: mesa2lb.v                
-- Date:        October 4, 2015   
-- Author:      khubbard
-- Language:    Verilog-2001 
-- Description: The Mesa Bus to LocalBus translator. Uses a Subslot for LB
--              Reads and Writes using 4 different command nibbles. Also
--              decodes the BSP subslots for things like pin control, power
--              modes and scope out debug features.
-- License:     This project is licensed with the CERN Open Hardware Licence
--              v1.2.  You may redistribute and modify this project under the
--              terms of the CERN OHL v.1.2. (http://ohwr.org/cernohl).
--              This project is distributed WITHOUT ANY EXPRESS OR IMPLIED
--              WARRANTY, INCLUDING OF MERCHANTABILITY, SATISFACTORY QUALITY
--              AND FITNESS FOR A PARTICULAR PURPOSE. Please see the CERN OHL
--              v.1.2 for applicable Conditions.
--
--
--    "\n"..."FFFF"."(F0-12-34-04)[11223344]\n" : 
--        0xFF = Bus Idle ( NULLs )
--  B0    0xF0 = New Bus Cycle to begin ( Nibble and bit orientation )
--  B1    0x12 = Slot Number, 0xFF = Broadcast all slots, 0xFE = NULL Dest
--  B2    0x3  = Sub-Slot within the chip (0-0xF)
--        0x4  = Command Nibble for Sub-Slot
--  B3    0x04 = Number of Payload Bytes (0-255)
--        0x11223344 = Payload
--
--    Slot 0xFF    = Broadcast all slots
--    Sub-Slot 0x0 = User Local Bus Access
--    Sub-Slot 0xE = PROM Local Bus Access
--                   0x0 = Bus Write
--                   0x1 = Bus Read
--                   0x2 = Bus Write Repeat ( burst to single address )
--                   0x3 = Bus Read  Repeat ( burst read from single address )
--    Sub-Slot 0xF = Power and Pin Control ( Write Only )
-- 
-- SubSlot=0 : LocalBus
-- Command
--       0 : Write
--       1 : Read
--
-- Ro : 0xF0FE3404 : 
--      0xFF = Bus Idle ( NULLs )
--  B0  0xF0 = New Bus Cycle to begin ( Nibble and bit orientation )
--  B1  0xFE = Slot Number - Default is 0xFE for Ro traffic
--  B2  0x3  = Sub-Slot within the chip (0-0xF)
--      0x4  = Command Nibble for Sub-Slot
--  B3  0x04 = Number of Payload Bytes (0-255)
--     
--
-- Revision History:
-- Ver#  When      Who      What
-- ----  --------  -------- ---------------------------------------------------
-- 0.1   10.04.15  khubbard Creation
-- ***************************************************************************/
`default_nettype none // Strictly enforce all nets to be declared
                                                                                
module mesa2lb
(
  input  wire         clk,
  input  wire         reset,
  input  wire         rx_byte_start,
  input  wire         rx_byte_stop,
  input  wire         rx_byte_rdy,
  input  wire [7:0]   rx_byte_d,
  output wire [7:0]   tx_byte_d,
  output wire         tx_byte_rdy,
  output reg          tx_done,
  input  wire         tx_busy,
  output reg          lb_wr,
  output wire         lb_rd,
  output reg  [31:0]  lb_wr_d,
  output reg  [31:0]  lb_addr,
  input  wire [31:0]  lb_rd_d,
  input  wire         lb_rd_rdy,
  input  wire         oob_en,
  input  wire [31:0]  oob_rd_d,
  input  wire         oob_rd_rdy,
  output reg          prom_wr,
  output wire         prom_rd,
  output reg  [31:0]  prom_wr_d,
  output reg  [31:0]  prom_addr,
  input  wire [31:0]  prom_rd_d,
  input  wire         prom_rd_rdy
); // module mesa2lb


  reg           lb_rd_loc;
  reg [3:0]     byte_cnt;
  reg [31:0]    addr_cnt;
  reg [31:0]    addr_len;
  reg [31:0]    dword_sr;
  reg           rx_byte_rdy_p1;
  reg           rx_byte_rdy_p2;
  reg           rx_byte_rdy_p3;
  reg           rx_byte_stop_p1;
  reg           rx_byte_stop_p2;
  reg           rx_byte_stop_p3;
  reg           header_jk;
  reg           wr_jk;
  reg           rd_jk;
  reg           rpt_jk;
  reg [2:0]     dword_stage;
  reg           dword_rdy;
  reg           reading;
  reg           rd_done;
  reg           rd_done_p1;
  reg [31:0]    lb_rd_sr;
  reg [3:0]     lb_rd_fsm;
  reg           rd_busy;
  reg [7:0]     rd_byte_d;
//reg [7:0]     rd_timeout_cnt;
//reg           rd_timeout_cnt_p1;
  reg           rd_byte_rdy;
  reg           lb_user_jk;
  reg           lb_prom_jk;
  reg           ro_header_rdy;


// TODO: Write and Read burst Lengths should be reduced from 32bits for timing
// TODO: A new cycle should always clear wr_jk and rd_jk in case a read timeout
  assign tx_byte_d   = rd_byte_d[7:0];
  assign tx_byte_rdy = rd_byte_rdy;




//-----------------------------------------------------------------------------
// Shift a nibble into a byte shift register. 
//        |---- Header ----|--- Payload ---|
//           0   1   2   3
// Write : <F0><00><00><08>[<ADDR><DATA>]
// Read  : <F0><00><00><08>[<ADDR><Length>]
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_lb1
 rx_byte_rdy_p1  <= rx_byte_rdy;
 rx_byte_rdy_p2  <= rx_byte_rdy_p1;
 rx_byte_rdy_p3  <= rx_byte_rdy_p2;
 rx_byte_stop_p1 <= rx_byte_stop;
 rx_byte_stop_p2 <= rx_byte_stop_p1;
 rx_byte_stop_p3 <= rx_byte_stop_p2;
 dword_rdy       <= 0;

 if ( rx_byte_start == 1 ) begin
   byte_cnt   <= 4'd0;
   header_jk  <= 1;
 end else if ( rx_byte_rdy == 1 ) begin 
   byte_cnt   <= byte_cnt + 1; 
 end 

 // Shift bytes into a 32bit SR
 if ( rx_byte_rdy == 1 ) begin 
   dword_sr[31:0] <= { dword_sr[23:0], rx_byte_d[7:0] };
 end

 // After we have 4 bytes, look for Slot=00,SubSlot=0,Command=0or1 (WR or RD)
 if ( rx_byte_rdy_p2 == 1 && byte_cnt[1:0] == 2'd3 ) begin
   dword_rdy <= 1;
   header_jk <= 0;
   if ( header_jk == 1 && wr_jk == 0 && rd_jk == 0 ) begin 
     // Note: This doesn't handle slot broadcast - only direct for LB Access
     if ( dword_sr[31:16] == 16'hF000 ) begin
       lb_user_jk <= 0;
       lb_prom_jk <= 0;
       if          ( dword_sr[15:12] == 4'h0 ) begin
         lb_user_jk <= 1;
       end else if ( dword_sr[15:12] == 4'hE ) begin
         lb_prom_jk <= 1;
       end

       if ( dword_sr[15:12] == 4'h0 || dword_sr[15:12] == 4'hE ) begin
         if ( dword_sr[11:8] == 4'h0 || dword_sr[11:8] == 4'h2 ) begin
           wr_jk       <= 1;
           dword_stage <= 3'b001;
         end 
         if ( dword_sr[11:8] == 4'h1 || dword_sr[11:8] == 4'h3 ) begin
           rd_jk         <= 1;
           dword_stage   <= 3'b001;
         end 
         // Repeat_JK Asserts to burst to/from single address
         if ( dword_sr[11:8] == 4'h2 || dword_sr[11:8] == 4'h3 ) begin
           rpt_jk <= 1;
         end else begin
           rpt_jk <= 0;
         end
       end // if ( dword_sr[15:12] == 4'h0 || dword_sr[15:12] == 4'hE ) begin
     end // if ( dword_sr[31:16] == 16'hF000 ) begin
   end else begin
     // Payload
     if ( wr_jk == 1 || rd_jk == 1 ) begin
       // This shifts 001 to 010, 100 then halts at 100
       // It records burst write stages of header,addr,data,data
       dword_stage <= { ( dword_stage[2] | dword_stage[1] ), 
                          dword_stage[0],
                          1'b0 };
     end
   end // if ( wr_jk == 0 && rd_jk == 0 ) begin 
 end // if ( rx_byte_rdy_p2 == 1 && byte_cnt[1:0] == 2'd3 ) begin

 //
 if ( rx_byte_stop_p3 == 1 ) begin
   wr_jk   <= 0;
 end
 if ( rd_done == 1 && rd_done_p1 == 0 ) begin
   rd_jk   <= 0;
 end
 if ( reset == 1 ) begin
   wr_jk      <= 0;
   rd_jk      <= 0;
   rpt_jk     <= 0;
   lb_user_jk <= 0;
   lb_prom_jk <= 0;
   header_jk  <= 0;
 end
 
end // proc_lb1


//-----------------------------------------------------------------------------
// Convert serial stream into parallel local bus
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_lb2
  lb_wr      <= 0;
  prom_wr    <= 0;
  lb_rd_loc  <= 0;
//lb_wr_d    <= 32'd0;
//lb_addr    <= 32'd0;
//prom_wr_d  <= 32'd0;
//prom_addr  <= 32'd0;
  rd_done_p1 <= rd_done;
  ro_header_rdy <= 0;

  if ( dword_rdy == 1 && wr_jk == 1 ) begin
    if ( dword_stage[1] == 1 ) begin
      addr_cnt <= dword_sr[31:0];// 1st DWORD of WR is LB Address
    end
    if ( dword_stage[2] == 1 ) begin
      lb_wr     <= lb_user_jk;
      prom_wr   <= lb_prom_jk;
      lb_addr   <= addr_cnt[31:0];// Load the stored address
      prom_addr <= addr_cnt[31:0];// Load the stored address
      lb_wr_d   <= dword_sr[31:0];// 2nd+ DWORDs or WR is Burst Data
      prom_wr_d <= dword_sr[31:0];// 2nd+ DWORDs or WR is Burst Data
      if ( rpt_jk == 0 ) begin
        addr_cnt  <= addr_cnt + 4;  // Increment address for next cycle
      end
    end
  end // if ( dword_rdy == 1 && wr_jk == 1 ) begin

  if ( dword_rdy == 1 && rd_jk == 1 ) begin
    if ( dword_stage[1] == 1 ) begin
      addr_cnt      <= dword_sr[31:0];// 1st DWORD of RD is LB Address
    end
    if ( dword_stage[2] == 1 ) begin
      addr_len      <= dword_sr[31:0];// 2nd DWORD of RD is Num DWORDs to Read
      ro_header_rdy <= 1;// Send Ro Header as we know length of packet to send
    end
  end // if ( dword_rdy == 1 && rd_jk == 1 ) begin

  if ( rd_jk == 0 ) begin
    addr_len       <= 32'd0;
    rd_done        <= 0;
    reading        <= 0;
  end else begin
    if ( addr_len != 32'd0 ) begin
      if ( ro_header_rdy == 0 && rd_busy == 0 && lb_rd_loc == 0 ) begin
        lb_rd_loc <= 1;
        lb_addr   <= addr_cnt[31:0];// Load stored address
        prom_addr <= addr_cnt[31:0];// Load stored address
        addr_len  <= addr_len - 1;  // Decrement DWORD length counter   
        reading   <= 1;
        if ( rpt_jk == 0 ) begin
          addr_cnt  <= addr_cnt + 4;  // Increment address for next cycle
        end
      end
    end else begin
      if ( reading == 1 ) begin
        rd_done <= 1;
      end
    end
  end // if ( rd_jk == 0 ) begin

//addr_cnt[31:12] <= 20'd0;// Harvest bits for timing
//addr_len[31:12] <= 20'd0;// Harvest bits for timing
// Max 63 DWORD Read as MesaBus payload max size is 255 bytes
  addr_len[31:6]  <= 26'd0;// Harvest bits for timing. 

end // proc_lb2
  assign lb_rd   = lb_rd_loc & lb_user_jk;
  assign prom_rd = lb_rd_loc & lb_prom_jk;


//-----------------------------------------------------------------------------
// Shift the readback data out 1 byte at a time 
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_lb3
  tx_done     <= 0;
  rd_byte_rdy <= 0;
  rd_byte_d   <= 8'H00;
//rd_timeout_cnt_p1 <= rd_timeout_cnt[7];

  // Timeout after 128 clocks
//if ( reset == 1 ) begin
//  rd_timeout_cnt <= 8'H00;// Stopped
//end else begin
//  if ( lb_user_jk == 1 || lb_prom_jk == 1 ) begin
//    if ( lb_rd_loc == 1 ) begin
//      rd_timeout_cnt    <= 8'H80;// Enable the count For 0-127
//    end else if ( lb_rd_rdy == 1 || prom_rd_rdy == 1 ) begin
//      rd_timeout_cnt[7] <= 0;// Stop the Count
//      rd_timeout_cnt_p1 <= 0;
//    end else if ( rd_timeout_cnt[7] == 1 ) begin
//      rd_timeout_cnt    <= rd_timeout_cnt + 1;
//    end else if ( rd_timeout_cnt_p1 == 1 ) begin
//      rd_timeout_cnt <= 8'H00;// Stopped
//      lb_rd_sr       <= 32'hDEADBEEF;
//      lb_rd_fsm      <= 4'b1111;
//      rd_busy        <= 1;
//    end
//  end
//end

  // rd_busy asserts when a 32bit Ro shift out is starting to pause bursts
  if ( ro_header_rdy == 1 && ( lb_prom_jk == 1 || lb_user_jk == 1 ) ) begin
    lb_rd_sr[31:8] <= 24'hF0FE00;
    lb_rd_sr[7:0]  <= { addr_len[5:0], 2'b00 };// Number of Return Bytes
    lb_rd_fsm      <= 4'b1111;
    rd_busy        <= 1;
  end
  if ( lb_rd_rdy == 1 && lb_user_jk == 1 ) begin
    lb_rd_sr  <= lb_rd_d[31:0];
    lb_rd_fsm <= 4'b1111;
    rd_busy   <= 1;
  end
  if ( prom_rd_rdy == 1 && lb_prom_jk == 1 ) begin
    lb_rd_sr  <= prom_rd_d[31:0];
    lb_rd_fsm <= 4'b1111;
    rd_busy   <= 1;
  end
  if ( oob_rd_rdy == 1 && oob_en == 1 ) begin
    lb_rd_sr  <= oob_rd_d[31:0];// oob stands for Out of Band signaling
    lb_rd_fsm <= 4'b1111;
    rd_busy   <= 1;
  end

  if ( lb_rd_loc == 1 ) begin
    rd_busy <= 1;
  end

  if ( lb_rd_fsm != 4'b0000 && rd_byte_rdy == 0 && tx_busy == 0 ) begin
    rd_byte_d   <= lb_rd_sr[31:24];
    rd_byte_rdy <= 1;
    lb_rd_sr    <= { lb_rd_sr[23:0], 8'h00 };
    lb_rd_fsm   <= { lb_rd_fsm[2:0], 1'b0  };
    if ( lb_rd_fsm == 4'b1000 ) begin
      rd_busy <= 0;
      if ( rd_jk == 0 && oob_en == 0 ) begin
        tx_done <= 1;
      end
    end
  end

  if ( reset == 1 ) begin 
    lb_rd_fsm  <= 4'b0000;
    rd_busy    <= 0;
  end 
end // proc_lb3


endmodule // mesa2lb
