/*
  Concatenates defined bus width input data from TRACEDATA using TRACECLK to bring it into
  specified word lengths of output data. Also monitors for TPIU sync pulses and will
  stop recording data (and provide messaging) if it isn't found.
 
  Working from CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0
 */

module uartTB(
   input 			     clk, // System Clock
   input 			     nRst, // Async reset

   output reg 			     nDValid, // Strobe for valid data
   output reg [OUTPUT_BUS_WIDTH-1:0] dOut // ...the valid data
   );

   // Parameters =============================================================================

   parameter OUTPUT_BUS_WIDTH=8;     // How wide the output bus should be
   parameter DIVISOR=(CLOCKFRQ/10);       // How many outputs per second
   parameter PULSEWIDTH=2;        // Width of the clock pulse in absolute clock cycles

   parameter CLOCKFRQ=240000000;        // Frequency of the oscillator
   
   // Internals =============================================================================
   reg [31:0]  c;
   reg [7:0]   iter;
   
   always @(posedge clk) begin
      if (nRst)
	begin  // Not in reset =====================
	   if (c==(DIVISOR-PULSEWIDTH))
	     begin
		// A new character is to be output - prepare the op buffer
		if (iter<=90)
		  dOut<=iter;
		else
		  if (iter==91)
		    dOut=10;
		  else
		    if (iter==92)
		      dOut=13;
		
		
		c<=c+1;
	     end
	   else
	   if (c==(DIVISOR+1-PULSEWIDTH))
	     begin
		// Character was prepared for output - now  flag it
		nDValid<=1'b0;
		c<=c+1;
	     end
	   else
	     if(c==DIVISOR)
	       begin
		  // This flag cycle is complete - preapare for the next one
		  if (iter==92)
		    iter<=65;
		  else
		    iter<=iter+1;
		  
		  nDValid<=1'b1;	    
		  c<=0;
	       end
	     else
	       c<=c+1;
	end
      else
	begin
	   dOut<=0;
	   nDValid<=1'b1;
	   iter<=65;
	   c<=0;
	end
   end
endmodule // uart_tb.v
