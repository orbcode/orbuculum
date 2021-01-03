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
		output reg              PkAvail,    // Toggling indicator packet ready
		output reg [127:0]      Packet      // The last packet
		);		  
   
   // Internals =======================================================================

   // Frame maintenance
   reg [31:0] 	construct;                // Track of data being constructed
   reg [111:0]  cPacket;                  // Entire TPIU frame being constructed
   reg [2:0]    remainingClocks;          // Number of elements to remaining for word
   reg [7:0]    elemCount;                // Element we're currently waiting to store
   reg [25:0]   syncInd;                  // Stretcher since last sync (1.4s @ 48MHz)
   
   // ================== Input data against traceclock from target system =============

   // Number of clock ticks for a full 16 bits to have been received
   wire [2:0]   remainingClocksForFullWord = (width==3)?3'h1:(width==2)?3'h3:3'h7;

   // Flag indicating we've got a new full sync (for 4, 2 and 1 bit cases)
   wire        syncPacket = (construct[31 -: 32]==32'h7fff_ffff);
   
   wire [31:0] nextConstruct =
                     (width==3)?{traceDinb[3:0],traceDina[3:0],construct[31:8]}:
                     (width==2)?{traceDinb[1:0],traceDina[1:0],construct[31:4]}:
                                {traceDinb[0],traceDina[0],construct[31:2]};

   // Pointer to where the output data word is in the construct frame
   wire [15:0] packetwd = construct[31 -:16];

   always @(posedge traceClkin, posedge rst)
     begin
        // Default status bits
	if (rst)
	  begin
             syncInd         <= 0;               // Not initially synced
	     construct       <= 0;               // Nothing built
             elemCount       <= (1<<0);          // No elements for this packet
             PkAvail         <= 0;               // No packet pending right now
             remainingClocks <= remainingClocksForFullWord;
	  end
	else
	  begin
             /* Roll constructed value along to accomodate new data */
             construct <= nextConstruct;

             // Code for establishing and maintaining sync ============================
             if (syncPacket)
               begin
                  $display("Got sync");
                  remainingClocks <= remainingClocksForFullWord;
                  elemCount <= (1<<0);           // erase any partial packet
                  syncInd <= ~0;
               end
             else
               begin
	          // Deal with complete element reception =============================
                  // Code for determining if we've got a packet element
                  if (syncInd) syncInd<=syncInd-1;
                  
                  if (remainingClocks) remainingClocks<=remainingClocks-1;
                  else
                    begin
                       // Got a complete word, process it
                       remainingClocks <= remainingClocksForFullWord;

                       if (packetwd != 16'h7fff)
                         begin
                            $display("Got word: %04X",packetwd);
                            elemCount<={elemCount[6:0],1'b0};
                            
                            case (elemCount)
                              (1<<0): cPacket[ 15: 0]<=packetwd;
                              (1<<1): cPacket[ 31:16]<=packetwd;
                              (1<<2): cPacket[ 47:32]<=packetwd;
                              (1<<3): cPacket[ 63:48]<=packetwd;
                              (1<<4): cPacket[ 79:64]<=packetwd;
                              (1<<5): cPacket[ 95:80]<=packetwd;
                              (1<<6): cPacket[111:96]<=packetwd;
                              (1<<7):
                                begin
                                   $display("Complete, sending (%d)",PkAvail);
                                   Packet <= {packetwd,cPacket};
                                   elemCount <= (1<<0);
                                   if (syncInd!=0) PkAvail <= !PkAvail;
                                end
                            endcase // case (elemCount)
                         end // if (packetwd!=16'h7fff)
                    end // else: !if(remainingClocks)
               end // else: !if (syncPacket)
          end // else: !if(rst)
     end // always @ (posedge traceClkin, posedge rst)
endmodule // traceIF
