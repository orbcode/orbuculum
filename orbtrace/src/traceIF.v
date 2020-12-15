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
   reg [31:0] 	construct;                // Track of data being constructed
   reg [2:0]    remainingClocks;          // Number of elements to receive for this word to complete

   // Sync checking stuff
   reg [8:0] 	gotSync;                  // Counter for when we last saw a sync

   // ================== Input data against traceclock from target system ====================

   // Number of clock ticks for a for 16 bits to have been received
   wire [2:0]   remainingClocksForFullWord = (width==3)?1:(width==2)?3:7;

   // Pointer to where the output data word is in the construct frame
   wire [15:0]  dataOut = construct[31 -:16];

   // Flag indicating we've got a new full sync (for 4, 2 and 1 bit cases)
   wire         goodSyncPacket = (construct[31:0]==32'h7fff_ffff);
   wire         invSyncPacket =  (width==3)?(construct[31:0]==32'hf7ff_ffff):
                                 (width==2)?(construct[31:0]==32'hfdff_ffff):
                                            (construct[31:0]==32'hfeff_ffff);

   wire         halfSyncPacket = (dataOut==16'h7fff);

   wire [3:0]   highData=(ofs==0)?traceDinb[3:0]:traceDina[3:0];
   wire [3:0]   lowData=(ofs==0)?traceDina[3:0]:traceDinb[3:0];

   wire [31:0]  nextConstruct = (width==3)?{highData[3:0],lowData[3:0],construct[31:8]}:
                                (width==2)?{highData[1:0],lowData[1:0],construct[31:4]}:
                                           {highData[0],lowData[0],construct[31:2]};

   always @(posedge traceClkin, posedge rst)
     begin
        // Default status bits
	WdAvail <= 0;                            // We have no packet available
	PacketReset<=0;                          // Stop erasing any partial packets
        sync<=0;                                 // Default is sync false, unless we see one

	if (rst)
	  begin
             ofs<=0;                             // No phase shift
	     construct<=0;                       // Nothing built
	     gotSync<=0;                         // ...no sync
	  end
	else
	  begin
             // Count down the clocks
             remainingClocks<=remainingClocks-1;

	     // Deal with sync flagging ===============================================
             if (!gotSync)
               begin
                  remainingClocks<=remainingClocksForFullWord;
                  if (invSyncPacket) ofs<=!ofs;
                end
             else
	       gotSync<=gotSync-1;              // While ever we've got sync, reduce the timeout count

             // Code for establishing and maintaining sync
	     // Check if signal starts on even or odd side of the DDR
             if (goodSyncPacket)
               begin
		  gotSync<=~0;                  // We synced, let people know
                  sync<=1;
                  remainingClocks<=remainingClocksForFullWord;
		  PacketReset <= 1'b1;          // tell upstairs to erase any partial packet it got
               end

             // Get the new data elements that have arrived ===========================
             construct<=nextConstruct;

	     // Deal with complete element reception ==================================
             // Code for determining if we've got a packet element
             if (!remainingClocks)
	       begin
                  remainingClocks<=remainingClocksForFullWord; // Reset the readbits counter

                  // Either a 7fff packet to be ignored or real data. Either way, we got 16 bits
		  // of it, so send it
	          if (!halfSyncPacket)
                    begin
		       WdAvail<=1'b1;
                       PacketWd<=dataOut;
                    end
	       end // if (!remainingClocks)
	  end // else: !if(rst)
     end // always @ (posedge traceClk, posedge rst)
endmodule // traceIF
