`default_nettype none

// packToSerial
// ============
//
module packToSerial (
		input            clk,
		input            rst,
		
        // Downwards interface to packet buffer
		input [127:0]    Frame,            // Input frame
		output reg       FrameNext,        // Request for next data element
		input            FrameReady,       // Next data element is available
                     
	// Upwards interface to serial (or other) handler : clk Clock
		output reg [7:0] DataVal,          // Output data value
		input            DataNext,         // Request for next data element
		output reg       DataReady         // Next data element is available
 		);

   parameter SYNC_INTERVAL=15;
   
   // Internals =========================================================================

   reg [127:0]                   currentPacket;    // The packet being transmitted

   reg [4:0]                     count;            // Which element we are outputting?
   reg [2:0]                     senderState;      // Onehot state encoding for uploader
   reg [7:0]                     syncInterval;     // Interval between sync packets

   /* States for uploader state machine */
   parameter
     ST_IDLE           =0,
     ST_SEND_SYN       =1,
     ST_SENDING_FRAME  =2;

   always @(posedge clk,posedge rst)
     begin
        DataReady      <= 1'b0;        // No data waiting to send
        FrameNext      <= 1'b0;        // Not currently requesting a new frame

	if (rst)  // Perform reset actions ==============================================
	  begin
             senderState  <= (1<<ST_IDLE);
             syncInterval <= 0;
	  end
	else
	  begin
             // Send data to uart if possible ==========================================
             case (senderState)
               (1<<ST_IDLE): // -- Waiting for some data to become available --
                 begin
                    if (FrameReady)
                      begin
                         FrameNext<=1'b1;
                         currentPacket<=Frame;
                         if (syncInterval==0)
                           begin
                              count<=5'h04;
                              syncInterval <= SYNC_INTERVAL;
                              senderState<=(1<<ST_SEND_SYN);
                           end
                         else
                           begin
                              count<=5'h10;
                              syncInterval <= syncInterval - 1;
                              senderState<=(1<<ST_SENDING_FRAME);
                           end
                      end
                 end

               (1<<ST_SEND_SYN): // -- Sending components of sync --
                 begin
                    if (DataNext)
                      begin
                         if (count==0)
                           begin
                              count<=5'h10;
                              senderState<=(1<<ST_SENDING_FRAME);
                           end
                         else
                           begin
                              count<=count-1;
                              DataVal<=(count==1)?8'h7f:8'hff;
                              DataReady<=1'b1;
                           end
                      end // if (DataNext)
                 end // case: (1<<ST_SEND_SYN)

               (1<<ST_SENDING_FRAME): // -- Outputting the frame --
                 begin
                    if (count==0)
                      begin
                         senderState <= (1<<ST_IDLE);
                      end
                    else
                      begin
                         if (DataNext)
                           begin
                              DataReady     <= 1'b1;
                              DataVal       <= currentPacket[7:0];
                              count         <= count-1;
                              currentPacket <= {8'h0,currentPacket[127:8]};
                           end
                      end // case: senderState[ST_SENDING_PACKET]
                 end // case: (1<<ST_SENDING_FRAME)
             endcase // case (senderState)
          end // else: !if(rst)
     end // always @ (clk,posedge rst)
endmodule // packBuild
