// traceIF
// =======
// Concatenates defined bus width input data from TRACEDATA using TRACECLK to bring it into
// 16 bit chunks which are stored in memory if they're not 'pass' cases. Also deals with top
// level sync. Above this level nothing needs to know how wide the tracebus actually is.
// 
// Working from CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0

module traceIF(
	       input 		 clk,          // System Clock
	       input 		 rst,          // Reset synchronised to clock

	       // Downwards interface to the trace pins
	       input [3:0] 	 traceDina,    // Tracedata rising edge... 1-n bits max, can be less
	       input [3:0] 	 traceDinb,    // Tracedata falling edge... 1-n bits max, can be less	
	       input 		 traceClkin,   // Tracedata clock... async to clk
	       input [2:0] 	 width,        // How wide the bus under consideration is 1..4 (0, 3 & >4 invalid)

		// Upwards interface to packet processor
	       output reg 	 PacketAvail,  // Flag indicating packet is available
	       input 		 PacketNext,   // Strobe for requesting next packet
	       input 		 PacketNextWd, // Strobe for next 16 bit word in packet	 

	       output reg [15:0] PacketOut,    // The next packet word
	       output reg 	 sync          // Indicator that we are in sync
	       );		  
   
   // Internals =============================================================================

   reg [39:0] 	    construct;                // Track of data being constructed
   reg [4:0] 	    readBits;                 // How many bits of the sample we have
	    
   reg [15:0] 	    traceipmem[0:255];        // Storage for packet under construction (16 bits wide, 2 bytes at a time)
   reg [4:0] 	    traceipmemNumWp;          // Write pointer for packet number 
   reg [3:0] 	    traceipmemOffsWp;         // Write pointer for offset in the packet

   reg [4:0] 	    traceipmemNumRp;          // Read pointer for packet number
   reg [2:0] 	    traceipmemOffsRp;         // Read pointer for offset in the packet
   reg [4:0] 	    packetReading;            // Packet I am currently reading in the buffer

   reg [2:0]        gotSync;                  // Pulse stretch between domains for sync
   
   reg [24:0] 	    lostSync;                 // Counter for sync loss timeout

   reg [1:0] 	    ofs;
   reg [2:0] 	    ofsSet;
	
   reg 		    storeWaiting;

   // ================== Input data against traceclock from target system ====================
   always @(posedge traceClkin) 
     begin
	if (rst)
	  begin
	     traceipmemNumWp <= 0;
	     traceipmemOffsWp <= 0;
	     readBits<=0;
	     construct<=0;
	     gotSync<=1'b0;
	     storeWaiting<=0;
	  end
	else
	  begin
	     // Deal with sync flagging ===============================================
	     ofsSet[2]=1;

	     // These apply for all cases
	     if (construct[39:8]==32'h7fff_ffff) ofsSet=0;
	     if (construct[38:7]==32'h7fff_ffff) ofsSet=1;

	     // For a two bit bus
	     if ((width==3'd2) || (width==3'd4))
	       begin
		  if (construct[37:6]==32'h7fff_ffff) ofsSet=2;
		  if (construct[36:5]==32'h7fff_ffff) ofsSet=3;
	       end

	     // For a four bit bus...
	     if (width==3'd4)
	       begin
		  if (construct[36:5]==32'h7fff_ffff) ofsSet=4;
		  if (construct[35:4]==32'h7fff_ffff) ofsSet=5;
		  if (construct[34:3]==32'h7fff_ffff) ofsSet=6;
		  if (construct[33:2]==32'h7fff_ffff) ofsSet=7;
	       end

	     if (!ofsSet[2])
	       begin
		  gotSync<=~0;          // We synced, let people know
		  readBits<=width<<1;   // ... and reset writing position in recevied data
		  traceipmemOffsWp<=0;  // ... and in the packet
		  ofs<=ofsSet[1:0];
	       end
	     else
	       begin
		  // Countdown the sync flag ==========================================
		  if (gotSync>0) gotSync<=gotSync-1;

		  // The store is done post-collection to speed up the critical path through the logic
		  if (storeWaiting)
		    begin
		       storeWaiting<=0;
		       // Sample to be stored has moved back one more step in memory..so accomodate that
		       traceipmem[{traceipmemNumWp[4:0],traceipmemOffsWp[2:0]}]<=construct[39-ofs-(width*2):24-ofs-(width*2)];
		       
		       // Check if we are still collecting this packet, adjust pointers appropriately
		       if (traceipmemOffsWp<7)
			 begin
			    traceipmemOffsWp<=traceipmemOffsWp+1;
			 end 
		       else
			 begin
			    // This packet is complete, move on to the next one
			    traceipmemNumWp<=traceipmemNumWp+1;
			    traceipmemOffsWp<=0;
			 end
		    end

		  // Deal with element reception ======================================
		  if ((sync) && (readBits==16))
		    begin
		       // Got a full set of bits, so store them ... except if they're useless
		       if (construct[39-ofs:24-ofs]!=16'h7fff)
			 begin
			    // This is useful data, and we are in sync, so flag to store it
			    storeWaiting<=1'b1;
			 end // if ((construct[31:16]!=16'h7fff) && (sync))
		       // Either a 7fff packet to be ignored or real data. Either way, we got 16 bits
		       // of it, so reset the bits counter (bearing in mind we'll take a sample this cycle)
		       readBits<=width<<1;
		    end // if ((sync && (readBits==16))
		  else
		    begin
		       // We are still collecting data...increment the bits counter
		       readBits<=readBits+(width<<1);
		    end // else: !if(readBits==16)
	       end // else: !if(!ofsSet[2])
	     
	     // Now get the new data elements that have arrived
	     case (width)
	       1: construct<={traceDinb[0],traceDina[0],construct[39:2]};
	       2: construct<={traceDinb[1:0],traceDina[1:0],construct[39:4]};
	       4: construct<={traceDinb[3:0],traceDina[3:0],construct[39:8]};
	       default:  construct<=0;  // These cases cannot really occur!
	     endcase // case (width)
	  end // else: !if(rst)
     end // always @ (posedge traceClk)
   
   // ============= Move traceclk synced data into on-chip domain =====================
   always @( posedge clk ) // Move traceClk into on-chip clock domain
     begin
	if (rst)
	  begin
	     // Reset states
	     traceipmemNumRp <= 0;
	     traceipmemOffsRp <= 0;
	     packetReading <= 0;
	     PacketAvail<=0;
	     PacketOut<=0;
	     lostSync<=0;
	     sync<=0;
	  end // else: !if(!rst)
	else
	  begin
	     // Let everyone know the state of sync at the moment
	     sync<=(lostSync!=0);

	     // ...and count down sync in case we don't see it again
	     if (gotSync)
	       begin
		  lostSync<=~0;
	       end
	     else if (lostSync>0)
	       begin
		  lostSync<=lostSync-1;
	       end

	     // Flag for upstairs showing that there is a packet available to process
	     PacketAvail<=(traceipmemNumWp!=traceipmemNumRp);

	     // If upstairs is asking us for data then send it if we've got it
	     if (PacketNext==1'b1) // Request to read next packet 
	       begin
		  if (traceipmemNumWp!=traceipmemNumRp)
		    begin
		       // Yes, we've got one, get it ready to go
		       packetReading<=traceipmemNumRp;
		       traceipmemOffsRp<=0;
		       traceipmemNumRp<=traceipmemNumRp+1;
		    end
	       end
	     else
	       if (PacketNextWd==1'b1)  // Deal with request to read next word from packet
		 begin
		    if (traceipmemOffsRp<8) // Don't wander off the end of the packet
		      begin
			 PacketOut<=traceipmem[{packetReading,traceipmemOffsRp[2:0]}];
			 traceipmemOffsRp<=traceipmemOffsRp+1;
		      end
		 end
	  end // else: !if(rst)
     end // always @ ( posedge clk )
endmodule // traceIF
