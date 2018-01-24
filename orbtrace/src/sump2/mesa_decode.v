/* ****************************************************************************
-- (C) Copyright 2015 Kevin M. Hubbard @ Black Mesa Labs
-- Source file: mesa_decode.v                
-- Date:        October 4, 2015   
-- Author:      khubbard
-- Language:    Verilog-2001 
-- Description: The Mesa Bus decoder. Mesa Bus is a byte-pipe much like a 
--              8b10b SERDES pipe. It supports sending packetized streams of
--              serialized bytes to multiple targets on a single serial bus.
--              This implementation uses UART serial, but the protocol is 
--              portable and may be used over SERDES or a synchronous bit,
--              nibble or byte wide streams. 
--              This block decodes packets and routes them to both
--              dowstream slots and the internal sub-slots for this slot.
-- License:     This project is licensed with the CERN Open Hardware Licence
--              v1.2.  You may redistribute and modify this project under the
--              terms of the CERN OHL v.1.2. (http://ohwr.org/cernohl).
--              This project is distributed WITHOUT ANY EXPRESS OR IMPLIED
--              WARRANTY, INCLUDING OF MERCHANTABILITY, SATISFACTORY QUALITY
--              AND FITNESS FOR A PARTICULAR PURPOSE. Please see the CERN OHL
--              v.1.2 for applicable Conditions.
--
--
-- Example:
--    .."FF".."FF"."F0123401[56]" : 
--        0xFF = Bus Idle ( NULLs )
--  B0    0xF0 = New Bus Cycle to begin ( Nibble and bit orientation )
--  B1    0x12 = Slot Number, 0xFF = Broadcast all slots, 0xFE = NULL Dest
--  B2    0x3  = Sub-Slot within the chip (0-0xF)
--        0x4  = Command Nibble for Sub-Slot ( Specific to that SubSlot )
--  B3    0x01 = Number of Payload Bytes (0-255)
--        0x56 = The Payload ( 1 Byte in this example, up to 255 total )
--
--   Slot Numbering 0x00-0xFF:
--     0x00 - 0xFD : Physical Slots 0-253
--     0xF0 - 0xFD : Reserved
--     0xFE        : Nobody slot ( NULL )
--     0xFF        : Broadcast to 0x00-0xEF
--
--   SubSlot Numbering 0x0-0xF:
--     0x0 - 0x7   : User SubSlots ( 0x0 is nominally a 32bit PCI local bus )
--                    0x0 = LB Bus Write
--                    0x1 = LB Bus Read
--                    0x2 = LB Bus Write Repeat ( same address )
--                    0x3 = LB Bus Read Repeat  ( same address )
--     0x8 - 0xB   : Scope Probe CH1-CH4 Mux Selects
--                    0x0 - 0xF : 1 of 16 signal sets to observe. 0x0 nom OFF
--     0xE         : FPGA PROM Access
--                   Commands:
--                    0x0 = LB Bus Write
--                    0x1 = LB Bus Read
--                    0x2 = LB Bus Write Repeat ( same address )
--                    0x3 = LB Bus Read Repeat  ( same address )
--     0xF         :  Mesa Control : Power and Pin Control
--                    Commands:
--
--                    0x0 = Ro pin is MesaBus Ro Readback     ( Default )
--                    0x1 = Ro pin is MesaBus Wi loopback
--                    0x2 = Ro pin is MesaBus Interrupt
--                    0x3 = Ro pin is user defined output function
--                    0x4 = Ri pin is MesaBus Input           ( Default )
--                    0x5 = Ri pin is disabled
--                    0x6 = Wo pin is MesaBus Output          ( Default )
--                    0x7 = Wo pin is disabled
--
--                    0x8 = RESERVED
--                    0x9 = RESERVED
--                    0xA = Report Device ID
--                    0xB = Reset Core 
--                    0xC = Clear Baud Lock
--                    0xD = Power Off     - Clock Trees Disabled
--                    0xE = Boot to Slot-1 Bootloader (Default)
--                    0xF = Boot to Slot-2 User Image
--
-- Revision History:
-- Ver#  When      Who      What
-- ----  --------  -------- ---------------------------------------------------
-- 0.1   10.04.15  khubbard Creation
-- ***************************************************************************/
//`default_nettype none // Strictly enforce all nets to be declared
                                                                                
module mesa_decode
(
  input  wire         clk,
  input  wire         reset,
  input  wire [3:0]   rx_in_d,
  input  wire         rx_in_rdy,
  output wire [7:0]   rx_out_d,
  output wire         rx_out_rdy,
  output wire [7:0]   rx_loc_d,
  output wire         rx_loc_rdy,
  output wire         rx_loc_start,
  output wire         rx_loc_stop
); // module mesa_decode


  reg           lbracket_jk;
  reg           passthru_jk;
  reg           broadcst_jk;
  reg           payload_jk;
  reg           process_jk;

  reg           packet_jk;
  reg           packet_jk_p1;
  reg [7:0]     byte_sr;
  reg           byte_pingpong;
  reg           rx_in_rdy_p1;
  reg           byte_rdy;
  reg           byte_rdy_p1;
  reg [3:0]     byte_cnt;
  reg [7:0]     byte_loc;
  reg [7:0]     byte_out;
  reg [7:0]     payload_cnt;
  reg           packet_done;

  reg           loc_start;
  reg [7:0]     loc_slot;
  reg [3:0]     loc_subslot;
  reg [3:0]     loc_command;
  reg [7:0]     loc_payload;

  assign rx_out_d     = byte_out[7:0];
  assign rx_out_rdy   = byte_rdy_p1;
  assign rx_loc_d     = byte_loc[7:0];
  assign rx_loc_rdy   = byte_rdy_p1;
  assign rx_loc_start = loc_start;
  assign rx_loc_stop  = packet_done;


