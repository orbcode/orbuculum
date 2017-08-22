// packCollect
// ===========
//
// Module that collects sets of bits from the trace interface (which is asynchronous with respect to this) and constructs
// it back into complete packets which can then be processed by layers above.
//
module packCollect (
		   input 	 clk, // System Clock
		   input 	 nRst, // System reset
		   input [1:0] 	 width, // Current trace buffer width
		   output  	 sync, // Indicator of if we are in sync

		   // Downwards interface to trace elements
		   output reg 	 TraceNext, // Strobe for requesting next trace data element
		   input 	 TraceAvail, // Flag indicating trace data is available
		   input [3:0] 	 TraceIn, // ...the valid Trace Data

		   // Upwards interface to packet processor
		   output 	 PacketAvail, // Flag indicating packet is available
		   input	 PacketNext, // Strobe for requesting next packet
		   input	 PacketNextWd, // Strobe for next 16 bit word in packet	 

		   output reg [15:0] PacketOut, // The next packet word
		   output reg [7:0]  PacketFinal // The last word in the packet (used for reconstruction)
		   );

   // Parameters =============================================================================

   parameter PACKET_LENGTH=16;    // How many bytes are in a TPIU packet
   parameter SYNC_COUNTOUT=20000; // Count before sync is considered lost
   
   parameter STATE_CHECK_AVAIL=0; // STATE: Request next bytes 
   parameter STATE_GET_DATA=1;    // STATE: Get them
   
   // Internals ==============================================================================

   reg [15:0] 		 packetmem[0:255];  // Storage for packet under construction (16 bits wide, 2 bytes at a time)
   reg [4:0] 	         packetmemNumWp;    // Write pointer for packet number 
   reg [4:0] 	         packetmemOffsWp;   // Write pointer for offset in the packet

   reg [4:0] 		 packetmemNumRp;    // Read pointer for packet number
   reg [4:0] 		 packetmemOffsRp;   // Read pointer for offset in the packet
   reg [4:0] 		 packetReading;     // Packet I am currently reading in the buffer
      
   reg [31:0] 		 dBuff;             // Last 32 bits read from stream - used for establishing sync
   reg [7:0] 		 readBits;          // Number of bits read from the stream

   reg [18:0] 		 lostSync;          // Counter for sync loss

   reg                   rxState;           // Current reception state from trace interface
   
   // Code ===================================================================================

   assign PacketAvail=(packetmemNumWp!=packetmemNumRp); // Flag showing that there is a packet available to process
   assign sync=(lostSync!=SYNC_COUNTOUT);               // Flag indicating that we are currently in sync
   
   always @( negedge clk ) // ---------------------------------------------------------------
     begin
	if (nRst)
	  begin
	     // ================================================
	     if (rxState==STATE_CHECK_AVAIL)
	       begin
		  if (TraceAvail)
		    begin
		       TraceNext <= 1'b1;
		       rxState=STATE_GET_DATA;
		    end
		  else
		    begin
		       TraceNext <= 1'b0;
		    end
	       end // if (rxState==STATE_CHECK_AVAIL)
	     else // Deal with case of (rxState==STATE_GET_DATA)
	       begin
		  TraceNext <= 1'b0;
		  dBuff<=dBuff>>(width+1)|(TraceIn<<(31-width));
		  readBits<=readBits+(width+1);
		  rxState<=STATE_CHECK_AVAIL;
	       end
	     
	     // ==================================================
	     if (dBuff==32'h7fffffff) //----- Deal with sync pulse
	       begin
		  // This only arrives inter-frame, so just reset the pointers
		  lostSync <= 0;
		  readBits <= 0;
		  dBuff <= 0;
		  packetmemOffsWp <= 0;
	       end
	     else
	       begin
		  if (lostSync<SYNC_COUNTOUT)
		    begin
		       lostSync<=lostSync+1;
		    end
	       end

	     // =====================================================
	     if (readBits==16) //----- Deal with entire pair of bytes received
	       begin
		  if (sync==1'b1)
		    begin
		       packetmem[{packetmemNumWp,packetmemOffsWp[3:0] }]<=dBuff[31:16];
		       packetmemOffsWp<=packetmemOffsWp+1;
		    end
		  readBits<=0;
	       end

	     if (packetmemOffsWp==(PACKET_LENGTH/2)) //----- Deal with entire frame received
	       begin
		  packetmemNumWp<=packetmemNumWp+1;
		  packetmemOffsWp<=0;
	       end

	     // =================================================================
	     if (PacketNext==1'b1) // ----- Deal with request to read next packet
	       begin
		  if (packetmemNumWp!=packetmemNumRp)
		    begin
		       PacketFinal <= packetmem[{packetmemNumRp,3'b111}][15:8];
		       packetReading <=packetmemNumRp;
		       packetmemOffsRp<=0;
		       packetmemNumRp<=packetmemNumRp+1;
		    end
	       end

	     // ============================================================================
	     if (PacketNextWd==1'b1) //----- Deal with request to read next word from packet
	       begin
		  if (packetmemOffsRp<(PACKET_LENGTH/2))
		    begin
		       PacketOut<=packetmem[{packetReading,packetmemOffsRp[3:0]}];
		       packetmemOffsRp<=packetmemOffsRp+1;
		    end
	       end
	  end
	else
	  begin
	   // Handle reset conditions
	     lostSync <= SYNC_COUNTOUT;
	     PacketFinal <= 0;
	     packetmemNumWp <= 0;
	     packetmemOffsWp <= 0;
	     packetmemNumRp <= 0;
	     packetmemOffsRp <= 0;
	     packetReading <= 0;
	     dBuff <= 0;	     
	     readBits <= 0;
	  end
     end // always @ (negedge clk)
endmodule // packCollect

    
