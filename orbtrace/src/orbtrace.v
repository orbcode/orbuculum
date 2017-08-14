module traceIF(traceDin,traceClk,width,clk,rst);
   input[3:0] traceDin;   // Tracedata
   input traceClk;        // Tracedata clock

   input [1:0] width;     // Width of the trace data
   input       clk;       // System Clock
   input       rst;       // Async reset

   output      dValid;    // Strobe for valid data
   reg 	       dValid;

   output [7:0] dOut;     // ...the valid data
   reg [7:0] 	dOut;

   reg[7:0]    construct;

   // Container for asynchronously received data
   reg [3:0]   latchedtraceDin;
   reg         newData;
 	       
   reg 	       oldtraceclk;
   reg [3:0]   eleCount;
   
   
   always @( posedge rst ) 
     begin
	dOut<=0;
	eleCount<=0;
	oldtraceclk<=0;
	dValid<=0;
	construct<=0;
	newData<=0;
     end

   always @(  posedge traceClk, negedge traceClk )
     begin
	// The sequential = is intentional here - want to make sure 
	// the data is latched before flagging it
	latchedtraceDin = traceDin;
	newData = 1;
     end
      
   always @( posedge clk ) 
     begin
	if ((!rst) && (newData)) 
	  begin
	     
	     construct<=(construct>>(width+1))|(latchedtraceDin<<(7-width));
	     eleCount<=eleCount+(width+1);

	     if ((eleCount+(width+1))>=4)
	       dValid<=0;
	     newData<=0;
	     oldtraceclk=traceClk;
	  end
     end

   always @( negedge clk ) begin
      if ((!rst) && (eleCount==8))
	begin
	   dOut<=construct;
	   construct<=0;
	   dValid<=1;
	   eleCount<=0;
	end
   end
endmodule // traceIF


   
  
