// DBG Interface
// From CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0 

`timescale 1ns/100ps

// Tests for dbgIF.
//
// Run with
//  iverilog -o r dbgIF.v swdIF.v ../testbeds/dbgIF_tb.v  ; vvp r
// Make -DSTATETRACE=1 for detail tracing inside swdIF.v.

module dbgIF_tb;
   
   // Testbed interface
   wire        tms_swdio_tb;
   
   reg         rst_tb;
   reg         clk_tb;

   // Interface pins
   wire        tms_swdo_tb;
   wire        swdi_tb;
   wire        tdi_tb;
   wire        tgt_reset_tb;
   reg         tgt_reset_state_tb;
   wire        tck_swclk_tb;
   reg         tdo_swo_tb;
   
   wire        swwr_tb;
   wire        nvsen_pin_tb;
   wire        nvdrive_pin_tb;
   
   

   // Controls
   reg [3:0]   turnaround_tb;
   reg [10:0]  clkDiv_tb;
   wire        vsen_tb;
   wire        vdrive_tb;

   // Messages
   reg [1:0]   addr32_tb;
   reg         rnw_tb;
   reg         apndp_tb;
   reg [39:0]  dwrite_tb;
   wire [2:0]  ack_tb;
   wire [31:0] dread_tb;

   // Actuation & Responses
   reg [3:0]   command_tb;
   
   reg         go_tb;
   wire        done_tb;
   wire        perr_tb;

   reg [44:0]  rx;
   
   assign tms_swdio_tb = (swwr_tb==1)?tms_swdo_tb:swdi_tb;
   assign swdi_tb = rx[0];
   
   
   dbgIF #(.TICKS_PER_USEC(50)) DUT (
	      .rst(rst_tb),        // Reset synchronised to clock
              .clk(clk_tb),

        // Gross control - power etc.
              .vsen(vsen_tb),
              .vdrive(vdrive_tb),
              
	// Downwards interface to the DBG pins
              .swdi(tms_swdio_tb),         // DIO pin from target
              .tms_swdo(tms_swdo_tb),      // DIO pin to target
              .swwr(swwr_tb),              // Direction of DIO pin
              .tck_swclk(tck_swclk_tb),    // Clock out pin to target
              .tdi(tdi_tb),                // TDI to target
              .tdo_swo(tdo_swo_tb),        // TDO from target
              .tgt_reset_state(tgt_reset_state_tb), // Reset to target

              .tgt_reset_pin(tgt_reset_tb),
              .nvsen_pin(nvsen_pin_tb),
              .nvdrive_pin(nvdrive_pin_tb),
              
	// Upwards interface to command controller
              .addr32(addr32_tb),  // Address bits 3:2 for message
              .rnw(rnw_tb),        // Set for read, clear for write
              .apndp(apndp_tb),    // AP(1) or DP(0) access?
              .ack(ack_tb),        // Most recent ack status
              .dwrite(dwrite_tb),  // Data/Parameter out to be written
              .dread(dread_tb),    // Data/Result in

              .command(command_tb),// Command out
              .go(go_tb),          // Trigger
              .done(done_tb),      // Response
              .perr(perr_tb)       // Error status
	      );		  

   integer c=0;
   
   always
     begin
	while (1)
	  begin
             #1;
	     clk_tb=~clk_tb;
	  end
     end

   always @(negedge tck_swclk_tb)
     begin
        rx<={1'b1,rx[45:1]};
     end
   always @(posedge tck_swclk_tb)
        $write("%d",tms_swdio_tb);
   realtime t;
   
   initial begin
      $timeformat( -6, 2," uS", 5);


      go_tb=0;
      
      rst_tb=0;
      clk_tb=0;
      #10;
      rst_tb=1;
      #10;
      rst_tb=0;
      #20;

      clkDiv_tb=11'd2;
      #2;

      // =========================================== Perform a reset
      $display("Performing Reset;");
      command_tb=DUT.CMD_RESET;
      tgt_reset_state_tb<=1;
      t=$realtime;
      go_tb<=1;
      while (done_tb==1) #1;
      go_tb<=0;
      while (done_tb==0) #1;
      $display("Complete, err=%d, time=%t\n\n",perr_tb,$realtime-t);
      
      // =========================================== Set SWD
      $display("Setting SWD;");
      command_tb=DUT.CMD_SET_SWD;
      go_tb<=1;
      while (done_tb==1) #1;
      go_tb<=0;
      while (done_tb==0) #1;
      $display("Complete, err=%d\n\n",perr_tb);
      
      // =========================================== Simple read
      $display("Simple read;");
      rx = { 1'b1, 32'habcdef12, 3'b001, 1'bx, 8'bx };
      addr32_tb=2'b01;
      rnw_tb<=1'b1;
      apndp_tb<=1;
      command_tb<=DUT.CMD_TRANSACT;
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
      $dumpfile("dbgIF.vcd");

      $dumpvars;
   end
endmodule // swdIF_tb
