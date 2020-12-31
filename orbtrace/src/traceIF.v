`default_nettype none

// traceIF
// =======
// Concatenates defined bus width input data from TRACEDATA using TRACECLK to bring it
// into 16 bit chunks which are stored in memory if they're not 'pass' cases. Also
// deals with top level sync. Above this level nothing needs to know how wide the
// tracebus actually is.
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
		output reg              PkAvail,    // Flag indicating packet ready
		output reg [127:0]      Packet,     // The last packet

		output reg              sync        // Toggling Indicator for sync
		);		  
   
   // Internals =======================================================================

   // Frame maintenance
   reg 	ofs;                              // Is the frame phase-shifted?
   reg [35:0] 	construct;                // Track of data being constructed
   reg [111:0]  cPacket;                  // Entire TPIU frame being constructed
   reg [2:0]    remainingClocks;          // Number of elements to remaining for word
   reg [2:0]    elemCount;                // Element we're currently waiting to store

   // ================== Input data against traceclock from target system =============

   // Number of clock ticks for a full 16 bits to have been received
   wire [2:0]   remainingClocksForFullWord = (width==3)?3'h1:(width==2)?3'h3:3'h7;

   // Flag indicating we've got a new full sync (for 4, 2 and 1 bit cases)
   wire        evenSyncPacket = (construct[35 -: 32]==32'h7fff_ffff);
   
   wire        oddSyncPacket =
                     (width==3)?(construct[31 -: 32]==32'h7fff_ffff):
                     (width==2)?(construct[33 -: 32]==32'h7fff_ffff):
                                (construct[34 -: 32]==32'h7fff_ffff);

   wire [35:0] nextConstruct =
                     (width==3)?{traceDinb[3:0],traceDina[3:0],construct[35:8]}:
                     (width==2)?{traceDinb[1:0],traceDina[1:0],construct[35:4]}:
                                {traceDinb[0],traceDina[0],construct[35:2]};

   // Pointer to where the output data word is in the construct frame
   wire [15:0] packetwd = (ofs==1'b0)?construct[35 -:16]:
                                      (width==3)?construct[31 -:16]:
                                      (width==2)?construct[33 -:16]:
                                                 construct[34 -:16];
   always @(posedge traceClkin, posedge rst)
     begin
        // Default status bits
	if (rst)
	  begin
             sync            <= 0;               // Set start state for sync event
             ofs             <= 0;               // No phase shift
	     construct       <= 0;               // Nothing built
             elemCount       <= 0;               // No elements for this packet
             PkAvail         <= 0;               // No packet pending right now
             remainingClocks <= remainingClocksForFullWord;
	  end
	else
	  begin
             /* Roll constructed value along to accomodate new data */
             construct<=nextConstruct;

             // Code for establishing and maintaining sync ============================
	     // Check if signal starts on even or odd side of the DDR
             if ((evenSyncPacket) || (oddSyncPacket))
               begin
                  ofs<=oddSyncPacket;
                  $display("Got sync, is odd=%d",ofs);
                  remainingClocks<=remainingClocksForFullWord;
                  elemCount <= 0;                 // erase any partial packet
                  sync<=!sync;
               end
             else
               begin
	          // Deal with complete element reception =============================
                  // Code for determining if we've got a packet element
                  if (remainingClocks) remainingClocks<=remainingClocks-1;
                  else
                    begin
                       // Got a complete word, process it
                       remainingClocks<=remainingClocksForFullWord;

                       if (packetwd!=16'h7fff)
                         begin
                            $display("Got word: %04X (%d)",packetwd,ofs);

                            case (elemCount)
                              3'h0: cPacket[ 15: 0]<=packetwd;
                              3'h1: cPacket[ 31:16]<=packetwd;
                              3'h2: cPacket[ 47:32]<=packetwd;
                              3'h3: cPacket[ 63:48]<=packetwd;
                              3'h4: cPacket[ 79:64]<=packetwd;
                              3'h5: cPacket[ 95:80]<=packetwd;
                              3'h6: cPacket[111:96]<=packetwd;
                              3'h7:
                                begin
                                   $display("Complete, sending (%d)",PkAvail);
                                   Packet<={packetwd,cPacket};
                                   PkAvail<=!PkAvail;
                                end
                            endcase // case (elemCount)
                            
                            elemCount<=elemCount+1;
                         end // if (packetwd!=16'h7fff)
                    end // else: !if(remainingClocks)
               end // else: !if((evenSyncPacket) || (oddSyncPacket))
          end // else: !if(rst)
     end // always @ (posedge traceClkin, posedge rst)
endmodule // traceIF
