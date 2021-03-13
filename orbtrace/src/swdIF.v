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
                output reg        swdo,     // DIO pin to target
                input             swclk,    // Clock out to target (from superior)
                output reg        swwr,     // Direction of DIO pin

        // Configuration
                input [1:0]       turnaround, // Clock ticks per turnaround
                input             dataphase,  // Does a dataphase follow WAIT/FAULT?
                
	// Upwards interface to command controller
                input [1:0]       addr32,   // Address bits 3:2 for message
                input             rnw,      // Set for read, clear for write
                input             apndp,    // AP(1) or DP(0) access?
                input [31:0]      dwrite,   // Most recent data from us
                output reg [2:0]  ack,      // Most recent ack status
                output reg [31:0] dread,    // Data read from target
                output reg        perr,     // Indicator of a parity error in the transfer

output reg c,
                input             go,       // Trigger
                output            idle      // Response
		);		  
   
   // Internals =======================================================================
   reg [3:0]                      swd_state;   // current state of machine
   reg [2:0]                      ack_in;      // ack coming in from target
   reg [5:0]                      bitcount;    // Counter for bits being transferred
   reg [31:0]                     bits;        // The bits being transferred
   reg                            par;         // Parity construct
   reg                            swclk_trak;  // Clock state tracker

   wire risingedge = ((!swclk_trak) && (swclk));
   wire fallingedge = ((swclk_trak) && (!swclk));   
   
`ifdef STATETRACE
   reg [3:0]                      swd_state_change;
`endif

   parameter ST_IDLE         = 0;
   parameter ST_HDR_TX       = 1;
   parameter ST_TRN1         = 2;
   parameter ST_ACK          = 3;
   parameter ST_TRN2         = 4;
   parameter ST_DWRITE       = 5;
   parameter ST_DREAD        = 6;
   parameter ST_DREADPARITY  = 7;   
   parameter ST_COOLING      = 8;

   assign idle = (swd_state==ST_IDLE);     // Definition for idleness
             
   always @(posedge clk, posedge rst)
     begin
        // Default status bits
	if (rst)
	  begin
             swd_state <= ST_IDLE;
             swwr      <= 0;
             perr      <= 0;
	  end
	else
	  begin
`ifdef STATETRACE
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

             swclk_trak <= swclk;
             
             case (swd_state)
               ST_IDLE: // Idle waiting for something to happen ===============================
                 begin
                    swwr    <= 1'b1;      // While idle we're in output mode
                    
                    // Things are about to kick off, put the leading 1 on the line
                    if ((go) && (fallingedge))
                      swdo <= 1'b1;

                    // ...and once we've got the 1 we can set off
                    if ((go) && (risingedge) && (swdo==1'b1))
                      begin
                         // Request to start a transfer (Note the leading '1' is already on the line)
                         bits <= { 1'b1, 1'b0, apndp^rnw^addr32[1]^addr32[0], addr32[1], addr32[0], rnw, apndp };
                         bitcount  <= 6'h7;      // 8 bits, 1 being sent already now
                         swd_state <= ST_HDR_TX; // Send the packet header
                         perr      <= 1'b0;      // Clear any previous error   
                         par       <= 1'b0;      // and clear accumulated parity
                      end
                 end // case: ST_IDLE

               ST_HDR_TX: // Sending the packet header ========================================
                 begin
                    if (fallingedge)
                      begin
                         bits     <= {1'b1,bits[31:1]};
                         swdo     <= bits[0];
                         bitcount <= bitcount-1;
                      end

                    if (!bitcount)
                      begin
                         bitcount  <= turnaround+1;
                         swd_state <= ST_TRN1;
                      end
                 end // case: ST_HDR_TX

               ST_TRN1: // Performing turnaround =============================================
                 begin
                    if (fallingedge)
                      begin
                         bitcount <= bitcount - 1;
                      end
               
                      if (!bitcount)
                        begin
                           swwr      <= 1'b0;                         
                           bitcount  <= 3;
                           swd_state <= ST_ACK;
                        end
                 end // case: ST_TRN1

               ST_ACK: // Collect the ack bits ===============================================
                 begin
                    if (fallingedge)
                      begin
                         ack_in   <= {swdi,ack_in[2:1]};
                         bitcount <= bitcount-1;
                      end

                    if (!bitcount)
                      begin
                         // Check what kind of ack we got
                         if (ack_in==3'b001)
                           begin
                              if (rnw)
                                begin
                                   bitcount<=32;
                                   swd_state <= ST_DREAD;
                                end
                              else
                                begin
                                   bitcount <= turnaround + 1;
                                   swd_state <= ST_TRN2;
                                end
                           end
                         else
                           begin
                              // Wasn't good, give up and return idle, via cooloff
                              swwr      <= 1'b1;
                              bitcount  <= dataphase?33:1;
                              swd_state <= ST_COOLING;
                           end // else: !if(ack_in==3'b001)
                      end // if ((fallingedge) && (!bitcount))
                 end // case: ST_ACK
               
               ST_TRN2: // One and a half turnaround time =====================================
                 begin
                    if (fallingedge)
                      begin
                         swwr     <= 1'b1;
                         bitcount <= bitcount - 1;
                      end
               
                      if (!bitcount)
                        begin
                           bitcount  <= 32;
                           bits      <= dwrite;
                           swd_state <= ST_DWRITE;
                        end
                 end // case: ST_TRN2
               
               ST_DREAD: // Reading 32 bit value ======================================
                 begin
                    if (fallingedge)
                      begin
                         bits     <= {swdi,bits[31:1]};
                         par      <= par^swdi;
                         bitcount <= bitcount-1;
                      end

                    if (!bitcount)
                      swd_state <= ST_DREADPARITY;
                 end

               ST_DWRITE: // Writing 32 bit value ======================================
                 begin
                    if (fallingedge)
                      begin
                         bits     <= {1'b0,bits[31:1]};
                         swdo     <= bits[0];
                         par      <= par^bits[0];
                         bitcount <= bitcount-1;
                         if (!bitcount)
                           begin
                              // This is the cycle after full word transmission
                              swdo <= par;
                              bitcount  <= go?1:8;
                              swd_state <= ST_COOLING;
                           end
                      end
                 end
               
               ST_DREADPARITY: // Reading parity ======================================
                 if (fallingedge)
                   begin
                      // Need to turn the link so it's ready for next tx
                      perr      <= par^swdi;
                      bitcount  <= 2;
                      swd_state <= ST_COOLING;
                      swwr      <= 1'b1;
                      swdo      <= 1'b0;
                   end

               ST_COOLING: // Cooling the link before shutting it =====================
                 begin
                    if (fallingedge)
                      begin
                         ack       <= ack_in;
                         dread     <= bits;
                         swdo      <= 1'b0;
                         swwr      <= 1'b1;
                         bitcount<=bitcount-1;
                      end
                    if (!bitcount)
                      swd_state <= ST_IDLE;
                 end
             endcase // case (swd_state)
          end // else: !if(rst)
     end // always @ (posedge clk, posedge rst)
endmodule // swdIF
