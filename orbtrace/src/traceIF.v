// traceIF
// =======
// Concatenates defined bus width input data from TRACEDATA using TRACECLK to bring it into
// specified word lengths of output data.  Samples are recorded at maximum width even if only
// a subset are actualy configured for use (this level doesn't even know).
// 
// Working from CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0


module traceIF (
		input [MAX_BUS_WIDTH-1:0]      traceDin, // Tracedata ... 1-n bits
		input 			       traceClk, // Tracedata clock

		input 			       clk, // System Clock
		input 			       nRst, // Async reset

		input 			       dNext, // Strobe for requesting next data element
		output 			       dAvail, // Flag indicating data is available
		output reg [MAX_BUS_WIDTH-1:0] dOut // ...the valid data write position
   );

   // Parameters =============================================================================

   parameter MAX_BUS_WIDTH=4;  // Maximum bus width that system is set for 
   
   // Internals =============================================================================

   assign dAvail = (tracememWp!=tracememRp);  // Flag showing that there is trace data available
   
   reg [9:0] 	    tracememWp;               // Write pointer in trace memory
   reg [9:0] 	    tracememRp;               // Read pointer in trace memory
	    
   reg [MAX_BUS_WIDTH-1:0] tracemem[0:1023];  // Sample memory

   reg 		    traceClkB;
   reg 		    oldTraceClk;

   reg 		    dNextPrev;                // Used for spotting edges in upper level sample requests
 		    
   always @( negedge clk ) // Move traceClk into on-chip clock domain
     if (nRst)
       begin
	  traceClkB<=traceClk;

	  if (traceClkB!=oldTraceClk)
	    begin
	       //  ------ There was a trace clock state change - clock the data
	       tracemem[tracememWp]<=traceDin;
	       tracememWp<=tracememWp+1;
	       oldTraceClk<=traceClkB;
	    end

	  if ((dNext==1'b1) && (dNextPrev==1'b0))
	    begin
	       // A positive clock edge - make the next sample available
	       dOut<=tracemem[tracememRp];
	       tracememRp<=tracememRp+1;
	       dNextPrev<=1'b1;
	    end
	  else
	    begin
	       dNextPrev<=dNext;
	    end
       end
     else
       begin
	  // Reset states
	  oldTraceClk<=traceClk;
	  traceClkB<=traceClk;
	  dNextPrev<=dNext;
	  tracememWp<=0;
	  tracememRp<=0;
	  dOut<=0;
       end // else: !if(nRst)
endmodule // traceIF
