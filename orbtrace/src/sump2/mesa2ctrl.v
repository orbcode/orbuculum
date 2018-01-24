/* ****************************************************************************
-- (C) Copyright 2015 Kevin M. Hubbard @ Black Mesa Labs
-- Source file: mesa2ctrl.v                
-- Date:        October 4, 2015   
-- Author:      khubbard
-- Language:    Verilog-2001 
-- Description: The Mesa Bus to Control Bus translator. Decodes all subslot  
--              command nibbles for this slot. Write Only Operations.
-- License:     This project is licensed with the CERN Open Hardware Licence
--              v1.2.  You may redistribute and modify this project under the
--              terms of the CERN OHL v.1.2. (http://ohwr.org/cernohl).
--              This project is distributed WITHOUT ANY EXPRESS OR IMPLIED
--              WARRANTY, INCLUDING OF MERCHANTABILITY, SATISFACTORY QUALITY
--              AND FITNESS FOR A PARTICULAR PURPOSE. Please see the CERN OHL
--              v.1.2 for applicable Conditions.
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
-- Revision History:
-- Ver#  When      Who      What
-- ----  --------  -------- ---------------------------------------------------
-- 0.1   10.04.15  khubbard Creation
-- ***************************************************************************/
`default_nettype none // Strictly enforce all nets to be declared
                                                                                
module mesa2ctrl
(
  input  wire         clk,
  input  wire         reset,
  input  wire         rx_byte_start,
  input  wire         rx_byte_stop,
  input  wire         rx_byte_rdy,
  input  wire [7:0]   rx_byte_d,
  output reg  [8:0]   subslot_ctrl
); // module mesa2ctrl

  reg [3:0]     byte_cnt;
  reg [31:0]    dword_sr;
  reg           rx_byte_rdy_p1;
  reg           rx_byte_rdy_p2;
  reg           rx_byte_rdy_p3;
  reg           rx_byte_stop_p1;
  reg           rx_byte_stop_p2;
  reg           rx_byte_stop_p3;


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

 if ( rx_byte_start == 1 ) begin
   byte_cnt   <= 4'd0;
 end else if ( rx_byte_rdy == 1 ) begin 
   if ( byte_cnt != 4'd4 ) begin
     byte_cnt   <= byte_cnt + 1; 
   end
 end 

 // Shift bytes into a 32bit SR
 if ( rx_byte_rdy == 1 ) begin 
   dword_sr[31:0] <= { dword_sr[23:0], rx_byte_d[7:0] };
 end

 subslot_ctrl[8] <= 0;// Strobe 
 // Accept single DWORD packets for Slot-00 (this) or Slot-FF (all)
 if ( rx_byte_rdy_p2 == 1 && byte_cnt[3:0] == 4'd3 ) begin
   if ( dword_sr[31:16] == 16'hF000 || 
        dword_sr[31:16] == 16'hF0FF    ) begin
     // Payload must be 0x00 length
     if ( dword_sr[7:0] == 8'h00 ) begin
       subslot_ctrl <= { 1'b1, dword_sr[15:8] };// D(8) is Strobe
     end
   end
 end

 if ( reset == 1 ) begin
   byte_cnt   <= 4'd0;
 end
 
end // proc_lb1


endmodule // mesa2ctrl
