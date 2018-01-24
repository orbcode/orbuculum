/* ****************************************************************************
-- Source file: mesa_id.v                
-- Date:        October 2015     
-- Author:      khubbard
-- Description: Simple state machine the reports chip ID over mesa-bus on 
--              request and muxes in the normal Ro byte path when idle.
-- Language:    Verilog-2001
-- Simulation:  Mentor-Modelsim 
-- Synthesis:   Lattice     
-- License:     This project is licensed with the CERN Open Hardware Licence
--              v1.2.  You may redistribute and modify this project under the
--              terms of the CERN OHL v.1.2. (http://ohwr.org/cernohl).
--              This project is distributed WITHOUT ANY EXPRESS OR IMPLIED
--              WARRANTY, INCLUDING OF MERCHANTABILITY, SATISFACTORY QUALITY
--              AND FITNESS FOR A PARTICULAR PURPOSE. Please see the CERN OHL
--              v.1.2 for applicable Conditions.
--
-- Revision History:
-- Ver#  When      Who      What
-- ----  --------  -------- ---------------------------------------------------
-- 0.1   10.04.15  khubbard Creation
-- ***************************************************************************/
`default_nettype none // Strictly enforce all nets to be declared

module mesa_id
(
  input  wire         clk,
  input  wire         reset,
  input  wire         report_id,
  input  wire [31:0]  id_mfr,
  input  wire [31:0]  id_dev,
  input  wire [31:0]  id_snum,
  input  wire [7:0]   mesa_core_ro_byte_d,
  input  wire         mesa_core_ro_byte_en,
  input  wire         mesa_core_ro_done,
  input  wire         mesa_ro_busy,
  output reg  [7:0]   mesa_ro_byte_d,
  output reg          mesa_ro_byte_en,
  output reg          mesa_ro_done
);// module mesa_id

  reg          report_jk;
  reg          report_id_p1;
  reg  [4:0]   report_cnt;
  reg          mesa_ro_busy_p1;
  wire [31:0]  time_stamp_d;


//-----------------------------------------------------------------------------
// Ro binary bytes are converted to 2 ASCII nibble chars for MesaBus Ro
// This is also a report_id FSM that muxes into the ro byte path on request.
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_mesa_ro_byte
  report_id_p1        <= report_id;
  mesa_ro_byte_d[7:0] <= mesa_core_ro_byte_d[7:0];
  mesa_ro_byte_en     <= mesa_core_ro_byte_en;
  mesa_ro_done        <= mesa_core_ro_done;
  mesa_ro_busy_p1     <= mesa_ro_busy;

  // When report_id asserts ( SubSlot 0xFA command ) shift out
  // a unique 32bit ID of 16 bit Manufacture and 16bit Device
  if ( report_id == 1 ) begin
    report_jk  <= 1;// Start FSM
    report_cnt <= 5'd0;
  end

  // State Machine for sending out device id when requested
  mesa_ro_busy_p1 <= mesa_ro_busy;
  if ( report_jk == 1 ) begin
    mesa_ro_byte_en <= 0;
    mesa_ro_done    <= 0;
  
    if ( report_id_p1==1 || ( mesa_ro_busy_p1==1 && mesa_ro_busy==0 )) begin
      report_cnt       <= report_cnt[4:0] + 1;
      mesa_ro_byte_en  <= 1;
      if          ( report_cnt == 5'd0 ) begin
        mesa_ro_byte_d[7:0] <= 8'hF0;// Header : Preamble
      end else if ( report_cnt == 5'd1 ) begin
        mesa_ro_byte_d[7:0] <= 8'hFE;// Header : Ro Slot is 0xFE
      end else if ( report_cnt == 5'd2 ) begin
        mesa_ro_byte_d[7:0] <= 8'h00;// Header : Ro Subslot is 0x00
      end else if ( report_cnt == 5'd3 ) begin
        mesa_ro_byte_d[7:0] <= 8'h10;// Header : 16 Bytes in Payload

      end else if ( report_cnt == 5'd4 ) begin
        mesa_ro_byte_d[7:0] <= id_mfr[31:24];
      end else if ( report_cnt == 5'd5 ) begin
        mesa_ro_byte_d[7:0] <= id_mfr[23:16];
      end else if ( report_cnt == 5'd6 ) begin
        mesa_ro_byte_d[7:0] <= id_mfr[15:8];
      end else if ( report_cnt == 5'd7 ) begin
        mesa_ro_byte_d[7:0] <= id_mfr[7:0];

      end else if ( report_cnt == 5'd8 ) begin
        mesa_ro_byte_d[7:0] <= id_dev[31:24];
      end else if ( report_cnt == 5'd9 ) begin
        mesa_ro_byte_d[7:0] <= id_dev[23:16];
      end else if ( report_cnt == 5'd10) begin
        mesa_ro_byte_d[7:0] <= id_dev[15:8];
      end else if ( report_cnt == 5'd11) begin
        mesa_ro_byte_d[7:0] <= id_dev[7:0];

      end else if ( report_cnt == 5'd12) begin
        mesa_ro_byte_d[7:0] <= id_snum[31:24];
      end else if ( report_cnt == 5'd13) begin
        mesa_ro_byte_d[7:0] <= id_snum[23:16];
      end else if ( report_cnt == 5'd14) begin
        mesa_ro_byte_d[7:0] <= id_snum[15:8];
      end else if ( report_cnt == 5'd15) begin
        mesa_ro_byte_d[7:0] <= id_snum[7:0];

      end else if ( report_cnt == 5'd16) begin
        mesa_ro_byte_d[7:0] <= time_stamp_d[31:24];
      end else if ( report_cnt == 5'd17) begin
        mesa_ro_byte_d[7:0] <= time_stamp_d[23:16];
      end else if ( report_cnt == 5'd18) begin
        mesa_ro_byte_d[7:0] <= time_stamp_d[15:8];
      end else if ( report_cnt == 5'd19) begin
        mesa_ro_byte_d[7:0] <= time_stamp_d[7:0];
        mesa_ro_done        <= 1;// Send LF
        report_jk           <= 0;// All Done - stop FSM
      end else begin
        mesa_ro_byte_d[7:0] <= 8'h00;// 0x00 = NULL
      end
    end
  end // if ( report_jk == 1 ) begin

end // proc_mesa_ro_byte

//-----------------------------------------------------------------------------
// 32bit UNIX TimeStamp of when the design was synthesized
//-----------------------------------------------------------------------------
time_stamp u_time_stamp
(
  .time_dout                        ( time_stamp_d[31:0] )
);


endmodule // mesa_id.v
