`default_nettype none

// packSend
// ========
//
module packSend (
		input 	      clk, // System Clock
		input 	      rst, // Clock synchronised reset
		
		input 	      sync, // Indicator of if we are in sync
		
	        // Downwards interface to packet processor
		input 	      PacketAvail, // Flag indicating packet is available
		input [127:0] PacketIn, // The next packet word
		output reg    PacketAvailAck, // Indicator that we've heard about the packet 
		
		// Upwards interface to serial (or other) handler
		output [7:0]  DataVal, // Output data value
		input 	      DataNext, // Request for next data element
		output reg    DataReady,
		
		output reg    DataOverf, // Too much data in buffer
 		);

   // Parameters =============================================================================

   // State of the input (from traceIF buffer) process
   parameter
     STATE_BYTE_0=0,                           // Individual bytes being sent to line      
     STATE_CHECK_AVAIL=16,                     // Waiting for a packet to become available
     STATE_SEND_FF1=17,                        // Sync frontmatter headers
     STATE_SEND_FF2=18,
     STATE_SEND_FF3=19,
     STATE_SEND_F7=20;
   
   // Internals ==============================================================================
   parameter BUFFLENLOG2=8;

   wire [7:0] 		      latchedData[0:15];
   
   reg [7:0] 			 opbuffmem[0:(2**BUFFLENLOG2)-1]; // Output buffer packet memory

   reg [BUFFLENLOG2-1:0]	 outputRp;         // Element position in output buffer
   reg [BUFFLENLOG2-1:0]	 outputWp;         // Element position in output buffer
   
   reg [4:0] 			 ipstate;          // Current state of the receive process - seven bits needed for packet length
   
   reg 				 lostSync;         // Flag indicating that we lost sync sometime since the last packet
 				
   reg [10:0] 			 ovfStretch;       // LED stretch for overflow indication
   reg 				 transmitting;     // Flag indicating we've told the UART to do something

				
   // ====== Perform the data-fetch state machine ==============
   task getInput;
      begin
	 case (ipstate)
	   // ---------------------------------------------------------------------
	   STATE_CHECK_AVAIL: // ----------------------- Waiting for data to arrive
	     begin
		if (PacketAvail)
		  begin
		     PacketAvailAck<=1'b1;
		     latchedData[0]<=PacketIn[7:0];
		     latchedData[1]<=PacketIn[15:8];
		     latchedData[2]<=PacketIn[23:16];
		     latchedData[3]<=PacketIn[31:24];
		     latchedData[4]<=PacketIn[39:32];
		     latchedData[5]<=PacketIn[47:40];
		     latchedData[6]<=PacketIn[55:48];
		     latchedData[7]<=PacketIn[63:56];
		     latchedData[8]<=PacketIn[71:64];
		     latchedData[9]<=PacketIn[79:72];
		     latchedData[10]<=PacketIn[87:80];
		     latchedData[11]<=PacketIn[95:88];
		     latchedData[12]<=PacketIn[103:96];
		     latchedData[13]<=PacketIn[111:104];
		     latchedData[14]<=PacketIn[119:112];
		     latchedData[15]<=PacketIn[127:120];

		     // If we had an indication of lost sync, then send a sync packet first
		     if (lostSync==1'b1)
		       begin
			  lostSync<=0;
			  ipstate<=STATE_SEND_FF1;
		       end 
		     else
		       begin
			  // This ensures that a Sync is send once in a while in the normal flow
			  if (outputWp<16)
			    ipstate<=STATE_SEND_FF1;
			  else
			    ipstate<=STATE_BYTE_0;
		       end
		  end // if (PacketAvail)
		else
		  ipstate<=STATE_CHECK_AVAIL;
	     end
	   
	   // ----------------------------------------------------------------------------------
	   default: // ------------------------- We are collecting the packet from the packetizer
	     begin
		PacketAvailAck<=1'b0;
		if (outputWp+1==outputRp) 
		  ovfStretch<=~0;
		else
		  outputWp<=outputWp+1;
		
		// Deal with the sync if requested
		if ((ipstate==STATE_SEND_FF1) || 
		    (ipstate==STATE_SEND_FF2) || 
		    (ipstate==STATE_SEND_FF3))
		  begin
		     opbuffmem[outputWp]<=8'hff;
		     ipstate<=ipstate+1;
		  end
		else
		  begin
		     if (ipstate==STATE_SEND_F7)
		       begin
			  opbuffmem[outputWp]<=8'h7f;
			  ipstate<=STATE_BYTE_0;
		       end 
		     else
		       begin
			  // Now sending the packet proper - byte number is encoded in the state id
			  opbuffmem[outputWp]<=latchedData[ipstate];
			  // One all packet is transmitted this will roll over to STATE_CHECK_AVAIL
			  ipstate<=ipstate+1;
		       end // else: !if(ipstate==STATE_SEND_F7)
		  end
	     end // case: default
	 endcase // case (ipstate)
      end
   endtask // getInput ---------------------------------------------
   

   // ======== Send data to upstairs if relevant, and dispatch to collect more from downstairs ============

   
   

   
   always @(posedge clk)
     begin
	if (rst)
	  begin
	     // ============= Setup reset conditions ============================
	     ipstate<=STATE_CHECK_AVAIL;	     
	     outputRp<=0;	     
	     outputWp<=0;
	     lostSync<=1'b0;	     
	     ovfStretch<=0;
	     DataReady<=0;
	     transmitting<=0;
	     
	  end // else: !if(!rst)
	else
	  begin
	     DataVal<=opbuffmem[outputRp];
	     
	     DataOverf<=(ovfStretch>0);
	     
	     // If we're illuminated for overflow then countdown
	     if (ovfStretch) ovfStretch<=ovfStretch-1;
	     
	     // Check to see if anyone is asking us for data
	     if ((DataNext) && (!transmitting))
	       begin
		  if (outputWp!=outputRp)
		    begin
		       outputRp<=outputRp+1;
		       transmitting<=1;
		       DataReady<=1;
		    end
		  else
		    begin
		       DataReady<=0;
		       transmitting<=0;
		    end // else: !if(outputWp!=outputRp)
	       end // if ((DataNext) && (!transmitting))
	     else
	       begin
		  DataReady<=0;
		  if (!DataNext)
		    transmitting<=0;
	       end

	     // Finally, suck and blow data into our local buffer
	     if (sync) 
	       getInput();
	     else 
	       lostSync<=1'b1; // If we lost sync then re-sync on reconnection
	  end // if (!rst)
     end // always @ (posedge clk)
endmodule // packSend
