`default_nettype none

// packBuffer
// ==========
//
module packBuffer (
		input            clk,           // System Clock
		input            rst,           // Clock synchronised reset
		
	        // Downwards interface to packet processor
		input            PkAvail,       // Flag indicating word is available
		input [127:0]    Packet,        // The next packet word
                  
		// Upwards interface to output handler : clk Clock
		output [127:0]   Frame,         // Output frame

		input            FrameNext,     // Request for next data element
		output reg       FrameReady,    // Next data element is available

                // Indicator that buffer overflowed
		output           DataOverf,     // Too much data in buffer
                output           DataInd        // Indicator of data status
 		);

   // Internals =========================================================================
   parameter BUFFLENLOG2=9;

   reg [BUFFLENLOG2-1:0]	 Rp;               // Read Element position in buffer
   reg [BUFFLENLOG2-1:0]	 Wp;               // Write Element position in buffer
   reg [BUFFLENLOG2-1:0]	 nextWp;           // Next Write Element position

   reg [25:0]                    dataIndStretch;   // Data indication stretch
   
   reg [2:0]                     pkavail_cdc;      // cdc/edge for packet finder

   /* Memory for storage of samples, one packet wide */
   ram #(.addr_width(BUFFLENLOG2),.data_width(128))
   mem (
            .din(Packet),
            .write_en(packetWrite),
            .waddr(Wp),
            .wclk(clk),

            .raddr(Rp),
            .rclk(clk),
            .dout(Frame)
        );

   assign DataOverf   = (Rp == nextWp);
   assign DataInd     = (dataIndStretch != 0);
   wire   packetWrite = ((pkavail_cdc==3'b011) || (pkavail_cdc==3'b100));
   
   
   always @(posedge clk,posedge rst)
     begin
	if (rst)  // Perform reset actions ==============================================
	  begin
	     Wp         <= 0;
             Rp         <= 0;
             nextWp     <= 1;
             FrameReady <= 0;
	  end
	else
	  begin // Perform new packet received actions ==================================

             // Perform cdc for the packet available flag
             pkavail_cdc<={pkavail_cdc[1:0],PkAvail};
             
             // Check for sync
             if (dataIndStretch!=0) dataIndStretch<=dataIndStretch-1;

             FrameReady <= ((Rp!=Wp) && (!FrameNext));
             
             // Store packet if we can
             if (packetWrite)
               begin
                  // We have a new packet, we will store it and check overflow
                  $display("New packet: %32x (Rp=%d, Wp=%d)",Packet,Rp,Wp);
                  if (nextWp==Rp)
                    begin
                       Rp<=Rp+1;
                       $display("Overflowed");
                    end
                  dataIndStretch<=~0;
                  Wp<=nextWp;
                  nextWp<=nextWp+1;
               end

             // Send packet to output side ==============================================
             if ((FrameNext) && (Rp!=Wp))
               begin
                  $display("Incrementing read, Rp=%d,Wp=%d",Rp,Wp);
                  Rp<=Rp+1;
               end
          end // else: !if(rst)
     end // always @ (posedge clk,posedge rst)   
endmodule // packBuffer
