`default_nettype none

// traceIF
// =======
// Concatenates defined bus width input data from TRACEDATA using TRACECLK to bring it
// into 16 bit packets, which are stored in memory in a 8 packet Frame if they're not
// 'pass' cases. Also deals with top level sync. Above this level nothing needs to
// know how wide the tracebus actually is.
// 
// Working from CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0

module traceIF # (parameter MAXBUSWIDTH = 4) (
		input                   rst, // Reset synchronised to clock

	// Downwards interface to the trace pins (1-n bits max, can be less)
		input [MAXBUSWIDTH-1:0] traceDina,  // Tracedata rising edge (LSB)
		input [MAXBUSWIDTH-1:0] traceDinb,  // Tracedata falling edge (MSB)
		input                   traceClkin, // Tracedata clock... async to clk
		input [1:0]             width,      // Incoming Bus width
                                                    //  0..3 (1, 1, 2 & 4 bits)
	// Upwards interface to packet processor
		output reg              FrAvail,    // Toggling indicator frame ready
		output reg [127:0]      Frame,      // The last frame
		);		  
   
   // Internals =======================================================================

   // Frame maintenance
   reg [31:0] 	construct;                // Track of data being constructed
   reg [111:0]  cFrame;                   // Entire TPIU frame being constructed
   reg [2:0]    remainingClocks;          // Number of elements remaining for word
   reg [2:0]    elemCount;                // Element we're currently waiting to store
   reg [26:0]   syncInd;                  // Stretcher since last sync (~0.7s @ 96MHz)
   
   // ================== Input data against traceclock from target system =============

   // Flag indicating we've got a new full sync
   wire        syncPacket = (construct[31 -: 32]==32'h7fff_ffff);
   
   // Pointer to where the output data word is in the construct frame
   wire [15:0] packet = construct[31 -:16];

   // Lookup for packet width to clocks
   wire [2:0] packetClocks = (width==2'b11)?3'h1:(width==2'h2)?3'h3:3'h7;

   always @(posedge traceClkin, posedge rst)
     begin
        // Default status bits
	if (rst)
	  begin
             syncInd         <= 0;               // Not initially synced
	     construct       <= 0;               // Nothing built
             elemCount       <= 0;               // No elements for this packet
             FrAvail         <= 0;               // No frame pending right now
             remainingClocks <= 0;               // Clocks will be set after reset
	  end
	else
	  begin
             /* Roll constructed value along to accomodate new data for 4, 2 & 1 bits */
             case (width)
               2'b11: construct <= {traceDinb[3:0],traceDina[3:0],construct[31:8]};
               2'b10: construct <= {traceDinb[1:0],traceDina[1:0],construct[31:4]};
               default: construct <= {traceDinb[0],traceDina[0],construct[31:2]};
             endcase

             // Code for establishing and maintaining sync ============================
             if (syncPacket)
               begin
                  $display("Got sync");
                  remainingClocks <= packetClocks; // We need to grab the whole packet
                  elemCount <= 0;                  // erase any partial packet
                  syncInd <= ~0;                   // ..and max the stretch count
               end
             else
               begin
	          // Deal with complete element reception =============================
                  if (syncInd) syncInd<=syncInd-1;
                  
                  if (remainingClocks) remainingClocks<=remainingClocks-1;
                  else
                    begin
                       remainingClocks <= packetClocks;

                       if (packet != 16'h7fff)
                         begin
                            $display("Got word: %04X",packet);
                            elemCount <= elemCount + 1;

                            /* Put this word to correct place in constructed frame */
                            case (elemCount)
                              0: cFrame[111:96]<={packet[7:0],packet[15:8]};
                              1: cFrame[ 95:80]<={packet[7:0],packet[15:8]};
                              2: cFrame[ 79:64]<={packet[7:0],packet[15:8]};
                              3: cFrame[ 63:48]<={packet[7:0],packet[15:8]};
                              4: cFrame[ 47:32]<={packet[7:0],packet[15:8]};
                              5: cFrame[ 31:16]<={packet[7:0],packet[15:8]};
                              6: cFrame[ 15: 0]<={packet[7:0],packet[15:8]};
                              7:
                                begin
                                   $display("Complete, sending (%d)",FrAvail);
                                   Frame <= {cFrame,{packet[7:0],packet[15:8]}};
                                   if (syncInd!=0) FrAvail <= !FrAvail;
                                end
                            endcase // case (elemCount)
                         end // if (packet!=16'h7fff)
                    end // else: !if(remainingClocks)
               end // else: !if (syncPacket)
          end // else: !if(rst)
     end // always @ (posedge traceClkin, posedge rst)
endmodule // traceIF
