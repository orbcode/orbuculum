`default_nettype none

// dbgIF
// =====
// 
// Working from ARM Debug Interface Architecture Specification ADIv5.0 to ADIv5.2
//
// Debug interface covering JTAG, SWJ and SWD use cases.
// Also deals with power pins.
//
// This gateware is under BSD licence.
//
// Commands;
//  CMD_RESET       : Reset target, return after timeout or when target is out of reset.
//  CMD_PINS_WRITE  : Go to SWJ mode and write pins specified in dwrite[7:0], masked
//                    by dwrite[15:8], wait for period of uS specified in countdown and
//                    then return pins in dread[7:0].
//  CMD_TRANSACT    : Execute command transaction on target interface.
//  CMD_SET_SWD     : Set interface to SWD mode.
//  CMD_SET_JTAG    : Set interface to JTAG mode.
//  CMD_SET_SWJ     : Set interface to SWJ mode.
//  CMD_SET_PWRDOWN : Set interface to power down.
//  CMD_SET_CLKDIV  : Set clock divisor for interface actions
//  CMD_SET_TURN    : Set turnaround clock ticks for SWD mode
//  CMD_WAIT        : Wait for prescribed number of uS
//  CMD_CLR_ERR     : Clear error status
//  CMD_SET_RST_TO  : Set reset time in uS
//
module dbgIF #(parameter TICKS_PER_USEC=50, parameter DEFAULT_RST_TIMEOUT_USEC=300) (
		input             rst,
                input             clk,
                
        // Gross control, power etc.
                input             vsen,            // When 1, provide power to VS (pins 11 & 13 on 20 pin header)
                input             vdrive,          // When 1, provide power to Vdrive (pin 1 on 10 & 20 pin header)

	// Downwards interface to the pins
                input             swdi,            // DIO pin from target
                output            tms_swdo,        // TMS or DIO pin to target when in SWD mode
                output            swwr,            // Direction of DIO pin when in SWD mode
                output            tck_swclk,       // Clock pin to target
                output            tdi,             // TDI pin to target
                input             tdo_swo,         // TDO/SWO pin from target
                input             tgt_reset_state, // Current state of tgt_reset 

                output            tgt_reset_pin,   // Output pin to pull target reset low
                output            nvsen_pin,       // Output pin to control vsen
                output            nvdrive_pin,     // Output pin to control vdrive

	// Interface to command controller
                input [1:0]       addr32,          // Address bits 3:2 for message
                input             rnw,             // Set for read, clear for write
                input             apndp,           // AP(1) or DP(0) access?
                output [2:0]      ack,             // Most recent ack status
                input  [39:0]     dwrite,          // Most recent data or parameter to write
                output [31:0]     dread,           // Data read from target

        // Event triggers and responses
                input [3:0]       command,         // Command to be performed 
                input             go,              // Trigger
                output            done,            // Response
                output reg        perr             // Indicator of a error in the transfer
	      );

   // Control commands
   parameter CMD_RESET       = 0;
   parameter CMD_PINS_WRITE  = 1;
   parameter CMD_TRANSACT    = 2;
   parameter CMD_SET_SWD     = 3;
   parameter CMD_SET_JTAG    = 4;
   parameter CMD_SET_SWJ     = 5;
   parameter CMD_SET_PWRDOWN = 6;
   parameter CMD_SET_CLKDIV  = 7;
   parameter CMD_SET_TURN    = 8;
   parameter CMD_WAIT        = 9;
   parameter CMD_CLR_ERR     = 10;
   parameter CMD_SET_RST_TO  = 11;

   // Comms modes
   parameter MODE_PWRDOWN = 0;
   parameter MODE_SWJ     = 1;
   parameter MODE_SWD     = 2;
   parameter MODE_JTAG    = 3;
                      
   // Internals =======================================================================
   reg [10:0]                     cdivcount;       // divisor for external clock
   reg [10:0]                     usecsdiv;        // Divider for usec
   reg [31:0]                     usecs;           // usec continuous counter
   reg [22:0]                     usecsdown;       // usec downcounter
   
   reg                            tgt_clk;         // Ticks on external clock
   reg [1:0]                      cdc_go;          // Clock domain crossed go
   reg                            if_go;           // Go to inferior
      
   // Pins driven by swd (MODE_SWD)
   wire                           swd_swdo;
   wire                           swd_swclk;
   wire                           swd_swwr;
   wire [2:0]                     swd_ack;
   wire [31:0]                    swd_dread;
   wire                           swd_perr;
   wire                           swd_idle;

   // Pins driven by pin_write (MODE_SWJ)
   reg                           pinw_swclk;
   reg                           pinw_swdo;
   reg                           pinw_ntrst;
   reg [7:0]                     pinw_dread;

   assign nvsen_pin     = ~vsen;
   assign nvdrive_pin   = ~vdrive;
   
   // Mux submodule outputs to this module outputs
   assign tms_swdo  = (active_mode==MODE_SWD)?swd_swdo  :(active_mode==MODE_SWJ)?pinw_swdo:1'b1;
   assign swwr      = (active_mode==MODE_SWD)?swd_swwr  :(active_mode==MODE_SWJ)?1'b1:1'b1;
   assign tck_swclk = (active_mode==MODE_SWD)?swd_swclk :(active_mode==MODE_SWJ)?pinw_swclk:1'b1;
   assign tdi       = (active_mode==MODE_SWD)?1'b1      :1'b1;
   assign ack       = (active_mode==MODE_SWD)?swd_ack   :0;
   assign dread     = (active_mode==MODE_SWD)?swd_dread :(active_mode==MODE_SWJ)?{24'd0,pinw_dread}:0;

   assign done = (dbg_state==ST_DBG_IDLE);

   swdIF swd_instance (
	      .rst(rst),
              .clk(tgt_clk),
              .swdi(swdi),
              .swdo(swd_swdo),
              .swclk(swd_swclk),
              .swwr(swd_swwr),
              .turnaround(turnaround),
                
              .addr32(addr32),
              .rnw(rnw),
              .apndp(apndp),
              .dwrite(dwrite[31:0]),
              .ack(swd_ack),
              .dread(swd_dread),
              .perr(swd_perr),
              
              .go(if_go && (active_mode==MODE_SWD)),
              .idle(swd_idle)
	      );

   reg [10:0]                     clkDiv;          // Divisor per clock change to target
   reg [3:0]                      turnaround;      // Number of cycles for turnaround when in SWD mode
   reg [22:0]                     rst_timeout;     // Default time for a reset
   
   reg [1:0]                      active_mode;     // Mode that the interface is actually in
   reg [1:0]                      commanded_mode;  // Mode that the interface is requested to be in
   reg [2:0]                      dbg_state;       // Current state of debug handler

   parameter ST_DBG_IDLE                 = 0;
   parameter ST_DBG_RESETTING            = 1;
   parameter ST_DBG_RESET_GUARD          = 2;
   parameter ST_DBG_PINWRITE_WAIT        = 3;
   parameter ST_DBG_WAIT_INFERIOR_START  = 4;
   parameter ST_DBG_WAIT_INFERIOR_FINISH = 5;
   parameter ST_DBG_WAIT_GOCLEAR         = 6;
   parameter ST_DBG_WAIT_TIMEOUT         = 7;
   

   // Active low reset on target
   assign tgt_reset_pin = (active_mode==MODE_SWJ)?pinw_ntrst:(dbg_state!=ST_DBG_RESETTING);
   
   always @(posedge clk, posedge rst)

     begin
	if (rst)
	  begin
             cdc_go<=0;
             tgt_clk<=0;
             cdivcount<=1;
             clkDiv<= TICKS_PER_USEC;
             rst_timeout<=DEFAULT_RST_TIMEOUT_USEC;
             dbg_state<=ST_DBG_IDLE;
             perr<=0;
             turnaround <= 4'd1;
             if_go <= 1'b0;  // We aren't normally asking an inferior to do anything
             active_mode <= MODE_PWRDOWN;
             commanded_mode <= MODE_PWRDOWN;
	  end
	else
          begin
             // CDC the go signal
             cdc_go <= {cdc_go[0],go};
             
             // Run clock for sub-modules constantly
             if (cdivcount)
               cdivcount <= cdivcount-1;
             else
               begin
                  tgt_clk <= ~tgt_clk;
                  cdivcount <= clkDiv;
               end

             if (usecsdiv)
               usecsdiv <= usecsdiv - 1;
             else
               begin
                  usecs <= usecs + 1;
                  usecsdiv <= TICKS_PER_USEC;
                  usecsdown <= usecsdown - 1;
               end

             
             case(dbg_state)
               ST_DBG_IDLE: // Command request ========================================================
                 if (cdc_go==2'b11)
                   begin
                      perr<=0;
                      case(command)
                        CMD_PINS_WRITE: // Write pins specified in call -----------
                          begin
                             active_mode<=MODE_SWJ;
                             usecsdown<=dwrite[39:16];
                             // Update these bits if they're requested for updating
                             pinw_swclk<=dwrite[8] ?dwrite[0]:tck_swclk;
                             pinw_swdo<= dwrite[9] ?dwrite[1]:tms_swdo;
                             pinw_ntrst<=dwrite[15]?dwrite[7]:tgt_reset_pin;
                             
                             dbg_state<=ST_DBG_PINWRITE_WAIT;
                          end // case: CMD_PINS_WRITE

                        CMD_RESET: // Reset target ---------------------------------
                          begin
                             usecsdown<=rst_timeout;
                             dbg_state<=ST_DBG_RESETTING;
                          end

                        CMD_TRANSACT: // Execute transaction on target interface ---
                          begin
                             if_go<=1'b1;
                             dbg_state<=ST_DBG_WAIT_INFERIOR_START;
                          end
                        
                        CMD_SET_SWD: // Set SWD mode -------------------------------
                          begin
                             commanded_mode <= MODE_SWD;
                             active_mode <= MODE_SWD;
                             dbg_state<=ST_DBG_WAIT_GOCLEAR;
                          end
                          
                        CMD_SET_JTAG: // Set JTAG mode -----------------------------
                          begin
                             commanded_mode <= MODE_JTAG;
                             active_mode <= MODE_JTAG;
                             dbg_state<=ST_DBG_WAIT_GOCLEAR;
                          end
                        
                        CMD_SET_SWJ: // Set SWJ mode -------------------------------
                          begin
                             commanded_mode <= MODE_SWJ;
                             active_mode <= MODE_SWJ;
                             dbg_state<=ST_DBG_WAIT_GOCLEAR;
                          end

                        CMD_SET_PWRDOWN: // Set Power Down mode --------------------
                          begin
                             commanded_mode <= MODE_PWRDOWN;
                             active_mode <= MODE_PWRDOWN;
                             dbg_state<=ST_DBG_WAIT_GOCLEAR;
                          end

                        CMD_SET_CLKDIV: // Set clock divisor -----------------------
                          begin
                             clkDiv<=dwrite;
                             dbg_state <= ST_DBG_WAIT_GOCLEAR;
                          end

                        CMD_SET_TURN: // Set turnaround count ----------------------
                          begin
                             turnaround<=dwrite;
                             dbg_state <= ST_DBG_WAIT_GOCLEAR;
                          end

                        CMD_WAIT: // Wait for specified number of uS ---------------
                          begin
                             usecsdown<=dwrite;
                             dbg_state <= ST_DBG_WAIT_TIMEOUT;
                          end

                        CMD_CLR_ERR: // Clear error status -------------------------
                          begin
                             dbg_state <= ST_DBG_WAIT_GOCLEAR;
                          end

                        default: // Unknown, set an error --------------------------
                          begin
                             perr<=1;
                             dbg_state <= ST_DBG_WAIT_GOCLEAR;
                          end
                      endcase // case (command)
                   end // if (cdc_go==2'b11)

               ST_DBG_WAIT_GOCLEAR: // Waiting for go indication to clear =================================
                 if (cdc_go!=2'b11)
                   dbg_state <= ST_DBG_IDLE;
                   
               ST_DBG_WAIT_INFERIOR_FINISH: // Waiting for inferior to complete its task ===================
                 case (active_mode)
                   MODE_SWD:
                     if (swd_idle)
                       begin
                          dbg_state <= ST_DBG_IDLE;
                          perr <= swd_perr;
                       end
                   default:
                     begin
                        perr<=1'b1;
                        dbg_state<=ST_DBG_WAIT_GOCLEAR;
                     end
                 endcase

               ST_DBG_WAIT_INFERIOR_START: // Waiting for inferior to start its task =======================
                 case (active_mode)
                   MODE_SWD:
                     if (~swd_idle)
                       begin
                          if_go <= 0;
                          dbg_state <= ST_DBG_WAIT_INFERIOR_FINISH;
                       end
                   default:
                     begin
                        if_go<=0;
                        perr<=1'b1;
                        dbg_state<=ST_DBG_WAIT_GOCLEAR;
                     end
                 endcase
               
               ST_DBG_WAIT_TIMEOUT: // Waiting for timeout to complete ====================================
                 if (!usecsdown)
                   dbg_state <= ST_DBG_WAIT_GOCLEAR;
               
               ST_DBG_PINWRITE_WAIT: // Waiting for pinwrite timer to expire ==============================
                 if (!usecsdown)
                   begin
                      pinw_dread[7:0]={ tgt_reset_state, tgt_reset_pin, tdo_swo, tdi, swdi, tck_swclk };
                      dbg_state <= ST_DBG_IDLE;
                   end
                      
               ST_DBG_RESETTING: // We are in reset =======================================================
                 if (!usecsdown)
                   begin
                      usecsdown<=rst_timeout;
                      dbg_state<=ST_DBG_RESET_GUARD;
                   end

               ST_DBG_RESET_GUARD: // We have finished reset, but wait for chip to ack ===================
                 begin
                    if ((tgt_reset_state) || (!usecsdown))
                      dbg_state<=ST_DBG_WAIT_GOCLEAR;
                 end
             endcase // case (dbg_state)
             
          end // else: !if(rst)
     end // always @ (posedge clk, posedge rst)
   
endmodule // dbgIF