//-----------------------------------------------------------------------------
// Shift a nibble into a byte shift register.  The 1st 0xF0 received when NOT
// currently in a packet determines the phasing for byte within nibble stream.
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_nib_in 
 rx_in_rdy_p1  <= rx_in_rdy;
 if ( rx_in_rdy == 1 ) begin
   byte_sr[7:4]  <= byte_sr[3:0];
   byte_sr[3:0]  <= rx_in_d[3:0];
   byte_pingpong <= ~ byte_pingpong;
 end
 if ( packet_jk == 0 ) begin
   byte_pingpong <= 0;
 end
 if ( reset == 1 ) begin
   byte_sr[7:0]  <= 8'hFF;
 end
end


//-----------------------------------------------------------------------------
// Decode and direct : Packet begins on a 0xF0 byte - which provides for both 
// bit and nibble alignment as bus sits idle at 0xFF.
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_byte_in 
 byte_rdy     <= 0;
 packet_jk_p1 <= packet_jk;

 if ( rx_in_rdy_p1 == 1 ) begin
   if ( byte_sr == 8'hF0 && packet_jk == 0 ) begin
     packet_jk <= 1;
     byte_rdy  <= 1;
   end
   if ( packet_jk == 1 && byte_pingpong == 0 ) begin
     byte_rdy <= 1;
   end
 end

 if ( reset == 1 || packet_done == 1 ) begin
   packet_jk <= 0;
 end
end // proc_byte_in


//-----------------------------------------------------------------------------
//              B0 B1   B2               B3
//  0xFF,0xFF,0xF0,Slot,Sub-Slot+Command,Payload_Len,{ payload },
//
// A Packet is a 4-byte header followed by an optional n-Byte Payload.
// The Byte after the 1st 0xF0 determines the destination slot.
//   slot 0xFF is broadcast and is forwarded untouched as 0xFF.
//   slot 0xFE is null and is forwarded untouched as 0xFE.
//   slot 0x00 is for this slot and is forwarded as 0xFE.
//   slot 0x01-0xFD is decremented by 1 and forwarded
// The 4th Byte determines the length of the payload. If 0, the packet is done.
// otherwise, packet continues for n-Bytes to deliver the payload. A 0xF0 
// following a completed packet indicates start of next packet.
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_header
 byte_rdy_p1 <= byte_rdy;
 packet_done <= 0;
 loc_start   <= 0;
 if ( byte_rdy == 1 ) begin
   // Count the Packet Header Bytes from 1-4 and stop
   if ( byte_cnt != 4'h4 ) begin
     byte_cnt <= byte_cnt + 1;
   end
   if ( byte_cnt == 4'h3 ) begin
     payload_cnt <= byte_sr[7:0];
     if ( byte_sr == 8'd0 ) begin
       packet_done <= 1;// No Payload - done immediately
     end
   end
   if ( byte_cnt == 4'h4 ) begin
     payload_cnt <= payload_cnt - 1;
   end
   if ( byte_cnt == 4'h4 && payload_cnt == 8'd1 ) begin
     packet_done <= 1;
   end

   if ( byte_cnt == 4'h0 ) begin
    loc_start <= 1;
   end
   if ( byte_cnt == 4'h1 ) begin
    loc_slot <= byte_sr[7:0];
   end
   if ( byte_cnt == 4'h2 ) begin
    loc_subslot <= byte_sr[7:4];
    loc_command <= byte_sr[3:0];
   end
   if ( byte_cnt == 4'h3 ) begin
    loc_payload <= byte_sr[7:0];
   end

   // Decode the Slot Number, Decrement when needed. Route to Local and Ext
   if ( byte_cnt == 4'h1 ) begin
     if ( byte_sr == 8'hFF ||
          byte_sr == 8'hFE ) begin
       byte_loc <= byte_sr[7:0];// 0xFF Broadcast and 0xFE null go to everybody
       byte_out <= byte_sr[7:0];
     end else if ( byte_sr != 8'h00 ) begin
       byte_loc <= 8'hFE;
       byte_out <= byte_sr[7:0] - 1;// Decrement the Slot Number as non-zero
     end else begin
       byte_loc <= byte_sr[7:0];// Slot Number is 0x00, so this slot gets it
       byte_out <= 8'hFE;       // Downstream slots get 0xFE instead
     end
   end else begin
     byte_loc <= byte_sr[7:0];
     byte_out <= byte_sr[7:0];
   end
 end // if ( byte_rdy == 1 ) begin

 if ( packet_jk == 0 ) begin
   byte_cnt    <= 4'd0;
   payload_cnt <= 8'd0;
 end
end // proc_header


endmodule // mesa_decode
