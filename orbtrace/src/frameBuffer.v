`default_nettype none

// frameBuffer
// ===========
//
// Take a single frame from the receiver and put it into fifo memory. Release it on
// demand to the handler as a complete frame.  Also reports overflow and data indication.
//
// Action on overflow is to roll forward the Read Pointer so the latest data are always in
// the fifo. This means it can be used as a post-mortem buffer.
//

module frameBuffer (
		input                        clk,
		input                        rst,
		
	        // Downwards interface to frame collector
		input                        FrAvail,     // Flag indicating frame is available
		input [127:0]                FrameIn,     // The next frame
                  
		// Upwards interface to output handler
		output [127:0]               FrameOut,    // Output frame
		input                        FrameNext,   // Request for next data element
                output reg [BUFFLENLOG2-1:0] FramesCnt,   // No of frames available

                // Indicator that buffer overflowed
                output                       DataOverf,   // Too much data in buffer
                output                       DataInd,     // Indicator of data status

                output reg [31:0]            TotalFrames, // Number of frames received
                output reg [15:0]            LostFrames   // Number of frames lost
 		);

   // Internals =========================================================================
   parameter BUFFLENLOG2=9;

   reg [BUFFLENLOG2-1:0]	 Rp;               // Read Element position in buffer
   reg [BUFFLENLOG2-1:0]	 Wp;               // Write Element position in buffer
   reg [BUFFLENLOG2-1:0]	 nextWp;           // Next Write Element position
   reg [BUFFLENLOG2-1:0]	 prevRp;           // Previous Read Element position

   reg [BUFFLENLOG2:0]           FramesCnt_t;      // No of frames available (pre-clock)
                                                   // Note the extra bit is intentional!

   reg [25:0]                    dataIndStretch;   // Data indication stretch
   
   reg [2:0]                     fravail_cdc;      // cdc/edge for frame finder

   /* Memory for storage of samples, one frame wide */
   ram #(.addr_width(BUFFLENLOG2),.data_width(128))
   mem (
            .din(FrameIn),
            .write_en(frameWrite),
            .waddr(Wp),
            .wclk(clk),

            .raddr(prevRp),
            .rclk(clk),
            .dout(FrameOut)
        );

   assign DataOverf   = (Rp == nextWp);
   assign DataInd     = (dataIndStretch != 0);
   wire   frameWrite = ((fravail_cdc==3'b011) || (fravail_cdc==3'b100));
   
   always @(posedge clk,posedge rst)
     begin
	if (rst)  // Perform reset actions ==============================================
	  begin
             FramesCnt   <= 0;
             FramesCnt_t <= 0;
	     Wp          <= 0;
             Rp          <= 0;
             prevRp      <= 0;
             nextWp      <= 1;
             TotalFrames <= 0;
             LostFrames  <= 0;
	  end
	else
	  begin // Perform new frame received actions ==================================

             // Perform cdc for the frame available flag
             fravail_cdc<={fravail_cdc[1:0],FrAvail};

             // Delay frames count by one cycle to allow for RAM propagation
             FramesCnt <= FramesCnt_t;

             // ... and calculate how many we've got in store (accomodate wrap-around)
             FramesCnt_t <= {1'b1,Wp}-Rp;
             
             // Check for sync
             if (dataIndStretch!=0) dataIndStretch<=dataIndStretch-1;

             // Store frame if we can
             if (frameWrite)
               begin
                  // We have a new frame, we will store it no matter what
                  $display("New frame: %32x (Rp=%d, Wp=%d)",FrameIn,Rp,Wp);
                  TotalFrames <= TotalFrames + 1;
                  dataIndStretch<=~0;
                  if (nextWp==Rp)
                    begin
                       LostFrames<=LostFrames+1;
                    end
                  else
                    begin
                       Wp<=nextWp;
                       nextWp<=nextWp+1;
                    end
               end

             // Perform read if there is one pending, or if we'd overflow otherwise
             if (((FrameNext) && (FramesCnt_t != 0)) || ((frameWrite && (nextWp==Rp))))
               begin
                  $display("Incrementing read, Rp=%d,Wp=%d",Rp,Wp);
                  prevRp <= Rp;
                  Rp <= Rp + 1;
               end
          end // else: !if(rst)
        
     end // always @ (posedge clk,posedge rst)   
endmodule // frameBuffer
