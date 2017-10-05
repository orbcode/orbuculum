// TPIU Signaling framework. 
// From CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0 

`timescale 1ns/100ps

module traceIF_tb();
   parameter chunksize=(4-1);

   reg[3:0] traceDin;  // Port is always 4 bits wide, even if we use less
   reg 	    traceClk;
   reg 	    clk;
   reg 	    nRst;

   wire     dAvail_tb;
   wire [3:0] dout_tb;
   
   reg 	    dReqNext_tb;
   
   traceIF DUT (
		.traceDin(traceDin), // Tracedata ... 1-4 bits
		.traceClk(traceClk), // Tracedata clock
		.clk(clk), // System Clock
		.nRst(nRst), // Async reset

		
		.dNext(dReqNext_tb), // Strobe for requesting next data element
		.dAvail(dAvail_tb), // Flag indicating data is available
		.dOut(dout_tb) // ...the valid data write position
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
      
   #1;
   
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
      dReqNext_tb=0;
      
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
      dReqNext_tb=1;
      #10;
      dReqNext_tb=0;      
      sendByte(8'hff);
      sendByte(8'hff);
      sendByte(8'h7f);
      // ....and a sane message
      sendByte(8'h42);
      dReqNext_tb=1;
      #10;
      dReqNext_tb=0;      
      
      sendByte(8'h71);
      sendByte(8'h19);
      sendByte(8'h69);
      sendByte(8'h12);      
      #30;
      dReqNext_tb=1;
      #10;
      dReqNext_tb=0;      
      #10;
      dReqNext_tb=1;
      #10;
      dReqNext_tb=0;      
      #10;
      dReqNext_tb=1;
      #10;
      dReqNext_tb=0;      
      #10;
      dReqNext_tb=1;
      #10;
      dReqNext_tb=0;      
      #10;
      dReqNext_tb=1;
      #10;
      dReqNext_tb=0;      
      #10
;
      
      $finish;
      
   end
   initial begin
      $dumpfile("trace_IF.vcd");

      $dumpvars;
   end
endmodule // trace_IF_tb

   
   
