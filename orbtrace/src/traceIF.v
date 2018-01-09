`default_nettype none

// traceIF
// =======
// Concatenates defined bus width input data from TRACEDATA using TRACECLK to bring it into
// 16 bit chunks which are stored in memory if they're not 'pass' cases. Also deals with top
// level sync. Above this level nothing needs to know how wide the tracebus actually is.
// 
// Working from CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0

module traceIF # (parameter BUSWIDTH = 4) (
		input 	       clk, // System Clock
		input 	       rst, // Reset synchronised to clock

	// Downwards interface to the trace pins
		input [BUSWIDTH-1:0]    traceDina, // Tracedata rising edge... 1-n bits max, can be less
		input [BUSWIDTH-1:0]    traceDinb, // Tracedata falling edge... 1-n bits max, can be less	
		input 	       traceClkin, // Tracedata clock... async to clk
		input [2:0]    width, // How wide the bus under consideration is 1..4 (0, 3 & >4 invalid)

	// Upwards interface to packet processor
		output reg     PackAvail, // Flag indicating packet is available
		input 	       PackAvailAck, // Flag indicating we've heard about the available packet
		
		output reg[127:0] PacketOut, // The next packet word
		output reg     sync // Indicator that we are in sync
		);		  
   
   // Internals =============================================================================

   reg [35:0] 			  construct;                // Track of data being constructed (extra eight bits long for mis-sync)
   reg [4:0] 			  readBits;                 // How many bits of the sample we have


   reg [15:0] 			  constructFrame[0:14];     // Storage for packet under construction (128-16 bits wide)   
   
   reg [3:0] 			  traceipmemWriteOfs;
   
   reg [2:0] 			  gotSync;                  // Pulse stretch between domains for sync

   reg [25:0] 			  lostSync;                 // Counter for sync loss timeout

   reg [2:0] 			  offset;
   wire 			  newSync;

   wire 			  PackAvailClkA;
   
   TaskAck_CrossDomain s (
    .clkA(traceClkin),
    .TaskStart_clkA(PackAvailClkA),

    .clkB(clk),
    .TaskBusy_clkB(PackAvail),
    .TaskDone_clkB(PackAvailAck)
			  );
   

   
   // ================== Input data against traceclock from target system ====================
   always @(posedge traceClkin)
     begin
	if (rst)
	  begin
	     traceipmemWriteOfs <= 0;
	     readBits<=0;
	     construct<=36'b0;
	     gotSync<=1'b0;
	     PackAvailClkA<=1'b0;
	  end
	else
	  begin
	     // Deal with sync flagging ===============================================
	     if (gotSync==0)
	       begin
		  newSync=1;
		  if (construct[35:4]==32'h7fff_ffff) offset<=4;
		  else
		    if (construct[34:3]==32'h7fff_ffff) offset<=3;		    
		    else
		      if (construct[33:2]==32'h7fff_ffff) offset<=2;		    
		      else
			if (construct[32:1]==32'h7fff_ffff) offset<=1;		    
			else
			  if (construct[31:0]==32'h7fff_ffff) offset<=0;
			  else
			    newSync=0;
	       end else // if (gotSync==0)
		 newSync=0;
	     
	     if (newSync==1)
	       begin
		  gotSync<=~3'd0;       // We synced, let people know
		  readBits<=width<<1;   // ... and reset writing position in recevied data
		  traceipmemWriteOfs <= 0; // ... and in the packet
	       end
	     else
	       begin
		  // Countdown the sync flag ==========================================
		  if (gotSync>0) gotSync<=gotSync-3'd1;

		  // Deal with element reception ======================================
		  if ((sync) && (readBits==16))
		    begin
		       // Got a full set of bits, so store them ... except if they're useless
		       if (construct[31+offset:16+offset]!=16'h7fff)
			 begin
			    // This is useful data, and we are in sync, so flag to store it
			    if (traceipmemWriteOfs<7)
			      begin
				 constructFrame[traceipmemWriteOfs]<=construct[31+offset:16+offset];
				 traceipmemWriteOfs<=traceipmemWriteOfs+1;
				 PackAvailClkA<=1'b0;
			      end
			    else
			      begin
				 // This packet is complete - deal with it
				 PacketOut<={construct[31+offset:16+offset],constructFrame[6],constructFrame[5],
					     constructFrame[4],constructFrame[3],constructFrame[2],
					     constructFrame[1],constructFrame[0]};
				 PackAvailClkA<=1'b1;
				 traceipmemWriteOfs<=0;
			      end // else: !if(traceipmemWriteOfs<7)
			 end 
		       
		       // Either a 7fff packet to be ignored or real data. Either way, we got 16 bits
		       // of it, so reset the bits counter (bearing in mind we'll take a sample this cycle)
		       readBits<=width<<1;
		    end // if ((sync) && (readBits==16))
		  else
		    begin
		       // We are still collecting data...increment the bits counter
		       readBits<=readBits+(width<<1);
		    end // else: !if((sync) && (readBits==16))
	       end // else: !if(construct==32'h7fff_ffff)
		  
	     // Now get the new data elements that have arrived
	     case (width)
	       1: construct<={traceDinb[0],traceDina[0],construct[35:2]};
	       2: construct<={traceDinb[1:0],traceDina[1:0],construct[35:4]};
	       4: construct<={traceDinb[3:0],traceDina[3:0],construct[35:8]};
	       default:  construct<=32'b0;  // These cases cannot really occur!
	     endcase // case (width)
	  end // else: !if(rst)
     end // always @ (posedge traceClk)
   
   always @( posedge clk ) // Move traceClk into on-chip clock domain
     begin
	if (rst)
	  begin
	     // Reset states
	     lostSync<=0;
	     sync<=0;
	  end // else: !if(!rst)
	else
	  begin
	     // Let everyone know the state of sync at the moment
	     sync<=(lostSync!=0);
	     
	     // ...and count down sync in case we don't see it again
	     if (gotSync) lostSync<=~26'b0;
	     else 
	       if (lostSync>0) lostSync<=lostSync-26'd1;
	  end // else: !if(rst)
     end // always @ ( posedge clk )
endmodule // traceIF
