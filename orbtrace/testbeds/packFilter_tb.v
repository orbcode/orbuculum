// TPIU Signaling framework. 
// From CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0 

`timescale 1ns/100ps

module packFilter_tb();
   reg oldclk;
   reg 	    clk;
   reg 	    rst;
	    
   reg 	      sync;                  // Indicator that we are in sync
  

   reg       pack_avail_tb;           // Flag indicating packet available
   wire      pack_next_tb;            // Requester from extern to give next packet
   wire      pack_next_word_tb;       // Requester from extern to give next 16 word in packet
   	    
   reg [15:0] pack_element_tb;  // next 16 bits of the packet
   reg [7:0]  pack_final_tb;     // The last byte of the packet

   wire       data_avail_tb;
   wire [7:0] data_val_tb;

   reg 	      data_req_tb;
   
   
   packFilter DUT(
		  .clk(clk), // System Clock
		  .rst(rst), // System reset
		  .sync(sync), // Indicator of if we are in sync

		  // Downwards interface to packet processor
		  .PacketAvail(pack_avail_tb), // Flag indicating packet is available
		  .PacketNext(pack_next_tb), // Strobe for requesting next packet
		  .PacketNextWd(pack_next_word_tb), // Strobe for next 16 bit word in packet	 

		  .PacketIn(pack_element_tb), // The next packet word
		  .PacketFinal(pack_final_tb), // The last word in the packet (used for reconstruction)

		  // Upwards interface to serial (or other) handler
		  .DataAvail(data_avail_tb), // There is decoded data ready for processing
		  .DataReq(data_req_tb), // Request for data
		  .DataVal(data_val_tb)    //  	 Output data value
		  
		  );

   function clk_posedge;
      input   c;
      begin
	 clk_posedge=((c==1'b1) && (oldclk!=c));
	 oldclk=c;
      end
   endfunction

   function clk_negedge;
      input c;
      begin
	 clk_negedge=((c==1'b0) && (oldclk!=c));
	 oldclk=c;
      end
   endfunction

   always @(posedge pack_next_tb)
     $display("Next packet requested");

   always
     begin
	while (1)
	  begin
	     clk=~clk;
	     #2;
	  end
     end

      
      
   initial begin
      rst=0;
      sync=0;
      clk=0;
      pack_avail_tb=0;
      pack_element_tb=16'h7fff;
      pack_final_tb=8'hff;
      data_req_tb=0;
      
      
      #10;
      rst=1;
      #10;
      rst=0;
      #5;

      sync=1;
      #5;

      if (pack_next_tb!==1'b0)
	$display("ERROR:PacketNext asserted before packet available");

      $display("Flagging packet available...");
      oldclk=clk;
      while (!clk_posedge(clk)) #0.1;      
      pack_avail_tb=1;
      pack_final_tb=8'hda;

      while (!pack_next_tb) #0.1;
      
      while (!clk_posedge(clk)) #0.1;
      pack_avail_tb=0;
      
      // Now wait for word request
      while (!pack_next_word_tb) #0.1;

      while (!clk_posedge(clk)) #0.1;




      $display(" 0:C=01 2a");
      pack_element_tb=16'h2a03;

      while ((!pack_next_word_tb) || (!clk_posedge(clk))) #0.1;
      $display(" 1:C=01 01 00");
      pack_element_tb=16'h0000;

      while ((!pack_next_word_tb) || (!clk_posedge(clk))) #0.1;      
      $display(" 2:C=22 99");
      pack_element_tb=16'h9945;
      
      while ((!pack_next_word_tb) || (!clk_posedge(clk))) #0.1;
      $display(" 3:C=22 33");
      pack_element_tb=16'h3387;

      while ((!pack_next_word_tb) || (!clk_posedge(clk))) #0.1;
      $display(" 4:C=43 01 02");
      pack_element_tb=16'h0200;
      
      while ((!pack_next_word_tb) || (!clk_posedge(clk))) #0.1;
      $display(" 5:C=22 44");
      pack_element_tb=16'h4445;
   
      while ((!pack_next_word_tb) || (!clk_posedge(clk))) #0.1;
      $display(" 6:C=22 55");
      pack_element_tb=16'h55E5;
     
      while ((!pack_next_word_tb) || (!clk_posedge(clk))) #0.1;
      $display(" 7:C=72 11");
      pack_element_tb=16'hca10;

      #50;
      
      while (data_avail_tb)
	begin
	   data_req_tb<=0;
	   #4;
	   data_req_tb<=1;
	   #4;

	   $display("%02X",data_val_tb);
	end
      #30;
      
      $finish;

   end
   initial begin
      $dumpfile("trace_IF.vcd");

      $dumpvars;
   end
endmodule 

  
