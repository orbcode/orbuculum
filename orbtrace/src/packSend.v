// packSend
// ========
//
module packSend(
		input 		 clk,           // System Clock
		input 		 rst,           // Clock synchronised reset

		input 		 sync,          // Indicator of if we are in sync

	        // Downwards interface to packet processor
		input 		 PacketAvail,   // Flag indicating packet is available
		output reg 	 PacketNext,    // Strobe for requesting next packet
		output reg 	 PacketNextWd,  // Strobe for next 16 bit word in packet	 

		input [15:0] 	 PacketIn,      // The next packet word

		// Upwards interface to serial (or other) handler
		output reg 	 DataAvail,     // There is decoded data ready for processing
		output reg [7:0] DataVal,       // Output data value
		input 		 DataNext,      // Request for data
		output reg 	 DataOverf,     // Too much data in buffer
		output reg 	 Probe
 		);

   // Parameters =============================================================================

   // State of the input (from traceIF buffer) process
   parameter
     STATE_BYTE_0=0,                           // Individual bytes being sent to line      
     STATE_CHECK_AVAIL=16,                     // Waiting for a packet to become available
     STATE_END_PACKET_STROBE=17,               // Strobing to get the first word of the packet
     STATE_SEND_FF1=18,                        // Sync frontmatter headers
     STATE_SEND_FF2=19,
     STATE_SEND_FF3=20,
     STATE_SEND_F7=21;
   
   // Internals ==============================================================================

   reg [7:0]    opbuffmem[0:511]; // Output buffer packet memory

   reg [8:0] 	outputRp;         // Element position in output buffer
   reg [8:0] 	outputWp;         // Element position in output buffer
   
   reg [4:0] 	ipstate;          // Current state of the receive process
   
   reg 		oldDataNext;      // Looking for data request edge
   reg 		lostSync;         // Flag indicating that we lost sync sometime since the last packet
 				
   reg [10:0] 	ovfStretch;       // LED stretch for overflow indication
				
   // ====== Perform the data-fetch state machine ==============
   task getInput;
      begin
	 case (ipstate)
	   // ---------------------------------------------------------------------
	   STATE_CHECK_AVAIL: // ----------------------- Waiting for data to arrive
	    begin
	       PacketNextWd<=0;
	       if (PacketAvail)
		 begin
		    // We got a flag for a new packet, so start requesting it
		    ipstate<=ipstate+1;
		    PacketNext<=1; 
		 end
	    end

	   // -----------------------------------------------------------------------------------------
	   STATE_END_PACKET_STROBE: // ---- We requested the next packet, so now request the first word
	     begin
		PacketNext<=1'b0;
		PacketNextWd<=1'b1;

		// If we had an indication of lost sync, then send a sync packet first
		if (lostSync==1'b1)
		  begin
		     lostSync<=0;
		     ipstate<=STATE_SEND_FF1;
		  end else
		    ipstate<=STATE_BYTE_0;
	     end

	   // ----------------------------------------------------------------------------------
	  default: // ------------------------- We are collecting the packet from the packetizer
	    begin
	       PacketNext<=1'b0;
	       // Deal with the sync if requested
	       if ((ipstate==STATE_SEND_FF1) || 
		   (ipstate==STATE_SEND_FF2) || 
		   (ipstate==STATE_SEND_FF3))
		 begin
		    PacketNextWd<=0;
		    opbuffmem[outputWp]<=8'hff;
		    ipstate<=ipstate+1;
		 end
	       else if (ipstate==STATE_SEND_F7)
		 begin
		    PacketNextWd<=0;
		    opbuffmem[outputWp]<=8'h7f;
		    ipstate<=STATE_BYTE_0;
		 end 
	       else
		 begin
		    // Now sending the packet proper - byte number is encoded in the state id
		    if (ipstate[0]==1'b0)
		      begin
			 // This is the lower byte
			 opbuffmem[outputWp]<=PacketIn[7:0];
			 PacketNextWd<=1'b1;
		      end
		    else
		      begin
			 opbuffmem[outputWp]<=PacketIn[15:8];
			 PacketNextWd<=1'b0;
		      end
		    ipstate<=ipstate+1;
		 end // else: !if(ipstate==STATE_SEND_F7)

	       // Whatever happend we added a byte to the output buffer, so
	       // either increment write position or flag the overflow
	       if (outputWp+1==outputRp)
		 ovfStretch<=~0;
	       else
		 outputWp<=outputWp+1;
	    end // case: default
	 endcase // case (ipstate)
     end
   endtask // getInput ---------------------------------------------

   // ======== Send data to upstairs if relevant, and dispatch to collect more from downstairs ============
   always @(posedge clk)
     begin
	if (!rst)
	  begin
	     DataAvail<=(outputWp!=outputRp);
	     DataVal<=opbuffmem[outputRp];
	     DataOverf<=(ovfStretch>0);

	     // If we're illuminated for overflow then countdown
	     if (ovfStretch) ovfStretch<=ovfStretch-1;

	     // Check to see if anyone is asking us for data
	     if ((DataNext) && (!oldDataNext))
	       begin
		  if (outputWp!=outputRp)
		    begin
		       outputRp<=outputRp+1;
		       DataAvail<=0;
		    end
	       end
	     oldDataNext<=DataNext;
	     
	     // Finally, suck and blow data into our local buffer
	     if (sync) 
	       getInput();
	     else 
	       lostSync<=1'b1; // If we lost sync then re-sync on reconnection
	     
	  end // if (!rst)
	else 
	  begin
	     // ============= Setup reset conditions ============================
	     ipstate<=STATE_CHECK_AVAIL;
	     
	     outputRp<=0;	     
	     outputWp<=0;
	     lostSync<=1'b1;	     
	     DataVal<=0;
	     oldDataNext<=0;
	     DataOverf<=0;
	     ovfStretch<=0;
	     PacketNext<=0;
	     PacketNextWd<=0;
	     DataAvail<=0;
	  end // else: !if(!rst)
     end // always @ (posedge clk)
endmodule // packSend
