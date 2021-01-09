// ram.v
// =====
//
// Provide dual clocked ram with parameterisable address and data widths.

module ram (din, write_en, waddr, wclk, raddr, rclk, dout);
   parameter  addr_width = 8;
   parameter  data_width = 16;
   input      [addr_width-1:0] waddr, raddr;
   input      [data_width-1:0] din;
   input      write_en, wclk, rclk;
   output reg [data_width-1:0] dout;
   reg        [data_width-1:0] mem [(1<<addr_width)-1:0];

always @(posedge wclk) // Write memory.
  begin
     if (write_en) mem[waddr] <= din; // Using write address bus.
  end

always @(posedge rclk) // Read memory.
  begin
     dout <= mem[raddr]; // Using read address bus.
  end
endmodule // ram
