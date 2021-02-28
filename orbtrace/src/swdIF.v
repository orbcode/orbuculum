`default_nettype none

// swdIF
// =====
// 
// Working from ARM Debug Interface Architecture Specification ADIv5.0 to ADIv5.2
// Modelled on structure from DAPLink implementation at https://github.com/ARMmbed/DAPLink
// which in Apache 2.0 Licence.
// This gateware uses (obviously!) uses no code from DAPLink and is under BSD licence.

module swdIF (
		input             rst,      // Reset synchronised to clock
                input             clk,
                
	// Downwards interface to the SWD pins
                input             swdi,     // DIO pin from target
                output            swdo,     // DIO pin to target
                output reg        swclk,    // Clock out pin to target
                output reg        swwr,     // Direction of DIO pin

        // Configuration
                input [10:0]      clkDiv,   // Divisor per clock change to target
                input [3:0] turnaround,
                
	// Upwards interface to command controller
                input [1:0]       addr32,   // Address bits 3:2 for message
                input             rnw,      // Set for read, clear for write
                input             apndp,    // AP(1) or DP(0) access?
                input [31:0]      din,      // Most recent data in
                output reg [2:0]  ack,      // Most recent ack status
                output reg [31:0] dout,     // Data out to be written
                output reg        err,      // Indicator of an error in the transfer

                input             go,       // Trigger
                output reg        done      // Response
		);		  
   
   // Internals =======================================================================
   reg [10:0]                     cdivcount;   // divisor for external clock
   
   reg [3:0]                      swd_state;   // current state of machine
   reg [2:0]                      ack_in;      // ack coming in from target
   reg [1:0]                      go_cdc;      // cdc from go trigger
   reg [5:0]                      bitcount;    // Counter for bits being transferred
   reg [31:0]                     bits;        // The bits being transferred
   reg                            par;         // Parity construct

`ifndef SYNTHESIS   
   reg [3:0]                      swd_state_change;
`endif

   parameter ST_IDLE=0;
   parameter ST_HDR_TX=1;
   parameter ST_TRN1=2;
   parameter ST_ACK=3;
   parameter ST_TRN2=4;
   parameter ST_DWRITE=5;
   parameter ST_DWRITEPARITY=6;
   parameter ST_DREAD=7;
   parameter ST_DREADPARITY=8;   
   parameter ST_COOLING=9;

   wire   posclkedge = ((!cdivcount) & (!swclk));
   wire   negclkedge = ((!cdivcount) & (swclk));
   assign swdo = bits[0];
   
   always @(posedge clk, posedge rst)
     begin
        // Default status bits
	if (rst)
	  begin
             swd_state<=ST_IDLE;
             swclk<=1'b1;
             done<=1;
             swwr<=0;
             go_cdc<=0;
             err<=0;
	  end
	else
	  begin
             go_cdc = {go,go_cdc[1]};

`ifndef SYNTHESIS
             // Print out state changes for testbench purposes
             swd_state_change<=swd_state;
             if (swd_state_change!=swd_state)
               begin
                  $display("");
                  $write("ST_");
                  case (swd_state)
                    ST_IDLE:         $write("IDLE");
                    ST_HDR_TX:       $write("HDR_TX");
                    ST_TRN1:         $write("TRN1");
                    ST_ACK:          $write("ACK");
                    ST_TRN2:         $write("TRN2");
                    ST_DWRITE:       $write("DWRITE");
                    ST_DWRITEPARITY: $write("DWRITEPARITY");
                    ST_DREAD:        $write("DREAD");
                    ST_DREADPARITY:  $write("DREADPARITY");
                    ST_COOLING:      $write("COOLING");
                    default:         $write("UNKNOWN!!");
                  endcase // case (swd_state)
                  $write(":");
               end // if (swd_state_change!=swd_state)
