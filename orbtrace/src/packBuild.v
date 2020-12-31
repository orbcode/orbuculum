`default_nettype none

// packBuild
// =========
//
module packBuild (
		input            clk,              // System Clock
		input            rst,              // Clock synchronised reset
		
	        // Downwards interface to packet processor
		input            PkAvail,          // Flag indicating word is available
		input [127:0]    Packet,           // The next packet word

		// Upwards interface to serial (or other) handler : clk Clock
		output reg [7:0] DataVal,          // Output data value

		input            DataNext,         // Request for next data element
		output reg       DataReady,        // Indicator that the next data element is available

                // Indicator that buffer overflowed
		output reg       DataOverf         // Too much data in buffer
 		);

   // Internals ==============================================================================
   parameter BUFFLENLOG2=9;

   reg [BUFFLENLOG2-1:0]	 Rp;               // Read Element position in buffer
   reg [BUFFLENLOG2-1:0]	 Wp;               // Write Element position in buffer
   reg [BUFFLENLOG2-1:0]	 nextWp;           // Next Write Element position in buffer

   wire [127:0]                  ramData;          // The data from the RAM
   reg [127:0]                   currentPacket;    // The packet being transmitted

   reg [4:0]                     count;            // Which element we are outputting from it
   reg [6:0]                     senderState;      // Onehot state encoding for uploader state machine

   reg [2:0]                     pkavail_cdc;      // cdc/edge detecting version of packet finder
   wire                          packetWrite;      // Flag to write packet to memory
   

   /* Memory for storage of samples, one packet wide */
   ram #(.addr_width(BUFFLENLOG2),.data_width(128))
   mem (
            .din(Packet),
            .write_en(packetWrite),
            .waddr(Wp),
            .wclk(clk),

            .raddr(Rp),
            .rclk(clk),
            .dout(ramData)
        );

   assign packetWrite = ((pkavail_cdc==3'b011) || (pkavail_cdc==3'b100));

   /* States for uploader state machine */
   parameter
     ST_IDLE           =0,
     ST_SEND_SYN       =1,
     ST_SEND_DAT       =2,
     ST_LOAD_TX_FRAME  =3,
     ST_SENDING_FRAME  =4;

   // ======= Write data to RAM using source domain clock =================================================

   always @(posedge clk,posedge rst)
     begin
        DataOverf<=0;           // Each iteration, default is no overflow
        DataReady<=1'b0;        // ...and no data waiting to send

	if (rst)  // Perform reset actions ================================================================
	  begin
	     Wp<=0;
             nextWp<=1;
             senderState<=(1<<ST_IDLE);
             Rp<=0;
	  end
	else
	  begin // Perform new packet received actions ====================================================
             pkavail_cdc<={pkavail_cdc[1:0],PkAvail};

             if (packetWrite)
               begin
                  // We have a new packet, we will store it and check overflow and ack the packet regardless
                  $display("New packet: %32x",Packet);
                  if (nextWp==Rp)
                    begin
                     //  Rp<=Rp+1;
                       DataOverf<=1'b1;
                       $display("Overflowed");
                    end
                  Wp<=nextWp;
                  nextWp<=nextWp+1;
               end

             // ======== Send data to uart if relevant ============

             case (senderState)
               (1<<ST_IDLE): /* Waiting for some data to become available ====================== */
                 begin
                    count<=5'h04;
                    if (Rp!=Wp)
                      begin
                         senderState<=(1<<ST_SEND_SYN);
                      end
                 end

               (1<<ST_SEND_SYN): /* Sending components of sync ================================= */
                 begin
                    if (DataNext)
                      begin
                         if (count==0)
                           senderState<=(1<<ST_LOAD_TX_FRAME);
                         else
                           begin
                              count<=count-1;
                              DataVal<=(count==1)?8'h7f:8'hff;
                              DataReady<=1'b1;
                           end
                      end // if (DataNext)
                 end // case: (1<<ST_SEND_SYN)

               (1<<ST_LOAD_TX_FRAME): /* Loading frame to be sent ============================== */
                 begin
                    count<=5'h10;
                    currentPacket<=ramData;
                    senderState<=(1<<ST_SENDING_FRAME);
                 end

               (1<<ST_SENDING_FRAME): /* Outputting the frame ================================== */
                 begin
                    if (count==0)
                      begin
                         Rp<=Rp+1;
                         senderState<=(1<<ST_IDLE);
                      end
                    else
                      begin
                         if (DataNext)
                           begin
                              DataReady<=1'b1;
                              DataVal<=currentPacket[7:0];
                              count<=count-1;
                              currentPacket<={8'h0,currentPacket[127:8]};
                           end // if (DataNext)
                      end // case: senderState[ST_SENDING_PACKET]
                 end // case: (1<<ST_SENDING_FRAME)
             endcase // case (senderState)
          end // else: !if(rst)
     end // always @ (clk,posedge rst)
endmodule // packBuild
