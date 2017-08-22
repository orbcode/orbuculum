// TPIU Signaling framework. 
// From CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0 

`timescale 1ns/100ps

module packCollect_tb();
   parameter chunksize=(1-1);

   reg[3:0] traceDin;  // Port is always 4 bits wide, even if we use less
   reg 	    traceClk;
   reg 	    clk;
   reg 	    nRst;
   reg [1:0] width_tb;
	    
   wire     dAvail_tb;
   wire [3:0] dout_tb;
   
   wire       dReqNext_tb;

   wire       sync_tb;

   wire     pack_avail_tb;
   reg 	    pack_next_tb;
   reg 	    pack_next_word_tb;
   
	    
   wire     [15:0] pack_element_tb;  
   wire     [7:0] pack_final_tb;
   
   traceIF trace (
		.traceDin(traceDin), // Tracedata ... 1-4 bits
		.traceClk(traceClk), // Tracedata clock
		.clk(clk), // System Clock
		.nRst(nRst), // Async reset

		
		.dNext(dReqNext_tb), // Strobe for requesting next data element
		.dAvail(dAvail_tb), // Flag indicating data is available
		.dOut(dout_tb) // ...the valid data write position
   );

   packCollect DUT (
		   .clk(clk), // System Clock
		   .nRst(nRst), // System reset
		   .width(width_tb), // Current trace buffer width
		    .sync(sync_tb), // Indicator of if we are in sync

		   // Downwards interface to trace elements
		   .TraceNext(dReqNext_tb), // Strobe for requesting next trace data element
		    .TraceAvail(dAvail_tb), // Flag indicating trace data is available
		    .TraceIn(dout_tb), // ...the valid Trace Data
		    
		   // Upwards interface to packet processor
		    .PacketAvail(pack_avail_tb), // Flag indicating packet is available
		   .PacketNext(pack_next_tb), // Strobe for requesting next packet
		   .PacketNextWd(pack_next_word_tb), // Strobe for next 16 bit word in packet	 

		   .PacketOut(pack_element_tb), // The next packet word
		   .PacketFinal(pack_final_tb) // The last word in the packet (used for reconstruction)
		   );
   
   
   //-----------------------------------------------------------
   task sendByte;
      input[7:0] byteToSend;
      integer bitsToSend;

      begin
	 bitsToSend=8;
	 while (bitsToSend>0) begin
	    traceDin[chunksize:0]=byteToSend;
	    #10;
	    traceClk<=!traceClk;
	    #10;
	    bitsToSend=bitsToSend-(chunksize+1);
	    byteToSend=byteToSend>>(chunksize+1);	    
	 end
      end
   endtask
   //-----------------------------------------------------------      
      

   always
     begin
	while (1)
	  begin
	     clk=~clk;
	     #2;
	  end
     end
      
   initial begin
      nRst=1;
      width_tb=chunksize;
      pack_next_tb=0;      
      traceDin=0;
      traceClk=0;

      clk=0;
      #10;
      nRst=0;
      #10;
      nRst=1;
      #100;

      // Initially send some junk while out of sync
      sendByte(8'hfe);
      sendByte(8'h23);

      // Now send Sync Sequence
      sendByte(8'hff);
      sendByte(8'hff);
      sendByte(8'hff);
      sendByte(8'h7f);
      // ....and a sane message
      sendByte(8'h00);
      sendByte(8'h10);
      sendByte(8'h01);
      sendByte(8'h11);
      sendByte(8'h02);
      sendByte(8'h12);      
      sendByte(8'h03);
      sendByte(8'h13);      
      sendByte(8'h04);
      sendByte(8'h14);      
      sendByte(8'h05);
      sendByte(8'h15);      
      sendByte(8'h06);
      sendByte(8'h16);      
      sendByte(8'h07);
      sendByte(8'h68);
      if (pack_avail_tb==1'b0)
	begin
	   $display("No packet available before packet complete: CORRECT");
	end
      else
	begin
	   $display("Packet available signalled before packet complete: ERROR");
	end
      
      sendByte(8'hff);
      #50;
      if (pack_avail_tb==1'b1)
	begin
	   $display("Packet available after packet complete: CORRECT");
	end
      else
	begin
	   $display("No Packet available signalled after packet complete: ERROR");
	end

      pack_next_tb=1;
      #4;
      pack_next_tb=0;
      #10

      if (pack_avail_tb==1'b1)
	begin
	   $display("Packet available after packet receive: ERROR");
	end
      else
	begin
	   $display("No Packet available signalled after packet receive: CORRECT");
	end
      #10;

      $display("68:%02X",pack_final_tb);
      pack_next_word_tb=1;
      #4;
      pack_next_word_tb=0;
      #4;
      $display("1000:%04X",pack_element_tb);
      pack_next_word_tb=1;
      #4;
      pack_next_word_tb=0;
      #4;
      $display("1101:%04X",pack_element_tb);
      pack_next_word_tb=1;
      #4;
      pack_next_word_tb=0;
      #4;
      $display("1202:%04X",pack_element_tb);
      pack_next_word_tb=1;
      #4;
      pack_next_word_tb=0;
      #4;
      $display("1303:%04X",pack_element_tb);
      pack_next_word_tb=1;
      #4;
      pack_next_word_tb=0;
      #4;
      $display("1404:%04X",pack_element_tb);
      pack_next_word_tb=1;
      #4;
      pack_next_word_tb=0;
      #4;
      $display("1505:%04X",pack_element_tb);
      pack_next_word_tb=1;
      #4;
      pack_next_word_tb=0;
      #4;
      $display("1606:%04X",pack_element_tb);
      pack_next_word_tb=1;
      #4;
      pack_next_word_tb=0;
      #4;
      $display("6807:%04X",pack_element_tb);
      pack_next_word_tb=1;
      #4;
      pack_next_word_tb=0;
      #4;
      $display("6807:%04X",pack_element_tb);
      
      
      
      $finish;
      
   end
   initial begin
      $dumpfile("trace_IF.vcd");

      $dumpvars;
   end
endmodule 

   
   
