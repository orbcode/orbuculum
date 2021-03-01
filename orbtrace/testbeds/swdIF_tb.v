// SWD-DP Interface
// From CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0 

`timescale 1ns/100ps

// Tests for swdIF.
//
// Run with
//  iverilog -o r swdIF.v ../testbeds/swdIF_tb.v  ; vvp r
// Make -DSTATETRACE=1 for detail tracing inside swdIF.v.

module swdIF_tb;

   
   // Testbed interface
   wire        swdio_tb;
   
   reg         rst_tb;
   reg         clk_tb;
   wire        swdo_tb;
   wire        swdi_tb;
   wire        swclk_tb;
   wire        swwr_tb;
   reg [10:0]  clkDiv_tb;
   reg [1:0]   addr32_tb;
   reg         rnw_tb;
   reg         apndp_tb;
   reg [31:0]  dwrite_tb;
   wire [2:0]  ack_tb;
   wire [31:0] dread_tb;
   reg         go_tb;
   wire        done_tb;
   reg [44:0]  rx;
   wire        perr_tb;
   
   assign swdio_tb = (swwr_tb==1)?swdo_tb:swdi_tb;
   assign swdi_tb = rx[0];
   
   
   swdIF DUT (
	      .rst(rst_tb),        // Reset synchronised to clock
              .clk(clk_tb),
                
	// Downwards interface to the SWD pins
              .swdi(swdio_tb),     // DIO pin from target
              .swdo(swdo_tb),      // DIO pin to target
              .swwr(swwr_tb),      // Direction of DIO pin
              .swclk(swclk_tb),    // Clock out pin to target

        // Configuration
              .clkDiv(clkDiv_tb),  // Divisor per clock change to target
                
	// Upwards interface to command controller
              .addr32(addr32_tb),  // Address bits 3:2 for message
              .rnw(rnw_tb),        // Set for read, clear for write
              .apndp(apndp_tb),    // AP(1) or DP(0) access?
              .dread(dread_tb),    // Most recent data in

              .ack(ack_tb),        // Most recent ack status
              .dwrite(dwrite_tb),  // Data out to be written
              .perr(perr_tb),      // Parity Error status
              
              .go(go_tb),          // Trigger
              .done(done_tb)       // Response
	      );		  

   integer c=0;
   
   always
     begin
	while (1)
	  begin
             #2;
	     clk_tb=~clk_tb;
	  end
     end

   always @(negedge swclk_tb)
     begin
        rx<={1'b1,rx[45:1]};
     end
   always @(posedge swclk_tb)
        $write("%d",swdio_tb);
   
   initial begin
      go_tb=0;
      
      rst_tb=0;
      clk_tb=0;
      #10;
      rst_tb=1;
      #10;
      rst_tb=0;
      #20;

      clkDiv_tb=11'd2;

      // =========================================== Simple read
      $display("Simple read;");
      rx = { 1'b1, 32'habcdef12, 3'b001, 1'bx, 8'bx };
      addr32_tb=2'b01;
      rnw_tb<=1'b1;
      apndp_tb<=1;
      go_tb<=1;
      while (done_tb==1) #1;
      go_tb<=0;
      while (done_tb==0) #1;
      if (perr_tb)
        $display("\nReturned Parity Error");
      else
        begin
           if ((ack_tb==3'b001) && (dread_tb==32'habcdef12))
             $write("\nOK ");
           else
             $write("\nFAULT ");
             $display("Returned [ACK %3b %08x]",ack_tb,dread_tb);
        end

      #10;
      
      // ======================================== Parity error read
      $display("\n\nParity error read;");
      rx = { 1'b0, 32'habcdef12, 3'b001, 1'bx, 8'bx };
      addr32_tb=2'b01;
      rnw_tb<=1'b1;
      apndp_tb<=1;
      go_tb<=1;
      while (done_tb==1) #1;
      go_tb<=0;
      while (done_tb==0) #1;
      if (perr_tb)
        $display("\nOK Returned Parity Error");
      else
        begin
           $write("\nFAULT ");
           $display("Returned [ACK %3b %08x]",ack_tb,dread_tb);
        end

      // ======================================== Wait read
      $display("\n\nWait read;");
      rx = { 1'b0, 32'habcdef12, 3'b010, 1'bx, 8'bx };
      addr32_tb=2'b01;
      rnw_tb<=1'b1;
      apndp_tb<=1;
      go_tb<=1;
      while (done_tb==1) #1;
      go_tb<=0;
      while (done_tb==0) #1;
      if (ack_tb==3'b010)
        $display("\nOK Returned WAIT");
      else
        begin
           $write("\nFAULT ");
           $display("Returned [ACK %3b %08x]",ack_tb,dread_tb);
        end
      
      // =========================================== Simple write
      $display("\n\nSimple write;");
      rx = { 1'b1, 32'hx, 3'b001, 1'bx, 8'bx };
      addr32_tb=2'b01;
      rnw_tb<=1'b0;
      apndp_tb<=1;
      dwrite_tb<=32'habcdef12;
      go_tb<=1;
      while (done_tb==1) #1;
      go_tb<=0;
      while (done_tb==0) #1;
      if (perr_tb)
        $display("\nReturned Parity Error");
      else
        begin
           if (ack_tb==3'b001)
             $write("\nOK ");
           else
             $write("\nFAULT ");
           $display("Returned [ACK %3b]",ack_tb);
        end

      #10;
      
      
      #50;
      
      $finish;
      
   end
   initial begin
      $dumpfile("swd_IF.vcd");

      $dumpvars;
   end
endmodule // swdIF_tb