`endif

             // Run clock when we are not in idle state
             if (swd_state!=ST_IDLE)
               if (cdivcount) cdivcount <= cdivcount-1;
               else
                 begin
                    cdivcount <= clkDiv;
                    swclk <= ~swclk;
                 end
             
             case (swd_state)
               ST_IDLE: // Idle waiting for something to happen ===============================
                 begin
                    swwr <= 1;      // While idle we're in output mode
                    cdivcount<=0;   // ...and we're ready to start clocking
                    swclk<=1'b1;    // ...with a -ve edge
                    
                    bits <= { 1'b1, 1'b0, apndp^rnw^addr32[1]^addr32[0], addr32[1], addr32[0], apndp, 1'b1 };
                    
                    if (go_cdc==2'b11)
                      begin
                         // Request to start a transfer
                         bitcount  <= 6'h7;      // 8 bits, first one is leaving now
                         swd_state <= ST_HDR_TX; // Send the packet header
                         done      <= 0;         // ...and acking the transfer
                         err       <= 0;         // Clear any previous error                         
                      end
                 end // case: ST_IDLE

               ST_HDR_TX: // Sending the packet header ========================================
                 begin
                    if (posclkedge)
                      begin
                         bits<={1'b1,bits[31:1]};
                         bitcount<=bitcount-1;
                         if (!bitcount)
                           begin
                              // Turn direction at the rising edge
                              swd_state<=ST_TRN1;
                              swwr<=1'b0;
                           end
                      end // if (posclkedge)
                 end // case: ST_HDR_TX

               ST_TRN1: // One bit turnaround time ==========================================
                 if (posclkedge)
                   begin
                      swd_state<=ST_ACK;
                      bitcount <= 3;
                   end
               
               ST_ACK: // Collect the ack bits ============================================
                 begin
                    par <= 0;
                    if (negclkedge)
                      begin
                         ack_in<={ack_in[1:0],swdi};
                         bitcount<=bitcount-1;
                      end
                    
                    if (posclkedge && (!bitcount))
                      begin
                         // Check what kind of ack we got
                         if (ack_in==3'b100)
                           begin
                              bitcount<=32;
                              // Was good, so set up for transfer
                              swd_state=rnw?ST_DREAD:ST_TRN2;
                              swwr<=~rnw;          // ... and make output if needed
                           end
                         else
                           begin
                              // Wasn't good, give up and return idle, via cooloff
                              
                              bitcount<=32;
                              swd_state<=ST_COOLING;
                           end // else: !if(ack_in==3'b100)
                        end // if (!bitcount)
                 end // case: ST_ACK
               
               ST_TRN2: // One bit turnaround time =====================================
                 begin
                    bitcount <= 32;
                    if (posclkedge)
                      swd_state<=ST_DWRITE;
                 end
               
               ST_DREAD: // Reading 32 bit value ======================================
                 begin
                    if (negclkedge)
                      begin
                         bits<={swdi,bits[31:1]};
                         par<=par^swdi;
                         bitcount<=bitcount-1;
                      end

                    if ((posclkedge) & (!bitcount))
                      begin
                         swd_state <= ST_DREADPARITY;
                      end
                 end

               ST_DWRITE: // Writing 32 bit value ======================================
                 if (posclkedge)
                   begin
                      bits<={1'b1,bits[31:1]};
                      par<=par^bits[0];
                      bitcount<=bitcount-1;
                      
                      if (!bitcount)
                        swd_state <= ST_DWRITEPARITY;
                   end
               
               ST_DREADPARITY: // Reading parity ======================================
                 if (negclkedge)
                   begin
                      err<=par^swdi;
                      done <= 1'b1;
                      ack <= ack_in;
                      dout<=bits;                      
                      swd_state <= ST_IDLE;
                   end

               ST_DWRITEPARITY: // Writing parity ======================================
                 begin
                    if (posclkedge)
                      bits[0]<=par; // This will end up on swdo

                    if (negclkedge)
                      begin
                         // If we have another write pending then go fetch it after turnaround, otherwise cool the link
                         bitcount <= (go_cdc==2'b11)?1:8;
                         swwr<=0;
                         swd_state <= ST_COOLING;
                      end
                 end // case: ST_DWRITEPARITY

               ST_COOLING: // Cooling the link before shutting it =====================
                 begin
                    if (posclkedge)
                      begin
                         bitcount<=bitcount-1;
                         if (!bitcount)
                           begin
                              done<=1'b1;
                              ack<=ack_in;
                              dout<=bits;
                              swd_state <= ST_IDLE;
                           end
                      end
                 end
             endcase // case (swd_state)
          end // else: !if(rst)
     end // always @ (posedge clk, posedge rst)
endmodule // swdIF


             

                         
