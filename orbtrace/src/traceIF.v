/*
  Concatenates defined bus width input data from TRACEDATA using TRACECLK to bring it into
  specified word lengths of output data. Also monitors for TPIU sync pulses and will
  stop recording data (and provide messaging) if it isn't found.
 
  Working from CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0
 */

module traceIF(
   input[3:0]  traceDin,                   // Tracedata ... 1-4 bits
   input       traceClk,                   // Tracedata clock
   input [1:0] width,                      // Width-1 of the trace data (note width of 3 is not supported)

   input       clk,                        // System Clock
   input       nRst,                       // Async reset

   output reg  dvalid,                     // Strobe for valid data
   output reg [OUTPUT_BUS_WIDTH-1:0] dOut, // ...the valid data
   output       sync                        // Flag indicating sync
   );

   // Parameters =============================================================================

   parameter OUTPUT_BUS_WIDTH=8;     // How wide the output bus should be
   parameter LOST_SYNC_GUARD=24000;  // How many clock cycles before sync is considered lost

   // Internals =============================================================================
   reg [40:0]  lostSync;  // Counter for having lost sync
   reg [31:0]  construct; // data under construction...up to 32 bits worth
   reg         traceClk_clk0; // TraceClk moving into clk domain
   reg         traceClk_clk1;
   reg [3:0]   traceDin_clk0; // TraceData moving into clk domain
   reg [3:0]   traceDin_clk1;
   reg         oldtraceClk_clk;
   
   reg [5:0]   eleCount;  // Elements of constructed value created

   assign sync=(lostSync<LOST_SYNC_GUARD);

   always @(dOut)
     if (nRst)
       dvalid<=1'b1;
     else
       dvalid<=1'b0;       

   always @( negedge clk ) // ---------------------------------------------------------------
     begin
	if (nRst)
	  begin
	     // Move traceClk and data into clk domain
	     traceClk_clk0<=traceClk;
	     traceClk_clk1<=traceClk_clk0;
	     traceDin_clk0<=traceDin;
	     traceDin_clk1<=traceDin_clk0;

	     // Reset any dvalid flag
	     dvalid<=1'b0;
	     
	     if (oldtraceClk_clk!=traceClk_clk1)
	       begin
		  // There was a trace clock state change - clock the data
		  construct<=(construct>>(width+1))|(traceDin_clk1<<(31-width));
		  eleCount<=eleCount+(width+1);
		  oldtraceClk_clk<=traceClk_clk1;
	       end
	     
	     // Deal with potential to output valid data
	     if (eleCount==OUTPUT_BUS_WIDTH)
	       begin
		  eleCount<=0;
		  // Yes, we have valid data, so output it if we are synced
		  if (sync==1'b1)
		    dOut<=construct[31:32-OUTPUT_BUS_WIDTH];
	       end

	     // Deal with potential that we've reached sync...this pattern can only appear interpacket
	     if (construct==32'h7fffffff)
	       begin
		  // Yup, so flag it and reset the timeout
		  lostSync=0;
		  eleCount<=0;
		  construct<=0;
	       end
	     else
	       // Move closer to a lost sync condition
	       lostSync=lostSync+1;
	  end
	else
	  begin
	     // Reset states
	     dOut<=0;
	     eleCount<=0;
	     construct<=0;
	     traceClk_clk0<=0;
	     traceClk_clk1<=0;	     
	     lostSync<=LOST_SYNC_GUARD;
	     oldtraceClk_clk<=0;
	     traceDin_clk0<=0;
	     traceDin_clk1<=0;	     
	  end // else: !if(nRst)
     end
endmodule // traceIF
