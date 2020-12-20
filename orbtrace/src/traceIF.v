`default_nettype none

// traceIF
// =======
// Concatenates defined bus width input data from TRACEDATA using TRACECLK to bring it into
// 16 bit chunks which are stored in memory if they're not 'pass' cases. Also deals with top
// level sync. Above this level nothing needs to know how wide the tracebus actually is.
// 
// Working from CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0

module traceIF # (parameter MAXBUSWIDTH = 4) (
		input                   rst,            // Reset synchronised to clock

	// Downwards interface to the trace pins
		input [MAXBUSWIDTH-1:0] traceDina,      // Tracedata rising edge... 1-n bits max, can be less
		input [MAXBUSWIDTH-1:0] traceDinb,      // Tracedata falling edge... 1-n bits max, can be less
		input                   traceClkin,     // Tracedata clock... async to clk
		input [1:0]             width,          // How wide the bus under consideration is 0..3 (1, 1, 2 & 4 bits)

	// Upwards interface to packet processor
		output reg              WdAvail,        // Flag indicating word is available
		output reg [15:0]       PacketWd,       // The last word that has been received
		output reg              PacketReset,    // Flag indicating to start packet again

		output reg              sync            // Indicator that we are in sync
		);		  
   
   // Internals =============================================================================

   // Frame maintenance
   reg 	ofs;                              // Is the frame phase-shifted? If so then correct for it
   reg [35:0] 	construct;                // Track of data being constructed
   reg [2:0]    remainingClocks;          // Number of elements to receive for this word to complete

   // ================== Input data against traceclock from target system ====================

   // Number of clock ticks for a for 16 bits to have been received
   wire [2:0]   remainingClocksForFullWord = (width==3)?1:(width==2)?3:7;

   // Pointer to where the output data word is in the construct frame
   wire [15:0]  dataOut = ofs?construct[31 -:16]:
                (width==3)?construct[35 -:16]:
                (width==2)?construct[33 -:16]:
                construct[34 -: 16];

   // Flag indicating we've got a new full sync (for 4, 2 and 1 bit cases)
   wire         evenSyncPacket = (construct[35 -: 32]==32'h7fff_ffff);
   
   wire         oddSyncPacket =  (width==3)?(construct[31 -: 32]==32'h7fff_ffff):
                                 (width==2)?(construct[33 -: 32]==32'h7fff_ffff):
                                 (construct[34 -: 32]==32'h7fff_ffff);

   wire [35:0]  nextConstruct = (width==3)?{traceDinb[3:0],traceDina[3:0],construct[35:8]}:
                                (width==2)?{traceDinb[1:0],traceDina[1:0],construct[35:4]}:
                                           {traceDinb[0],traceDina[0],construct[35:2]};

   always @(posedge traceClkin, posedge rst)
     begin
        // Default status bits
	WdAvail     <= 0;                        // We have no packet available
	PacketReset <= 0;                        // Stop erasing any partial packets
        sync        <= 0;                        // Default is sync false, unless we see one

	if (rst)
	  begin
             ofs       <= 0;                     // No phase shift
	     construct <= 0;                     // Nothing built
	  end
	else
	  begin
             construct<=nextConstruct;

             // Code for establishing and maintaining sync ============================
	     // Check if signal starts on even or odd side of the DDR
             if ((evenSyncPacket) || (oddSyncPacket))
               begin
                  if (oddSyncPacket)
                    ofs<=1;
                  else
                    ofs<=0;
                  
                  sync<=1;
                  remainingClocks<=remainingClocksForFullWord;
		  PacketReset <= 1'b1;          // tell upstairs to erase any partial packet it got
               end
             else
               begin
	          // Deal with complete element reception ==================================
                  // Code for determining if we've got a packet element
                  if (!remainingClocks)
	            begin
                       remainingClocks<=remainingClocksForFullWord; // Reset the readbits counter
                       if (dataOut!=16'h7fff)
                         begin
                            // We got 16 bits of whatever it is, so send it
		            WdAvail<=1'b1;
                            PacketWd<=dataOut;
                         end
                    end // if (!remainingClocks)
                  else
                    remainingClocks<=remainingClocks-1;
               end // else: !if((evenSyncPacket) || (oddSyncPacket))
	  end // else: !if(rst)
     end // always @ (posedge traceClk, posedge rst)
endmodule // traceIF
