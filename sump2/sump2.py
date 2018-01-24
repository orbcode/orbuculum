#!/usr/bin/python
# sump2
#               Copyright (c) Kevin M. Hubbard 2017 BlackMesaLabs
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later 
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. 
# See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
# Source file: sump2.py
# Date:    07.25.16
# Author:  Kevin M. Hubbard
# Description: A light weight VCD viewer written in Python+PyGame for Linux
#              or Windows platforms. Designed to make use of the mouse scroll
#              wheel for fast navigation and inspection of waveform files. Also
#              follows keyboard navigation used by Vim and ChipVault.
# History:
#  The backstory on ChipWave.py, which was then forked to become sump2.py 
#  is that I wrote it over a weekend while sequestered in a Redmond,WA hotel
#  chaperoning a highschool JSA tournament for my daughter's class. I was 
#  wanting a better VCD viewer for IcarusVerilog and was frustrated with 
#  the GTKwave user interface and difficulty installing on Linux. It was 
#  designed only to be the backend viewer for simulations, but turned out to 
#  be a really good front-end and back-end for SUMP hardware capture engine.
#  Original ( now SUMP1 ) design used .NET for waveform viewing which was
#  frustratingly slow. PyGame based SUMP2 gui is 100x better in my opinion.
#
# PyGame:
#  ChipWave uses to Python package PyGame for Mouse and Screen iterfacing. 
#  PyGame does not come with Python and MUST BE INSTALLED!
#     See http://www.pygame.org/download.shtml
#  ChipWave.py was written in 2013 as a VCD viewer for IcarusVerilog. It was
#  ditched in favor of just using GTKwave. Basic features were then reused for
#  a SUMP2 front-end and back-end to replace the SUMP1 Powershell/.NET app.
#  Note: There are some legacy ChipWave functions still in here that are not
#  currently being used and have not been removed.
#
# [ Python 3.5 for Windows ]
#   https://www.python.org/downloads/ Python 3.5.2 python-3.5.2.exe
#   Click Add to Path on Installer Popup
#
#   python.exe -c 'import distutils.util; print(distutils.util.get_platform())'
#   win32
#
# [ PyGame ]
#   http://www.lfd.uci.edu/~gohlke/pythonlibs/#pygame
#   pygame-1.9.2b1-cp35-cp35m-win_amd64.whl
#   pygame-1.9.2b1-cp35-cp35m-win32.whl
#
#   Copy WHL to C:\Users\root\AppData\Local\Programs\Python\Python35-32\Scripts
#   pip install pygame-1.9.2b1-cp35-cp35m-win32.whl
# 
# [ PySerial ]
#   https://pypi.python.org/pypi/pyserial
#   https://pypi.python.org/packages/...../pyserial-3.1.1-py2.py3-none-any.whl
#   pip install pyserial-3.1.1-py2.py3-none-any.whl
#
# [ spidev for RaspberryPi SPI Communications to icoboard ]
#   sudo apt-get install python-dev
#   sudo apt-get install git
#   git clone git://github.com/doceme/py-spidev
#   cd py-spidev
#   sudo python setup.py install
#
# TODO: Vertical resizing of window has issues. Signal scrolling isnt updated
#
# Revision History:
# Ver   When      Who       What
# ----  --------  --------  ---------------------------------------------------
# 0.00  07.25.16  khubbard  Creation. Forked from chip_wave.py ( VCD Viewer )
#
# TODO: Key repeats dont work. Fake out and make own key events ?
# TODO: scroll_up and scroll_down have issues if signals are deleted.
# TODO: Add a "GenerateVCD" feature that calls external program to make vcd
# TODO: Support for bus ripping. Complicated....
# TODO: Search only support Hex searches. Should support signed and unsigned
# TODO: Add support for an autorun.txt file on startup.
# TODO: Reload doesn't work if new VCD is longer than old VCD.
# TODO: On reload, wave.do should be saved and reloaded to preserve format.
# WARNING: vcdfile2signal_list() currently requires a clock signal or else
#          conversion doesnt work unless there is a value change every sample
# NOTE: Pygame stops responding if window is dragged onto second monitor.
# TODO: Doesn't support SUMP list reordering or wave.txt order.
#       09.05.16 khubbard   Fix for VCD exporting nicknames. GUI crash fixes.
#       09.06.16 khubbard   Partial ported from Python 2.7 to 3.5. Incomplete.
#       09.18.16 khubbard   Major performance improvements. GUI file loading  
#       09.19.16 khubbard   Adjust DWORDs in RLE for trig_delay value         
#       09.20.16 khubbard   RLE Undersample 4x-64x feature added. Popup change.
#       09.23.16 khubbard   Popup Entry for variable changes in GUI added.
#       09.24.16 khubbard   User Interface and performance usability improvmnts
#       09.25.16 khubbard   GUI popup for signal rename.
#       09.26.16 khubbard   Fixed opening VCD files for static offline viewing.
#       09.26.16 khubbard   zoom_out capped at max_samples. RLE->VCD working
#       09.29.16 khubbard   cursor_snap back in for single click op. VCD 'x'
#       10.04.16 khubbard   fast_render added. Disable 4x prerender on >1000
#       10.06.16 khubbard   Fixed popup bugs. sump_bundle_data() feature added
#       10.16.16 khubbard   RLE culling null sample improvements. Menu changes.
#       10.17.16 khubbard   fixed vcdfile2signal_list() not decompressing.
#       10.18.16 khubbard   fixed menu. New function list_remove
#       10.19.16 khubbard   RLE Event to DWORD alignment fix. Needs HW too.
#       10.20.16 khubbard   Improve centering to trigger post acquisition. 
#       10.21.16 khubbard   Acquisition_Length fixed. Also works with RLE now.
#       10.24.16 khubbard   Fixed RLE cropping not showing DWORDs.Speed Improvs
#       10.26.16 khubbard   TCP change to send full DWORDs with leading 0s
#       12.19.16 khubbard   Fork for Pi and icoboard from Windows original.
#     2017.01.05 khubbard   Merge Win,Pi fork. bundle fix. Trigger enhancements
#     2017.01.07 khubbard   Fixed Pi import SpiDev issues with 2017.01.05
#     2017.03.09 khubbard   Arm without polling option added.
#     2017.03.09 khubbard   Only allow single falling edge trigger.
#     2017.03.09 khubbard   Replace 2D arrays from VCD export.     
#     2017.03.14 khubbard   Added show/hide support to children of bundles.
#     2017.04.19 khubbard   Keyboard macros and crop_to_ini added.
#     2017.04.25 khubbard   startup and shutdown scripting.
#     2017.06.08 khubbard   Added system command for calling external programs
#     2017.09.13 khubbard   Fixed poor compression in conv_txt2vcd()
#     2017.09.13 khubbard   Faster VCD by not iterating hidden sump_save_txt()
#     2017.09.13 khubbard   Updated manual
#     2017.09.22 khubbard   Fixed zoom_out bug. Rendering performance increase.
#     2017.09.22 khubbard   trigger_remove_all added. rle_autocull added.
#     2017.09.22 khubbard   rle_autocull added. rle_gap_remove added.
#     2017.09.23 khubbard   Convrtd sig values from "1" to True.1/2 DRAM needed
#     2017.09.23 khubbard   RLE_LOSSY fully implemented. 10x speedup for 2M pts
#     2017.09.23 khubbard   Smart time cursors scales between ns,us and ms 
#     2017.09.25 khubbard   Time units displayed for windows.
#     2017.09.25 khubbard   Fixed fast rendering of static signals.
#     2017.09.26 khubbard   Improved cursor operations.
#     2017.09.27 khubbard   rle_gap_replace added.      
#     2017.09.30 khubbard   Fix RLE_LOSSY bug to pad enough samples for DWORDs
#     2017.09.30 khubbard   Fix rle_autocull bug tossing too many samples
#     2017.10.01 khubbard   Fix rle_autocull post trigger bug            
#     2017.10.05 khubbard   list_view added
#     2017.10.05 khubbard   dont load startup file if no hardware ( ie VCD )
#     2017.10.06 khubbard   VCD gen 10x faster, bundles and load_vcd added.
#
# Note: VV tags every place sig.values were converted from "1" to True 
#
# Web Location:
#   https://github.com/blackmesalabs/sump2
# Also please see:
#   https://blackmesalabs.wordpress.com/
#     2016/12/22/sump2-100-msps-32bit-logic-analyzer-for-icoboardraspberrypi/
#     2016/10/24/sump2-96-msps-logic-analyzer-for-22/
###############################################################################
import time   
from time import sleep;
import math   # pow
import types  # type
import sys;
import os;
import platform;
import locale;

class main(object):
 def __init__(self):
  self.vers = "2017.10.06";
  print("Welcome to SUMP2 " + self.vers + " by BlackMesaLabs");
  self.mode_cli = True;

  try:
    import pygame # Import PyGame Module
  except:
    print("WARNING: PyGame not FOUND!! running in Command Line Mode");
    print("Pygame http://www.lfd.uci.edu/~gohlke/pythonlibs/#pygame");
    print("pip install pygame-1.9.2b1-cp35-cp35m-win32.whl");

  self.vars = init_vars( self, "sump2.ini" );
  self.help = init_help( self );
  self.math   = math;
  list2file( self, "sump2_manual.txt", init_manual(self ) );

  # Generate a Text file that shows all the key macros
  # vars["sump_macro_1"]  = "Acquire_RLE";
  macro_list = [];
  for key in self.vars:
    if ( "sump_macro_" in key ):
      macro_list += [ key + " " + self.vars[key] ];
  list2file( self, "sump2_key_macros.txt", sorted(macro_list) );

  init_globals( self );# Internal software variables
  try:
    print( self.os_sys );
    if ( self.os_sys != "Linux" ):
      locale.setlocale( locale.LC_NUMERIC, 'English' );
#   if ( self.os_sys == "Linux" ):
#     import spidev;# SPI Interface Module for Pi
#   else:
#     locale.setlocale( locale.LC_NUMERIC, 'English' );
  except:
    print("WARNING: OS Detection Failed!! Don't know what to do.");

  self.file_log = open ( self.vars["file_log"] , 'w' );


  # ARG0 either specifies a static file to view OR sump2 or an IP address
  #  for talking directly to sump2 hardware.
  import sys;
  args = sys.argv + [None]*3;
  self.file_name = args[1]; # args[0] is script name

  if ( self.file_name == "bd_shell" or \
       self.file_name == "cli"      ):
    self.mode_cli = True;
    self.file_name = None;
  else:
    if 'pygame' in locals() or 'pygame' in globals():
      display_init( self );
      self.mode_cli = False;
    else:
      self.mode_cli = True;

  self.signal_list = [];# List of class signal(object)s
  self.signal_delete_list = [];

  if ( self.file_name == None ):
#   if ( sump_connect( self ) == False ):
#     shutdown( self );
#   sump_connect( self );
    if ( sump_connect( self ) != False ):
      sump2signal_list( self );# Make Signals based on SUMP2 HW Config
      self.top_module = "sump2";
    else:
#     make_demo_vcd();
#     self.file_name = "foo.vcd";
      self.file_name = make_demo_vcd( self );

#   sump_dump_data(self);
# else:
  if ( self.file_name != None ):
    self.bd=None;
    file2signal_list( self, self.file_name );# VCD is now a signal_list
#   save_format( self, self.file_name, False );# Create Wave File from VCD Info
    self.vcd_import = True;# Prevents saving a VCD specific sump2_wave.txt file
    self.vcd_name   = self.file_name;
    self.file_name = None; # Make sure we don't overwrite vcd with wave on exit


# # Attempt to loading an existing wave.txt file for this block if exists
# # otherwise, create one from scratch
# import os;
# file_name = "wave_" + self.top_module + ".txt";# Default
# if os.path.exists( file_name ):
#   print( "load_format() ", file_name );
#   load_format( self, file_name );
# else:
#   save_format( self, file_name, False );


  if ( self.bd != None ):
    ###########################################################################
    # If a wave file doesn't exist, create a default one using info from HW
    import os;
#   file_name = "wave_" + self.top_module + ".txt";# Default
    file_name = "sump2_wave.txt";# Default
    if ( os.path.exists( file_name ) == False and self.bd != None ):
      print("Creating default wave file");
      ram_dwords = self.sump.cfg_dict['ram_dwords'];
      ram_bytes  = self.sump.cfg_dict['ram_event_bytes'];
      events = ram_bytes * 8;

      # Iterate the number of event bits and init with 0s
      txt_list = [];
      for j in range( 0, events, 1):
        txt       = ("/event[%d]" % j) ;
        txt_list += [ txt + " -nickname " + txt.replace("/","") ];

      # Then follow with a group for all the DWORDs
      if ( ram_dwords != 0 ):
        txt_list += ["/dword[%d:%d]" % ( ( 0),( ram_dwords-1) )];
        for i in range( 0, ram_dwords, 1):
          txt       = "  /dword[%d]" % ( i );
          txt_list += [ txt + " -nickname " + txt.replace("/","") ];

      file_out  = open( "sump2_wave.txt", 'w' );

      for each in txt_list:
        file_out.write( each + "\n" );
      file_out.close(); 

    # Load in the wavefile
    if os.path.exists( file_name ):
      draw_header( self, ( "load_format() "+ file_name ) );# Pi
      print( "load_format() ", file_name );
      load_format( self, file_name ); 
      trig_i = sump_dump_data(self);
      self.trig_i = trig_i;
      sump_vars_to_signal_attribs( self );# Populates things like trigger attr

#   if os.path.exists( file_name ):
#   print( "load_format() ", file_name );
#   load_format( self, file_name ); 
#   if ( self.bd != None ):
#     sump_dump_data(self);
# else:
#   save_format( self, file_name, False );# Create one
#   return;


  #############################################################################
  # CLI Main Loop : When no PyGame loop here STDIN,STDOUT old school style
  while( self.mode_cli == True and self.done == False ):
    rts = raw_input(self.prompt);
#   rts = input(self.prompt);
    rts = rts.replace("="," = ");
    words = " ".join(rts.split()).split(' ') + [None] * 4;
    if ( words[1] == "=" ):
      cmd   = words[1];
      parms = [words[0]]+words[2:];
    else:
      cmd   = words[0];
      parms = words[1:];
#   print( cmd, parms );
    rts = proc_cmd( self, cmd, parms );
    for each in rts:
      print( each );


  # Load the wavefile 

# # Calc max number of samples and change default zoom if not enough to fill
# for sig_obj in self.signal_list:
#   if ( len( sig_obj.values )  > self.max_samples ):
#     self.max_samples = len( sig_obj.values );
# if ( ( self.max_samples * self.zoom_x ) < self.screen_width ):
#   self.zoom_x = float(self.screen_width) / float(self.max_samples);
# set_zoom_x( self, self.zoom_x ); # Set the zoom ratio

  recalc_max_samples( self );

  # Draw the 1st startup screen
  screen_refresh( self );
  self.context = "gui";

  self.pygame.mouse.set_cursor(*pygame.cursors.arrow);# Pi

  file_startup = self.vars["sump_script_startup"];
  import os.path
# if ( os.path.isfile(file_startup) ):
  if ( os.path.isfile(file_startup) and self.bd != None ):
    rts = source_file( self, [file_startup] );
    sump_vars_to_signal_attribs( self );# Update triggers, etc
    self.name_surface_valid = False;
    screen_refresh( self );

  # GUI Main Loop
  self.clock = self.pygame.time.Clock();
  self.time  = self.pygame.time;
  self.pygame.key.set_repeat(50,200);
  while ( self.done==False ):
    # When live, attempt to acquire data, else display static data (faster)
    if ( self.acq_state=="arm_rle" or  self.acq_state=="arm_normal"):
      draw_header( self,"Armed");
    if ( self.acq_state == "acquire_single" or
         "acquire_rle" in self.acq_state  or
         "download"    in self.acq_state  or
         self.acq_state == "acquire_continuous" ):
      # Check to see if acquired bit is set, then read the data
      # print ("%02X" % self.sump.rd( addr = None )[0] );

      # There are different Done HW bits for RLE versus non-RLE acquisitions
      if ( "acquire_rle" in self.acq_state  ):
        sump_done = self.sump.status_triggered + self.sump.status_rle_post;
      else:
        sump_done = self.sump.status_triggered + self.sump.status_ram_post;

      self.undersample_data = False;
      self.undersample_rate = 1;
#     if ( ( self.sump.rd( addr = None )[0] & sump_done ) == sump_done ):
      if ( 
           ((self.sump.rd( addr = None )[0] & sump_done ) == sump_done ) or
           ( "download" in self.acq_state )
         ):
        if   ( self.acq_state == "download_rle" ): 
          self.acq_state = "acquire_rle";
        elif ( self.acq_state == "download_rle_lossy" ): 
          self.acq_state = "acquire_rle_lossy";
        elif ( self.acq_state == "download_normal" ): 
          self.acq_state = "acquire_single";

        if ( self.vcd_import == True ):
          self.signal_list = self.signal_list_hw[:];# Support returning to HW 
          self.vcd_import = False;

        if ( self.acq_mode == "nonrle" ):
          trig_i = sump_dump_data(self);
          self.rle_lossy = False;
        else:
          trig_i = sump_dump_rle_data(self);
        self.trig_i = trig_i;
        print("Trigger Index = %d " % trig_i );

        # Place the cursors by the trigger.
        for ( i , each ) in enumerate( self.cursor_list ):
          if ( i == 0 ): offset = -6;
          else         : offset = +4;
          each.selected = False;
#         trigger_sample = self.max_samples // 2;# Temporary Trig @ 50%
#         each.sample = int( trigger_sample ) + offset;
          each.sample = int( trig_i ) + offset;
        self.curval_surface_valid = False;# curval surface invalid 
 
#       print( self.acq_state );
        if ( self.acq_state == "acquire_continuous" ):
          sump_arm( self, True );
#       elif ( self.acq_state=="arm_rle" or  self.acq_state=="arm_normal"):
#         draw_header( self,"Armed");
#         pass;
        else:
          self.acq_state = "acquire_stop";
          draw_header( self, "ACQUIRED");
          print("RENDERING-START");
#         start = ( self.max_samples // 2 ) - ( self.max_samples // 8 );
#         stop  = ( self.max_samples // 2 ) + ( self.max_samples // 8 );

          # Zoom-Out the maximum amount that still keeps trigger centered
          # for non-RLE this is trivial, for RLE it is more complicated
          trig_to_start = trig_i - 0;
          trig_to_end   = self.max_samples - trig_i;
          start = trig_i - min( trig_to_start, trig_to_end );
          stop  = trig_i + min( trig_to_start, trig_to_end );

#         print(" self.max_samples  %d" % self.max_samples );
#         print(" trig_i            %d" % trig_i           );
#         print(" trig_to_start     %d" % trig_to_start    );
#         print(" trig_to_end       %d" % trig_to_end      );
#         print(" start             %d" % start            );
#         print(" stop              %d" % stop             );

# Pi is slower for rendering, so render less
#         start = trig_i - min( trig_to_start, trig_to_end ) // 2;
#         stop  = trig_i + min( trig_to_start, trig_to_end ) // 2;

          proc_cmd( self, "zoom_to", [str(start), str(stop) ] );
#         proc_cmd( self, "zoom_to", ["0", str( self.max_samples ) ] );
#         proc_cmd( self, "zoom_to_cursors", [] );
          print("RENDERING-COMPLETE");

          file_shutdown = self.vars["sump_script_shutdown"];
          import os.path
          if ( os.path.isfile(file_shutdown) ):
            rts = source_file( self, [file_shutdown] );
            sump_vars_to_signal_attribs( self );# Update triggers, etc
            self.name_surface_valid = False;
            screen_refresh( self );

      else:
#       draw_header(self,"Waiting for trigger..");
        draw_screen( self );# This updates banner
        self.time.wait(500 ); # Waiting for Trigger

    else:
#     self.clock.tick( 10 ); # Don't take 100% of CPU as that would be rude
      self.time.wait(10 ); # Wait 10ms to share CPU

    for event in pygame.event.get(): # User did something
      # VIDEORESIZE
      if ( self.os_sys != "Linux" ):
        if event.type == pygame.VIDEORESIZE:
          self.screen= pygame.display.set_mode(event.dict['size'], 
                              pygame.RESIZABLE | 
                              pygame.HWSURFACE |
                              pygame.DOUBLEBUF);
          self.resize_on_mouse_motion = True;# Delay redraw until resize done

      # Detect when console box has gained focus and switch from GUI to BD_SHELL
      # and loop in a keyboard loop processing commands. Exit on a NULL Command.
#     if event.type == pygame.ACTIVEEVENT:
#       #print(  str(event.gain) + " " + str(event.state)  );
#       # state=2 user gave focus to something other than GUI. Assume DOS-Box
#       if ( event.state == 2 and self.os_sys != "Linux" ):
#         bd_shell( self );

      # KEYDOWN           
      if event.type == pygame.KEYDOWN:
        if ( event.key == pygame.K_BACKSPACE ):
          self.key_buffer = self.key_buffer[:-1];# Remove last char
        elif ( event.key == pygame.K_DELETE ):
          proc_cmd( self, "delete", [""] );
        elif ( event.key == pygame.K_INSERT ):
          proc_cmd( self, "insert_divider", [""] );
        elif ( event.key == pygame.K_PAGEUP    ):
          proc_cmd( self, "zoom_in"  , [] );
        elif ( event.key == pygame.K_PAGEDOWN  ):
          proc_cmd( self, "zoom_out" , [] );
        elif ( event.key == pygame.K_HOME      ):
          proc_cmd( self, "font_larger"  , [] );
        elif ( event.key == pygame.K_END       ):
          proc_cmd( self, "font_smaller" , [] );
#       elif ( event.key == pygame.K_Q         ):
#         proc_cmd( self, "quit" , [] );
        elif ( event.key == pygame.K_RIGHT ):
          num_samples = self.sample_room // 16;
          proc_cmd( self, "scroll_right", [str(num_samples)] );
        elif ( event.key == pygame.K_LEFT  ):
          num_samples = self.sample_room // 16;
          proc_cmd( self, "scroll_left", [str(num_samples)] );
        elif ( event.key == pygame.K_F1  or
               event.key == pygame.K_F2  or
               event.key == pygame.K_F3  or
               event.key == pygame.K_F4  or
               event.key == pygame.K_F5  or
               event.key == pygame.K_F6  or
               event.key == pygame.K_F7  or
               event.key == pygame.K_F8  or
               event.key == pygame.K_F9  or
               event.key == pygame.K_F10 or
               event.key == pygame.K_F11 or
               event.key == pygame.K_F12 or
               event.key == pygame.K_1   or
               event.key == pygame.K_2   or
               event.key == pygame.K_3   or
               event.key == pygame.K_4   or
               event.key == pygame.K_5   or
               event.key == pygame.K_6   or
               event.key == pygame.K_7   or
               event.key == pygame.K_8   or
               event.key == pygame.K_9   or
               event.key == pygame.K_0   
             ):
          if   ( event.key==pygame.K_1 ): cmd=( self.vars["sump_macro_1"]);
          elif ( event.key==pygame.K_2 ): cmd=( self.vars["sump_macro_2"]);
          elif ( event.key==pygame.K_3 ): cmd=( self.vars["sump_macro_3"]);
          elif ( event.key==pygame.K_4 ): cmd=( self.vars["sump_macro_4"]);
          elif ( event.key==pygame.K_5 ): cmd=( self.vars["sump_macro_5"]);
          elif ( event.key==pygame.K_6 ): cmd=( self.vars["sump_macro_6"]);
          elif ( event.key==pygame.K_7 ): cmd=( self.vars["sump_macro_7"]);
          elif ( event.key==pygame.K_8 ): cmd=( self.vars["sump_macro_8"]);
          elif ( event.key==pygame.K_9 ): cmd=( self.vars["sump_macro_9"]);
          elif ( event.key==pygame.K_0 ): cmd=( self.vars["sump_macro_0"]);

          if   ( event.key==pygame.K_F1 ): cmd=( self.vars["sump_macro_F1"]);
          elif ( event.key==pygame.K_F2 ): cmd=( self.vars["sump_macro_F2"]);
          elif ( event.key==pygame.K_F3 ): cmd=( self.vars["sump_macro_F3"]);
          elif ( event.key==pygame.K_F4 ): cmd=( self.vars["sump_macro_F4"]);
          elif ( event.key==pygame.K_F5 ): cmd=( self.vars["sump_macro_F5"]);
          elif ( event.key==pygame.K_F6 ): cmd=( self.vars["sump_macro_F6"]);
          elif ( event.key==pygame.K_F7 ): cmd=( self.vars["sump_macro_F7"]);
          elif ( event.key==pygame.K_F8 ): cmd=( self.vars["sump_macro_F8"]);
          elif ( event.key==pygame.K_F9 ): cmd=( self.vars["sump_macro_F9"]);
          elif ( event.key==pygame.K_F10): cmd=( self.vars["sump_macro_F10"]);
          elif ( event.key==pygame.K_F11): cmd=( self.vars["sump_macro_F11"]);
          elif ( event.key==pygame.K_F12): cmd=( self.vars["sump_macro_F12"]);
          proc_cmd( self, cmd.lower(), [] );

        # Up and Down arrows either Zoom In,Out or Scroll the Signal list
        elif ( event.key == pygame.K_UP    ):
          if ( self.mouse_region == "signal_name" ):
            proc_cmd( self, "scroll_up"  , ["1"] );
          else:
            proc_cmd( self, "zoom_in"    , [] );
        elif ( event.key == pygame.K_DOWN  ):
          if ( self.mouse_region == "signal_name" ):
            proc_cmd( self, "scroll_down", ["1"] );
          else:
            proc_cmd( self, "zoom_out", [] );

        elif ( event.key == pygame.K_SPACE and self.key_buffer == "" ):
          proc_cmd( self, "Expand", [""] );
        draw_screen( self );
        screen_flip( self );



# Note: Text Entry moved to DOS-Box
#       elif ( event.key == pygame.K_RETURN ):
#         self.key_buffer = self.key_buffer.replace("="," = ");
#         words = self.key_buffer.strip().split()+[None]*4;# Avoid IndexError
#         cmd = words[0];
#         parms = words[1:];
#         if ( self.txt_entry == True ):
#           cmd   = self.txt_entry_caption;# ie "Rename_Signal"
#           parms = words[0:];
#           self.txt_entry = False; # Disable Dialog Box
#         rts = proc_cmd( self, cmd, parms );
#         for each in rts:
#           print( each );# <CR>s
#         sys.stdout.write( self.prompt );# No <CR>
#         self.cmd_history.append( " " + self.key_buffer );
#         self.key_buffer = "";
#       elif ( event.key > 0 and event.key < 255 ):
#         self.key_buffer = self.key_buffer + event.unicode;
#         if ( event.unicode == "/" or event.unicode == "?" ):
#           self.key_buffer = self.key_buffer + " ";
#         sys.stdout.write( event.unicode );

      # QUIT
      if ( event.type == pygame.QUIT ) : 
        self.done=True;

      # MOUSEMOTION 
      if event.type == pygame.MOUSEMOTION:
        (self.mouse_x,self.mouse_y) = pygame.mouse.get_pos();
#       self.mouse_region = get_mouse_region(self,self.mouse_x,self.mouse_y);
#       # If mouse wave moved to right of the value region, scroll once to the
#       # right and then create a fake MOUSEMOTION event to continue scrolling
#       # until mouse is moved away. 
#       if ( self.mouse_region == "scroll_right" or \
#            self.mouse_region == "scroll_left"       ):
#         proc_cmd( self, self.mouse_region , ["1"] ); # scroll left or right
#         # TODO: This wait time needs to be configurable. On my HP Centos
#         # laptop it scrolled too fast at 50ms, too slow at 250ms.
#         # Make sure mouse is still in this window ( focused )
#         if ( self.pygame.mouse.get_focused() == True ):
#           self.pygame.time.wait( 100 );# Delay in ms
#           self.pygame.event.post( pygame.event.Event( pygame.MOUSEMOTION ) );

        # If a resize op was just completed, redraw on 1st mouse motion as
        # trying to redraw during the resize is very slow and jerky.
        if ( self.resize_on_mouse_motion == True ):
          self.resize_on_mouse_motion = False; # This makes resize op smoother
          old_width = self.screen_width;
          ( self.screen_width, self.screen_height ) = self.screen.get_size();
          self.vars["screen_width"]  = str( self.screen_width );
          self.vars["screen_height"] = str( self.screen_height );
#         if ( self.screen_width > old_width ):
          # This is a HACK as sig_value_stop_x wasn't auto adjusting for some
          # reason when window is resized to be larger.
          # There is like a chicken and egg problem with zoom_full and stop_x
          # adjust stop_x for delta change rather than calling zoom_full twice
          self.sig_value_stop_x += ( self.screen_width - old_width );
          proc_cmd( self, "zoom_full", [] );# HACK Needed to update parms
#         create_surfaces( self );
#         flush_surface_cache( self );
#         screen_refresh( self );

        # If popup enabled, continue drawing and updating until button release
        if ( self.popup_x != None ):
          self.popup_sel = get_popup_sel( self );
#         print("draw_popup_cmd()");
          draw_popup_cmd( self );# Just draw popup on top of existing display
          screen_flip( self );# Only thing changing is the popup selection

        # If mouse button is held down, check for drag operation
#       elif ( self.mouse_button != 0 ):
        elif ( self.mouse_button == 1 or \
               self.mouse_button == 2       ):
          # Make sure the region doesnt wander, so calc from mouse press
          self.mouse_region = get_mouse_region(self,
                      self.mouse_btn1dn_x, self.mouse_btn1dn_y );
#         print( self.mouse_region );# HERE
          if   ( self.mouse_region == "cursor" ):
            mouse_event_move_cursor( self );      # Move a cursor
          elif ( self.mouse_region == "slider" ):
            mouse_event_move_slider( self,0 );    # Move the viewport slider
          elif ( self.mouse_region == "signal_name" ):
            mouse_event_vertical_drag_wip( self );# Move a signal name
# Doesnt work well
#         elif ( self.mouse_region == "signal_value" ):
#           # Calculate mouse drag deltas in char units
#           delta_x=abs(self.mouse_btn1dn_x-self.mouse_x) / self.txt_width;
#           delta_y=abs(self.mouse_btn1dn_y-self.mouse_y) / self.txt_height;
#           if ( delta_x > 2 and delta_y > 2 ):
#             mouse_event_area_drag_wip( self );  # Rectangle Zoom Region

      # MOUSEBUTTONUP : event.button  1=Left,2=Middle,3=Right,4=ScrlUp,5=ScrlDn
      if event.type == pygame.MOUSEBUTTONUP:
        (self.mouse_x,self.mouse_y) = pygame.mouse.get_pos();
        self.mouse_region = get_mouse_region(self,self.mouse_x,self.mouse_y);
        if ( event.button == 1 ):    
          (self.mouse_btn1up_x,self.mouse_btn1up_y)=(self.mouse_x,self.mouse_y);
        if ( event.button == 3 ):    
          (self.mouse_btn3up_x,self.mouse_btn3up_y)=(self.mouse_x,self.mouse_y);

        # Attempt to detect double-click on left-mouse button t<300ms
        if ( event.button == 1 ):    
          self.mouse_btn1up_time_last = self.mouse_btn1up_time;
          self.mouse_btn1up_time = self.pygame.time.get_ticks();
          if ( ( self.mouse_btn1up_time - self.mouse_btn1up_time_last ) < 300 ):
            mouse_event_double_click( self );
        
        delta_y=abs( self.mouse_btn3dn_y-self.mouse_btn3up_y )/self.txt_height;
        delta_x=abs( self.mouse_btn3dn_x-self.mouse_btn3up_x )/self.txt_width;
        # If popup enabled, process the cmd
#       if ( self.popup_x != None ):
        if ( self.popup_x != None and 
             ( event.button == 1 ) or
             ( event.button == 3 and ( delta_y > 1.0 or delta_x > 1.0 )  ) 
           ):
#         proc_cmd( self, self.popup_sel, [""] );
          words = self.popup_sel.strip().split() + [""] * 4;# AvoidIndexError
          proc_cmd( self, words[0],words[1:] );
          self.popup_x = None;# Erase popup
          self.popup_parent_x = None;
        else:
          # Mouse Button 1 Only - except emulate Center Mouse Drag to Zoom
          delta_x = abs( self.mouse_btn1dn_x-self.mouse_btn1up_x ) / \
                         self.txt_width;
          delta_y = abs( self.mouse_btn1dn_y-self.mouse_btn1up_y ) / \
                         self.txt_height;

#         if ( event.button == 2 and delta_x > 2 and delta_y > 2 ):
          if ( event.button == 1 or
              ( event.button == 2 and delta_x > 2 and delta_y > 2 ) ):
            # Mouse region is from 1st click, not release
            self.mouse_region = get_mouse_region(self,
                                self.mouse_btn1dn_x, self.mouse_btn1dn_y );
            if   ( self.mouse_region == "cursor" ):
              mouse_event_move_cursor( self );      # Move a cursor
            elif ( self.mouse_region == "slider" ):
              mouse_event_move_slider( self,0 );    # Move the viewport slider
            elif ( self.mouse_region == "signal_expand" ):
              proc_cmd( self, "Expand", [""] );
            elif ( self.mouse_region == "signal_name" ):
              delta_y = abs( self.mouse_btn1dn_y-self.mouse_btn1up_y ) / \
                             self.txt_height;
              if ( delta_y > 0 ):
                mouse_event_vertical_drag_done( self,                     \
                                  ((self.mouse_btn1dn_y-self.mouse_btn1up_y) / \
                                   self.txt_height ) );# Reorder signal list

#           elif ( self.mouse_region == "signal_value" ):
#             delta_x = abs( self.mouse_btn1dn_x-self.mouse_btn1up_x ) / \
#                            self.txt_width;
#             delta_y = abs( self.mouse_btn1dn_y-self.mouse_btn1up_y ) / \
#                            self.txt_height;
#             if ( delta_x < 2 and delta_y < 2 ):
#               mouse_event_single_click( self ); # Moves Cursor to here
#             if ( delta_x > 2 and delta_y < 2 ):
#               # signal_value region is being dragged, so pan left or right
#               direction = (self.mouse_btn1dn_x-self.mouse_btn1up_x) /  \
#                            self.zoom_x;
#               proc_cmd( self, "scroll_right", [str( int(direction) ) ] );
#
#             elif ( delta_x > 2 and delta_y > 2 ):
#               mouse_event_area_drag_done( self );    # Zooms to region

          if ( event.button == 2 and delta_x < 2 and delta_y < 2 ):
            print( "Center Mouse Button Click");

          # Mouse-Scroll Wheel
          # region==signal_name  : Scroll Up and Down
          # region==signal_value : Scroll Left and Right
          # region==slider       : Zoom in and out      
          elif ( event.button >= 4 ):    
            # print( self.mouse_region );
            if   ( self.mouse_region == "signal_name" ):
              if ( event.button == 4 ):
                proc_cmd( self, "scroll_up", ["1"] );
              elif ( event.button == 5 ):
                proc_cmd( self, "scroll_down", ["1"] );

            elif ( self.mouse_region == "signal_value" ):
              if ( event.button == 4 ):
                mouse_event_zoom_scroll( self, +1 );
              elif ( event.button == 5 ):
                mouse_event_zoom_scroll( self, -1 );

            elif ( self.mouse_region == "slider" ):
              if ( event.button == 4 ):
                proc_cmd( self, "scroll_right",[str(+self.scroll_num_samples)]);
              elif ( event.button == 5 ):
                proc_cmd( self, "scroll_left",[str(+self.scroll_num_samples)]);

            elif ( self.mouse_region == "cursor" ):
              self.curval_surface_valid = False;# curval surface is now invalid
              for cur_obj in self.cursor_list:
                if ( cur_obj.selected == True ):
                  sample = cur_obj.sample;
                  if ( event.button == 4 ):
                    sample +=1;
                  elif ( event.button == 5 ):
                    sample -=1;
                  if ( sample < 0 ) : sample = 0;
                  if ( sample > self.max_samples ): sample = self.max_samples;
                  cur_obj.sample = sample;
                  screen_refresh( self );

        self.mouse_button = 0; # No Button is Pressed

      # MOUSEBUTTONDOWN : 1=Left,2=Middle,3=Right,4=ScrlUp,5=ScrlDn
      if event.type == pygame.MOUSEBUTTONDOWN:
        self.mouse_button = event.button;# Remember which button is Pressed
        (self.mouse_x,self.mouse_y) = pygame.mouse.get_pos();
        self.mouse_region = get_mouse_region(self,self.mouse_x,self.mouse_y);

        # Left-Mouse-Button-Down
        # If popup is already up and right-click is clicked again, emulate left
        if ( event.button == 1 or event.button == 2 or 
             ( event.button == 3 and self.popup_x != None ) ): 
          self.mouse_btn1dn_time = self.pygame.time.get_ticks();
          (self.mouse_x,self.mouse_y) = pygame.mouse.get_pos();     
          (self.mouse_btn1dn_x,self.mouse_btn1dn_y) = \
               (self.mouse_x,self.mouse_y);

          if   ( self.mouse_region == "slider" ):
            mouse_event_move_slider( self, 0 );
          elif ( self.mouse_region == "signal_name" or
                 self.mouse_region == "signal_expand" ):
            mouse_event_select_signal( self );
          elif ( self.mouse_region == "signal_value" ):
            mouse_event_single_click( self ); # Moves Cursor to here
#           pass;

        # Right-Mouse-Button-Down
        if ( event.button == 3 and self.popup_x == None ):
          (self.popup_x,self.popup_y) = pygame.mouse.get_pos();
          (self.mouse_btn3dn_x,self.mouse_btn3dn_y) = \
               (self.mouse_x,self.mouse_y);
	  # For cursor bring tos want to know exacy sample right click was on
          (self.popup_sample, Null ) = get_sample_at_mouse( self, 
			                  self.popup_x, self.popup_y );
          # Set the popup up and to the left so that a click and release
          # selects the 1st guy ( Scroll_Toggle ) - a hack - I know.
          self.popup_x -= 2*self.txt_width;
          self.popup_y -= self.txt_height;

          if   ( self.mouse_region == "signal_value" ):
            self.popup_list = self.popup_list_values;
#         elif ( self.mouse_region == "signal_name" ):
          else:
            self.popup_list = self.popup_list_names;
          draw_popup_cmd( self );
          self.popup_sel = get_popup_sel( self );
          screen_flip( self ); # Place popup on top existing stuff, no erase
#         self.acq_state = "acquire_stop";# Stop any live acquisitions



    # New
#   draw_screen( self );
#   screen_flip( self );

  shutdown( self, False, False );
  return;# This is end of main program loop

###############################################################################
def recalc_max_samples( self ):
  # Calc max number of samples and change default zoom if not enough to fill
  self.max_samples = 0;
  for sig_obj in self.signal_list:
    if ( len( sig_obj.values )  > self.max_samples ):
      self.max_samples = len( sig_obj.values );
  if ( self.mode_cli == True ):
    return;
  if ( float(self.max_samples) != 0.0 and
       ( self.max_samples * self.zoom_x ) < self.screen_width ):
    self.zoom_x = float(self.screen_width) / float(self.max_samples);
  set_zoom_x( self, self.zoom_x ); # Set the zoom ratio

  # Warning: This sample_room calculation assumes samples are 1 nibble wide.
  sample_start = self.sample_start;
  start_x = self.sig_value_start_x;
  x2 = self.screen_width - start_x - 2*self.txt_width;
  self.sample_room = int( float(x2) / float(self.zoom_x) );
  self.sample_stop  = sample_start + self.sample_room;
  return;

def display_init( self ):
  log( self, ["display_init()"] );
  import pygame # Import PyGame Module
  pygame.init() # Initialize the game engine
  if ( self.os_sys == "XXLinux" ):
    ( self.screen_width, self.screen_height ) = \
      (pygame.display.Info().current_w, pygame.display.Info().current_h);
    print( self.screen_width );
    print( self.screen_height );
    self.screen=pygame.display.set_mode(
       [ self.screen_width, self.screen_height ],
       pygame.FULLSCREEN );
  else:
    self.screen_width  = int( self.vars["screen_width"], 10 );
    self.screen_height = int( self.vars["screen_height"], 10 );
    self.screen=pygame.display.set_mode(
       [ self.screen_width, self.screen_height ],
       pygame.RESIZABLE | pygame.HWSURFACE | pygame.DOUBLEBUF );


  self.pygame = pygame;
  self.pygame.display.set_icon( create_icon( self ) );
  draw_header( self, "" );
  self.font = get_font( self,self.vars["font_name"],self.vars["font_size"]);
  self.gui_active = True;
  create_surfaces( self );
  return;


# mouse_event_select_signal() : User has clicked the mouse in the signal name
# region, so either deselect the old selection and select the new signal at
# the mouse location, or if the shift key is held down, select everything from
# old selection to new location.
def mouse_event_select_signal( self ):
  self.name_surface_valid = False;
  if ( self.pygame.key.get_pressed()[self.pygame.K_LSHIFT] or 
       self.pygame.key.get_pressed()[self.pygame.K_RSHIFT]    ):

#   (Null,index) = get_sample_at_mouse( self, self.mouse_x, self.mouse_y );
#   if ( index != None ):
#     sig_obj = self.signal_list[ index ];
#     sig_obj.selected = True;
#     self.sig_obj_sel = sig_obj;# Remember for pulldown commands
    sig_obj = get_sig_obj_at_mouse( self, self.mouse_x, self.mouse_y );
    if ( sig_obj != None ):
      sig_obj.selected = True;
      self.sig_obj_sel = sig_obj;# Remember for pulldown commands

      start_jk = False;
      for sig_obj in self.signal_list:
        # Select all visible signals between old select and new select
        if ( start_jk == True and sig_obj.visible == True ):
          sig_obj.selected = True;
        # Start the grouping on the old select
        if ( sig_obj.selected == True ):
          start_jk = True;
        # Finish when we get to new select 
        if ( sig_obj == self.sig_obj_sel ):
          start_jk = False;
          break;
      screen_refresh( self );
    
  else:
    # DeSelect All signals unless a CTRL key is held down
    if ( self.pygame.key.get_pressed()[self.pygame.K_LCTRL] == False and
         self.pygame.key.get_pressed()[self.pygame.K_RCTRL] == False     ):
      for sig_obj in self.signal_list:
        sig_obj.selected = False;
    # Find the signal at the mouse location at select it.
    sig_obj = get_sig_obj_at_mouse( self, self.mouse_x, self.mouse_y );
    if ( sig_obj != None ):
      sig_obj.selected = True;
      self.sig_obj_sel = sig_obj;# Remember for pulldown commands
      screen_refresh( self );
  return;


def mouse_event_double_click( self ):
# print "mouse_event_double_click()";
  sig_obj = get_sig_obj_at_mouse( self, self.mouse_x, self.mouse_y );
  if ( sig_obj != None ):
    sig_obj.selected = True;
    if ( sig_obj.hidden == False ):
      proc_cmd( self, "hide", [""] );
    else:
      proc_cmd( self, "show", [""] );
  return;


def mouse_event_move_slider( self, direction ):
# print "mouse_event_move_slider()";
  mouse_x = self.mouse_x;
  mouse_y = self.mouse_y;
  delta_x = abs( self.mouse_btn1dn_x-self.mouse_btn1up_x ) / self.txt_width;
# if ( delta_x == 0 and direction == 0 ):
  if ( True ):
    x1 = self.sig_value_start_x;
    x2 = self.sig_value_stop_x;
    mouse_x -= self.slider_width / 2;# Put Center of Slider on Mouse
#   if ( mouse_x > x1 and mouse_x < x2 ):
    if ( True ):
      self.sample_start = int(self.max_samples * ( mouse_x - x1 ) / ( x2-x1 ));
      # Prevent scrolling too far to right or left
      if ( self.sample_start + self.sample_room > self.max_samples ):
        self.sample_start = int(self.max_samples - self.sample_room);
      if ( self.sample_start < 0 ):
        self.sample_start = 0;
  else:
    None; # Dont support dragging slider
  screen_refresh( self );
  return;


def mouse_event_single_click( self ):
  proc_cmd( self, "cursor_snap" , [str(self.mouse_x),str(self.mouse_y)] );
  return;

#     print( self.popup_x );
def mouse_event_zoom_scroll( self, direction ):
  ( sample, Null )  = get_sample_at_mouse( self, self.mouse_x, self.mouse_y );
  # Mouse zoom in or out depending on wheel direction
  if ( direction == +1 ):
    new_zoom_x = self.zoom_x * 2.00;
    if ( new_zoom_x > 100 ):
      new_zoom_x = 100.0;# Don't ZoomIn too far
    set_zoom_x( self, new_zoom_x );
  else:
    # Warning routine appears twice, here and again for zoom_out command
    # search on ZOOM_OUT. 2017.09.21
    # Make sure there are enough samples to our right, if not recalculate
    # the center sample position so that the zoom_out fits within avail samples
    sample_room = self.sample_room * 2.00;
    sample = self.sample_start - sample_room // 4;
    if ( ( sample + sample_room ) > self.max_samples ):
      sample = self.max_samples - sample_room;
    if ( sample < 0 ): sample = 0;
    self.sample_start = int( sample );
    set_zoom_x( self, self.zoom_x / 2.0 );
    screen_refresh( self );
    return;
#   proc_cmd( self, "zoom_full", [] );

#   if ( self.stop_zoom == False ):
#     new_zoom_x = self.zoom_x / 2.00;
#     set_zoom_x( self, new_zoom_x );

  # Now see what sample is at the mouse position and adjust start accordingly
  # so that original sample is still under the mouse
  (new_sample,Null)=get_sample_at_mouse( self, self.mouse_x, self.mouse_y );
  sample_offset = int( sample - new_sample );
  self.sample_start += sample_offset;
  if ( self.sample_start < 0 ):
    self.sample_start = 0;
  screen_refresh( self );
  return;


def mouse_event_horizontal_drag( self, direction ):
# print "mouse_event_horizontal_drag()";
  return;
# # Support dragging the value region left or right to pan a number of samples
# self.sample_start += int( direction );
# # Prevent scrolling too far to right
# if ( self.sample_start + self.sample_room > self.max_samples ):
#   self.sample_start = int( self.max_samples - self.sample_room );
# if ( self.sample_start < 0 ):
#   self.sample_start = 0;
# screen_refresh( self );
# return;


def mouse_event_move_cursor( self ):
  (self.mouse_x,self.mouse_y) = self.pygame.mouse.get_pos();
  ( sample, Null )  = get_sample_at_mouse( self, self.mouse_x, self.mouse_y );
  self.curval_surface_valid = False;# curval surface is invalid when cur move
# for cur_obj in self.cursor_list:
#   if ( self.mouse_btn1dn_y > cur_obj.y and \
#        self.mouse_btn1dn_y < cur_obj.y+self.txt_height ):
#     if ( sample < 0 ): sample = 0;
#     if ( sample > self.max_samples ): sample=self.max_samples;
#     cur_obj.sample = int( sample );
#     for each in self.cursor_list:
#       each.selected = False;# Make both cursors unselected
#     cur_obj.selected = True;# now select the current cursor only
#     screen_refresh( self );
# return;

  # Determine which cursor is closest to mouse click and move that one.
  # If the cursors are close to each other, use Y location of mouse to 
  # determine which cursor the user is pointing to, else use closest X.
  # HERE
  c1_y = self.cursor_list[0].y;
  c2_y = self.cursor_list[1].y;
  c1_x = self.cursor_list[0].x;
  c2_x = self.cursor_list[1].x;
# if ( abs( c1_x - c2_x ) > 50 ):
#   if ( abs( self.mouse_btn1dn_x - c1_x ) < \
#        abs( self.mouse_btn1dn_x - c2_x )     ):
#     cur_obj = self.cursor_list[0];
#   else:
#     cur_obj = self.cursor_list[1];
# else:
#   if ( abs( self.mouse_btn1dn_y - c1_y ) < \
#        abs( self.mouse_btn1dn_y - c2_y )     ):
#     cur_obj = self.cursor_list[0];
#   else:
#     cur_obj = self.cursor_list[1];
  if ( self.mouse_btn1dn_y < c1_y+self.txt_height ):
    cur_obj = self.cursor_list[0];
  else:
    cur_obj = self.cursor_list[1];

  if ( sample < 0 ): sample = 0;
  if ( sample > self.max_samples ): sample=self.max_samples;
  cur_obj.sample = int( sample );
  for each in self.cursor_list:
    each.selected = False;# Make both cursors unselected
  cur_obj.selected = True;# now select the current cursor only
  screen_refresh( self );
  return;


def mouse_event_vertical_drag_wip( self ):
# print "mouse_event_vertical_drag_wip()";
  # Reorder the signal list when a signal name is dragged
  if ( True ):
    x1 = self.sig_name_start_x;
    x2 = self.sig_name_stop_x;
    sig_obj = get_sig_obj_at_mouse( self, self.mouse_x, self.mouse_y );
    if ( sig_obj != None ):
    # Draw a horizontal line at insert point before button is released
      y1 = sig_obj.y - 1; y2 = y1;
#     flush_surface_cache( self );
      screen_erase( self );
      draw_screen( self );
      self.pygame.draw.line(self.screen,self.color_fg,(x1,y1),(x2,y2),1);
      screen_flip( self );
  return;


# TODO: This doesnt handle invisible signals
def mouse_event_vertical_drag_done( self, direction  ):
# print "mouse_event_vertical_drag_done()";
  # Reorder the signal list when a signal name is dragged
  if ( True ):
    (Null , index_dn )  = get_sample_at_mouse( self, self.mouse_btn1dn_x,
                                                     self.mouse_btn1dn_y  );
    (Null , index_up )  = get_sample_at_mouse( self, self.mouse_btn1up_x,
                                                     self.mouse_btn1up_y  );
    if ( index_up > index_dn ):
      index_up -= 1;# Need to adjust this
    print( "index_up = " + str( index_up ));
    print( "index_dn = " + str( index_dn ));
    if ( index_up != None and index_dn != None ):
      self.signal_list.insert( index_up, self.signal_list.pop( index_dn ) );
    flush_surface_cache( self );
    screen_refresh( self );
  return;


def mouse_event_area_drag_wip( self ):
# print "mouse_event_area_drag_wip()";
  if ( self.mouse_btn1dn_x > self.sig_value_start_x ):
    # Draw a rectangle over the drag region to be zoomed in on
    x1 = self.mouse_btn1dn_x;
    y1 = self.mouse_btn1dn_y;
    w  =    ( self.mouse_x - self.mouse_btn1dn_x );
    h  =    ( self.mouse_y - self.mouse_btn1dn_y );
    screen_erase( self );
    self.pygame.draw.rect( self.screen, self.color_fg,(x1,y1,w,h), 1);
    draw_screen( self );
    screen_flip( self );
  return;


def mouse_event_area_drag_done( self ):
# print "mouse_event_area_drag_done()";
  if ( self.mouse_btn1dn_x > self.sig_value_start_x ):
    (s1,Null)=get_sample_at_mouse(self,self.mouse_btn1dn_x,self.mouse_btn1dn_y);
    (s2,Null)=get_sample_at_mouse(self,self.mouse_btn1up_x,self.mouse_btn1up_y);
    s1 = int( s1 );
    s2 = int( s2 );
    if ( s1 > s2 ):
      s1,s2 = s2,s1;# Swap so that s1 is always smallest of the two
    proc_cmd( self, "zoom_to" , [str(s1),str(s2)] );# 
  return;


###############################################################################
# Given a mouse position, return the section of screen that it is in
# as a txt string "signal_name","signal_value","cursor","slider"
def get_mouse_region( self, mouse_x, mouse_y ):
  if ( self.popup_x != None ):
    return ""; # No region if a popup is open

  if ( mouse_x > self.sig_name_start_x  and 
       mouse_x < self.sig_name_stop_x   and 
       mouse_y > self.sig_name_start_y  and 
       mouse_y < self.sig_name_stop_y+self.txt_height  ):
    # See if Click in the net name "[+]" region
#   flush_surface_cache( self );
    sig_obj = get_sig_obj_at_mouse( self, mouse_x, mouse_y );
    if ( sig_obj != None ):
      txt1 = self.font.render( "  ",True,self.color_fg, self.color_bg);
      txt2 = self.font.render( "[+]",True,self.color_fg, self.color_bg);
      if ( mouse_x > self.sig_name_start_x and 
           mouse_x < ( ( self.sig_name_start_x                 ) + \
                       ( sig_obj.hier_level * txt1.get_width() ) + \
                       ( txt2.get_width()                      )     ) ):
        return "signal_expand";
      else:
        return "signal_name";
  if ( mouse_x > self.sig_value_start_x and
       mouse_x < self.sig_value_stop_x  and
       mouse_y > self.sig_value_start_y and
       mouse_y < self.sig_value_stop_y+self.txt_height ):
    return "signal_value";
  if ( mouse_x > self.sig_name_stop_x  and
       mouse_x < self.sig_value_start_x and
       mouse_y > self.sig_value_start_y and
       mouse_y < self.sig_value_stop_y+self.txt_height ):
    return "scroll_left";
  if ( mouse_x > self.sig_value_stop_x and
       mouse_y > self.sig_value_start_y and
       mouse_y < self.sig_value_stop_y+self.txt_height ):
    return "scroll_right";
  if ( mouse_x > self.sig_value_start_x and
       mouse_x < self.sig_value_stop_x  and
#      mouse_y > self.cursor_start_y    and 
# HERE
#      mouse_y > self.sig_value_stop_y+self.txt_height and
       mouse_y < self.cursor_stop_y+self.txt_height    ):
    return "cursor";
  if ( mouse_x > self.sig_value_start_x and
       mouse_x < self.sig_value_stop_x  and
       mouse_y > self.cursor_stop_y         ):
    return "slider";
  return "";


###############################################################################
# direction -1=Backwards,+1=Forwards,0=Both
# value= Binary, Hex or "edge" ( transition )
# Returns sample number
def search_values( self, sig_obj, sample_start, search_value, direction ):
  sample = sample_start; # RTS Default for it search_value not found
  if ( sig_obj ):
    done_right = False;
    done_left  = False;
    if ( direction == -1 ):
      done_right = True;
    if ( direction == +1 ):
      done_left  = True;
    i = 0;
    last_value_right = sig_obj.values[ sample_start ];
    last_value_left  = sig_obj.values[ sample_start ];
    while ( done_right == False or done_left == False ):
      i += 1;
      if ( sample_start + i <  len( sig_obj.values ) ):
        value_right = sig_obj.values[ sample_start + i ];
        if ( ( search_value == value_right ) or  
             ( search_value == "edge" and value_right != last_value_right ) ):
          done_right = True;
          sample = sample_start + i;
        last_value_right = value_right;
      else:
        done_right = True;
      if ( sample_start - i >= 0 ):
        value_left  = sig_obj.values[ sample_start - i ];
        if ( ( search_value == value_left ) or  
             ( search_value == "edge" and value_left != last_value_left ) ):
          done_left = True;
          sample = sample_start - i;
        last_value_left  = value_left;
      else:
        done_left  = True;
  return sample;


###############################################################################
# Given position of mouse, return the sample number, and signal index 
def get_sample_at_mouse( self, mouse_x, mouse_y ):
  x = mouse_x - self.sig_value_start_x;    
  sample_num = int( ( x / self.zoom_x ) + self.sample_start );  
  signal_index = None;
  for ( i , sig_obj ) in enumerate( self.signal_list ):
    if ( sig_obj.visible == True ):
      if ( mouse_y > sig_obj.y and  \
           mouse_y < sig_obj.y + sig_obj.h ):
        signal_index = i;
  return ( sample_num, signal_index );

###############################################################################
# Given position of mouse, return the sig_obj 
def get_sig_obj_at_mouse( self, mouse_x, mouse_y ):
  for sig_obj in self.signal_list:
    if ( sig_obj.visible == True ):
      if ( mouse_y > sig_obj.y and  \
           mouse_y < sig_obj.y + sig_obj.h ):
        return sig_obj;
  return None;


###############################################################################
# Given name of a sig_obj, return that sig_obj
def get_sig_obj_by_name( self, name ):
  for sig_obj in self.signal_list:
    if ( sig_obj.name == name ):
      return sig_obj;
  return None;



###############################################################################
def log( self, txt_list ):
  for each in txt_list:
#   print( "log() :" + str( each ));
    self.file_log.write( str(each) + "\n" );
#   draw_header( self, each );
  return;


###############################################################################
def init_help( self ):
  a = [];
  a+=["#####################################################################"];
  a+=["# SUMP2 BlackMesaLabs  GNU GPL V2 Open Source License. Python 3.x   #"];
  a+=["# (C) Copyright 2017 Kevin M. Hubbard - All rights reserved.        #"];
  a+=["#####################################################################"];
  a+=["# bd_shell Commands                                                 #"];
  a+=["#   env                : Display all assigned variables and values  #"];
  a+=["#   print var          : Display value of variable 'var'            #"];
  a+=["#   foo = bar          : Assign 'bar' to the variable 'foo'         #"];
  a+=["#   var_bs foo bar     : Set bits 'bar' inside variable 'foo'       #"];
  a+=["#   var_bc foo bar     : Clear bits 'bar' inside variable 'foo'     #"];
  a+=["#   help               : Display help page ( you're looking at it ) #"];
  a+=["#   quit               : Quit the SUMP2 application                 #"];
  a+=["#   gui or NULL<ENTER> : Return from BD_SHELL to GUI Interface      #"];
  a+=["#   source filename    : Source an external command script          #"];
  a+=["#   sleep,sleep_ms  n  : Pause of n seconds or milliseconds         #"];
  a+=["# UNIX Commands                                                     #"];
  a+=["#   pwd,mkdir,cd,ls,cp,vi                                           #"];
  a+=["# Backdoor Commands                                                 #"];
  a+=["#   w addr data        : Write data to addr                         #"];
  a+=["#   w addr data data   : Write multiple dwords                      #"];
  a+=["#   r addr             : Read  data from addr                       #"];
  a+=["#   r addr dwords      : Read multiple dwords starting at addr      #"];
  a+=["# GUI Commands                                                      #"];
  a+=["#   crop_to_cursors    : Minimize sample set to cursor region       #"];
  a+=["#   save_png           : Save current screen image to PNG file      #"];
  a+=["#   save_vcd           : Save current capture to VCD file           #"];
  a+=["#   bd_shell           : Switch from GUI to a bd_shell CLI          #"];
  a+=["# SUMP Commands                                                     #"];
  a+=["#   sump_arm  timeout  : Arm SUMP2 engine and wait for timeout sec  #"];
  a+=["#   sump_arm_rle n     : Arm SUMP2 engine and wait for n seconds    #"];
  a+=["#   sump_stop          : Stop the SUMP2 engine                      #"];
  a+=["#   sump_status        : Display status of SUMP2 engine             #"];
  a+=["#   acquire_single     : Arm for Single non-RLE acquisition         #"];
  a+=["#   acquire_continuous : Arm for non-RLE acquisition and loop       #"];
  a+=["#   acquire_stop       : Stop any pending arming                    #"];
  a+=["#   acquire_rle_1x     : Arm for RLE acquisition plus dword data    #"];
  a+=["#   acquire_rle_8x     : Arm for RLE acquisition,  8x decimated     #"];
  a+=["#   acquire_rle_64x    : Arm for RLE acquisition, 64x decimated     #"];
  a+=["#####################################################################"];
  return a;

###############################################################################
def init_manual( self ):
  a = [];
  a+=["#####################################################################"];
  a+=["# SUMP2 by BlackMesaLabs  GNU GPL V2 Open Source License. Python 3.x "];
  a+=["# (C) Copyright 2017 Kevin M. Hubbard - All rights reserved.         "];
  a+=["#####################################################################"];
  a+=["1.0 Scope                                                            "];
  a+=[" This document describes the SUMP2 software and hardware.            "];
  a+=["                                                                     "];
  a+=["2.0 Software Architecture                                            "];
  a+=[" The SUMP2 application is a Python 3.5 script using the PyGame module"];
  a+=[" for mouse and graphical user interface. Communication to hardware is"];
  a+=[" via TCP Socket communications to a BD_SERVER.py instance. The SW is "];
  a+=[" architected as a GUI wrapper around a command line application with "];
  a+=[" a bd_shell interface.  When the PyGame GUI is used, mouse menu      "];
  a+=[" selections create commands that are then interpreted by bd_shell.   "];
  a+=[" In theory, sump2.py may be executed without PyGame as a command line"];
  a+=[" only program to arm the sump2 hardware and then dump captured data  "];
  a+=[" to a VCD file for offline viewing by another application.           "];
  a+=["                                                                     "];
  a+=["3.0 Command Descriptions                                             "];
  a+=[" Zoom_In            : Increase signal view magnification 2x          "];
  a+=[" Zoom_Out           : Decrease signal view magnification 2x          "];
  a+=[" Zoom_Full          : View all signal samples : WARNING May be slow  "];
  a+=[" Zoom_Previous      : Return to previous zoom view.                  "];
  a+=[" Zoom_to_Cursors    : View region bound by cursors                   "];
  a+=[" Crop_to_Cursors    : Reduce sample set to region bound by cursors   "];
  a+=[" Crop_to_INI        : Reduce sample set to region bound by INI file  "];
  a+=[" Cursors_to_View    : Bring both cursors into current view           "];
  a+=[" Cursor1_to_Here    : Bring Cursor1 to mouse pointer                 "];
  a+=[" Cursor2_to_Here    : Bring Cursor2 to mouse pointer                 "];
  a+=[" Acquire_Normal     : Arm for non-RLE acquisition and wait for trig  "];
  a+=[" Acquire_RLE        : Arm for RLE acquisition and wait for trigger   "];
  a+=[" Acquire_RLE_Lossy  : Arm for RLE Lossy acquisition and wait for trig"];
  a+=[" Arm_Normal         : Arm for non-RLE acquisition.                   "];
  a+=[" Arm_RLE            : Arm for RLE acquisition.                       "];
  a+=[" Download_Normal    : Download acquisition from Arm_Normal           "];
  a+=[" Download_RLE       : Download acquisition from Arm_RLE              "];
  a+=[" Download_RLE_Lossy : Download acquisition from Arm_RLE              "];
  a+=[" Acquire_Stop       : Abort an Arming                                "];
  a+=[" Acquire_Continuous : Arm hardware for looping non-RLE acquisitions  "];
  a+=[" Load_VCD           : View the specified VCD file                    "];
  a+=[" Save_VCD_Full      : Save entire acquisition to VCD file            "];
  a+=[" Save_VCD_Cursors   : Save Cursor Region to VCD file                 "];
  a+=[" List_View          : View selected signals in a text file           "];
  a+=[" File_Load          : Load a bd_shell script file                    "];
  a+=[" File_Save          : Save capture to a VCD,PNG,JPG, etc file        "];
  a+=[" Save_Rename        : Rename the last file saved                     "];
  a+=[" Font_Larger        : Increase GUI font size                         "];
  a+=[" Font_Smaller       : Decrease GUI font size                         "];
  a+=[" BD_SHELL           : Close GUI and open a BD_SHELL command line     "]; 
  a+=["                                                                     "];
  a+=[" Rename             : Rename a selected signal's nickname            "]; 
  a+=[" Insert_Divider     : Insert a dummy signal divider                  "]; 
  a+=[" Clipboard          : Cut and Paste selected signals                 "]; 
  a+=[" Visibility         : Change visibility. Impacts RLE Compression     "]; 
  a+=[" Trigger_Rising     : Set Trigger for Rising edge of selected        "]; 
  a+=[" Trigger_Falling    : Set Trigger for Falling edge of selected       "]; 
  a+=[" Trigger_Watchdog   : Set Trigger for Watchdog timeout of selected   "]; 
  a+=[" Set_Pattern0       : Advanced Triggering                            "]; 
  a+=[" Set_Pattern1       : Advanced Triggering                            "]; 
  a+=[" Clear_Pattern_Match: Advanced Triggering                            "]; 
  a+=[" Set_Data_Enable    : Advanced data sampling                         "]; 
  a+=[" Clear_Data_Enable  : Advanced data sampling                         "]; 
  a+=[" SUMP_Configuration : Modify advanced SUMP variables                 "]; 
  a+=[" Acquisition_Length : Configure amount of non-RLE RAM to use         "]; 
  a+=["                                                                     "];
  a+=["4.0 SUMP2 Environment Variables                                      "];
  a+=[" bd_connection           : Connection type to hardware. tcp only     "];
  a+=[" bd_protocol             : Communication protocol to HW, poke only   "];
  a+=[" bd_server_ip            : IP address or localhost for bd_server     "];
  a+=[" bd_server_socket        : Socket Number for bd_server, 21567 typ"];
  a+=[" sump_addr               : 32bit PCI address of sump_ctrl_reg        "];
  a+=[" sump_data_enable        : Event bits to use for data_enable feature "];
  a+=[" sump_rle_event_en       : Event bits to use for RLE capture         "];
  a+=[" sump_rle_post_trig_len  : Max number of post trigger RLE samples    "];
  a+=[" sump_rle_pre_trig_len   : Max number of pre trigger RLE samples     "];
  a+=[" sump_trigger_delay      : Number of clocks to delay trigger         "];
  a+=[" sump_trigger_field      : Event bits to use for trigger             "];
  a+=[" sump_trigger_nth        : nTh trigger to trigger on                 "];
  a+=[" sump_trigger_type       : or_rising,or_falling,watchdog,pattern_ris "];
  a+=[" sump_user_ctrl          : 32bit user_ctrl field                     "];
  a+=[" sump_user_pattern0      : 32bit user pattern0 field                 "];
  a+=[" sump_user_pattern1      : 32bit user pattern1 field                 "];
  a+=[" sump_watchdog_time      : Watchdog timeout for Watchdog trigger     "];
  a+=[" sump_rle_gap_remove     : For RLE_LOSSY, replace any RLE gaps that  "];
  a+=["                           exceed N clocks with M gap_replace clocks."];
  a+=[" sump_rle_gap_replace    : For RLE_LOSSY, see sump_rle_gap_remove,   "];
  a+=["                           this is M value to replace the N gaps.    "];
  a+=[" sump_rle_autocull       : If 1, remove any RLE dead time at front   "];
  a+=["                           or end of RLE acquisition samples.        "];
  a+=["                                                                     "];
  a+=["5.0 SUMP2 Hardware                                                   "];
  a+=[" The SUMP2 hardware is a single verilog file with fixed input parms  "];
  a+=[" for the depth and width of capture memory to use. A maximum SUMP2   "];
  a+=[" configuration contains a 32bit Block RAM for non-RLE events and a   "];
  a+=[" 64bit Block RAM for RLE events and time stamps. In addition to 32   "];
  a+=[" signal events, SUMP2 may also capture 16 DWORDs (512 bits ) of non  "];
  a+=[" RLE data. The SUMP2 software automatically adjusts to each instance "];
  a+=[" of hardware for memory depth, width and advanced features. A key    "];
  a+=[" feature for acquiring long captures in time is the ability to mask  "];
  a+=[" any of the event inputs, which can be used to dramatically reduce   "];
  a+=[" event occurrence and support capturing only events of interest. The "];
  a+=[" software supports masking events by double-clicking the signal name "];
  a+=[" prior to arming which hides the signals and masks them from the RLE "];
  a+=[" compression. 10x to 1000x compression is possible run-time for some "];
  a+=[" designs by dynamically masking input events prior to acquisition.   "];
  a+=["                    ---------------                                  "];
  a+=["  events[31:0] -+->| Trigger Logic |-------------------------        "];
  a+=["                |   ---------------      -----------------   |       "];
  a+=["                +---------------------->| RLE Compression |  |       "];
  a+=["                |   ---------------     | Timestamp and   |<-+       "];
  a+=["                +->|   RLE RAM     |<---| Addr Generator  |  |       "];
  a+=["                |   ---------------      -----------------   |       "];
  a+=["                |   ---------------      -----------------   |       "];
  a+=["                 ->| non-RLE RAM   |<-+-| Addr Generator  |<-        "];
  a+=["                    ---------------   |  -----------------           "];
  a+=["                    ---------------   |                              "];
  a+=["  dwords[511:0] -->| non-RLE RAM   |<-                               "];
  a+=["                    ---------------                                  "];
  a+=["                                                                     "];
  a+=["6.0 Working with large RLE datasets                                  "];
  a+=[" RLE datasets can be overwhelming large to work with in software once"];
  a+=[" samples have been decompressed. Compression ratios of 10,000:1 are  "];
  a+=[" possible for some systems. SUMP Software provides internal tools for"];
  a+=[" reducing the hardware captured RLE dataset to more manageable size  "];
  a+=[" for both viewing and VCD generation.                                "];
  a+=["  crop_to_cursors : Permanently crops the number of samples to a     "];
  a+=["                    region indicated by the cursors.                 "];
  a+=["  Signal Hiding   : Hiding a signal prior to acquisition will mask   "];
  a+=["                    the signal entirely and increase the overall RLE "];
  a+=["                    acquisition length. Hiding a signal post acquire "];
  a+=["                    speeds up rendering time for remaining signals.  "];
  a+=["                                                                     "];
  a+=[" 6.1 Bundles                                                         "];
  a+=["   The following is an example of manually modifying sump2_wave.txt  "];
  a+=["   file in order to group together multiple events into a bundle.    "];
  a+=["   /my_cnt -bundle -hex                                              "];
  a+=["     /event[12]  -nickname event[12]                                 "];
  a+=["     /event[13]  -nickname event[13]                                 "];
  a+=["     /event[14]  -nickname event[14]                                 "];
  a+=["     /event[15]  -nickname event[15]                                 "];
  a+=["                                                                     "];
  a+=["7.0 History                                                          "];
  a+=[" The original OSH+OSS SUMP was designed in 2007 as an external logic "];
  a+=[" logic analyzer using a Xilinx FPGA eval board for capturing external"];
  a+=[" electrical signals non compressed to all available FPGA block RAM.  "];
  a+=[" See http://www.sump.org/projects/analyzer/                          "];
  a+=[" The original developer published the serial communication protocol  "];
  a+=[" and also wrote a Java based waveform capture tool. The simplicity of"];
  a+=[" the protocol and the quality and maintenance of the Open-Source Java"];
  a+=[" client has inspired many new SUMP compliant projects such as:       "];
  a+=[" 'Open Logic Sniffer' : https://www.sparkfun.com/products/9857       "];
  a+=["                                                                     "];
  a+=[" 7.1 SUMP1-RLE ( 2014 )                                              "];
  a+=["  Black Mesa Labs developed the SUMP1-RLE hardware in 2014 as a      "];
  a+=["  software protocol compatible SUMP engine that was capable of real  "];
  a+=["  time hardware compression of samples ( Run Length Encoded ). The   "];
  a+=["  idea of the project was to leverage the open-source Java software  "];
  a+=["  and couple it with new hardware IP that was capable of storing deep"];
  a+=["  capture acquisitions using only a single FPGA Block RAM, allowing  "];
  a+=["  SUMP to be used internally with existing FPGA designs rather than  "];
  a+=["  a standalone device. FPGA vendor closed license logic analyzers all"];
  a+=["  store using no compression requiring vast amount of Block RAMS to  "];
  a+=["  be useful and typically do not fit will within the limited fabric  "];
  a+=["  resources of an existing FPGA design requiring debugging. SUMP1-RLE"];
  a+=["  was later enhanced to include 2 DWORDs of sampled data along with  "];
  a+=["  the RLE compressed signal events. This enhancement required new    "];
  a+=["  software which was written in .NET Powershell for Windows platform."];
  a+=["                                                                     "];
  a+=[" 7.2 SUMP2-RLE ( 2016 )                                              "];
  a+=["  SUMP2 is a software and hardware complete redesign to improve upon "];
  a+=["  the SUMP1-RLE concept. For SUMP2 the .NET software was tossed due  "];
  a+=["  to poor user interface performance and replaced with a PyGame based"];
  a+=["  VCD waveform viewer ( chip_wave.py also from BML ). The SUMP2 HW   "];
  a+=["  is now a single Verilog file with no backwards compatibility with  "];
  a+=["  any legacy SUMP hardware or software systems. SUMP2 hardware is    "];
  a+=["  designed to capture 512bits of DWORDs and 32bits of events versus  "];
  a+=["  the SUMP1 limits of 16 event bits and 64bits of DWORDs. Sample     "];
  a+=["  depth for SUMP2 is now completely defined by a hardware instance   "];
  a+=["  with software that automatically adapts.  The RLE aspect of SUMP2  "];
  a+=["  is optional and not required for simple data intensive captures.   "];
  a+=["  SUMP2 software includes bd_shell support for changing variables    "];
  a+=["  on the fly and providing simple low level hardware access to regs. "];
  a+=["                                                                     "];
  a+=["8.0 BD_SERVER.py                                                     "];
  a+=["  The SUMP2.py application does not communicate directly to hardware "];
  a+=["  but instead uses BD_SERVER.py as an interface layer. BD_SERVER is  "];
  a+=["  a multi use server application that accepts requests via TCP to    "];
  a+=["  read and write to low level hardware and then translates those     "];
  a+=["  requests using one of many low level hardware protocols available. "];
  a+=["  BD_SERVER allows the low level communications to easily change from"];
  a+=["  interfaces like USB FTDI serial to PCI without requiring any change"];
  a+=["  to the high level application. This interface also supports the    "];
  a+=["  debugging of an embedded system from a users regular desktop with  "];
  a+=["  a standard Ethernet or Wifi connection between the two. Typical use"];
  a+=["  is to run both python applications on same machine and use the TCP "];
  a+=["  localhost feature within the TCP stack for communications.         "];
  a+=["                                                                     "];
  a+=["    ------------           --------------           ---------------  "];
  a+=["   |  sump2.py  |<------->| bd-server.py |<------->| SUMP Hardware | "];
  a+=["    ------------  Ethernet --------------  USB,PCI  ---------------  "];
  a+=["                                                                     "];
  a+=["9.0 License                                                          "];
  a+=[" This hardware and software is released under the GNU GPLv2 license. "];
  a+=[" Full license is available at http://www.gnu.org                     "];
  a+=["                                                                     "];
  return a;


###############################################################################
def init_vars( self, file_ini ):
  # Load App Variables with Defaults.
  vars   = {}; # Variable Dictionary
  vars["font_name"] = "dejavusansmono";
  vars["font_size"] = "12";
  vars["file_in"] = "dut.vcd";
  vars["file_log"] = "sump2_log.txt";
  vars["color_screen_background"] = "000000";
  vars["color_screen_foreground"] = "00FF00";
  vars["screen_width"]  = "800";
  vars["screen_height"] = "600";
  vars["cursor_unit"] = "clocks";
# vars["cursor_unit"] = "samples";
  vars["cursor_mult"] = "1.0";
  vars["sump_crop_pre_trig_len"]  = "00040000";
  vars["sump_crop_post_trig_len"] = "00001000";
  vars["sump_macro_1"]  = "Acquire_RLE";
  vars["sump_macro_2"]  = "Acquire_RLE_Lossy";
  vars["sump_macro_3"]  = "Acquire_Normal";
  vars["sump_macro_4"]  = "Quit";
  vars["sump_macro_5"]  = "Crop_to_INI";
  vars["sump_macro_6"]  = "Crop_to_Cursors";
  vars["sump_macro_7"]  = "Save_PNG";
  vars["sump_macro_8"]  = "Save_VCD";
  vars["sump_macro_9"]  = "Zoom_to_Cursors";
  vars["sump_macro_0"]  = "Cursors_to_View";

  vars["sump_macro_F1"]  = "Acquire_RLE";
  vars["sump_macro_F2"]  = "Acquire_RLE_Lossy";
  vars["sump_macro_F3"]  = "Acquire_Normal";
  vars["sump_macro_F4"]  = "Quit";
  vars["sump_macro_F5"]  = "Crop_to_INI";
  vars["sump_macro_F6"]  = "Crop_to_Cursors";
  vars["sump_macro_F7"]  = "Save_PNG";
  vars["sump_macro_F8"]  = "Save_VCD";
  vars["sump_macro_F9"]  = "Zoom_to_Cursors";
  vars["sump_macro_F10"] = "Cursors_to_View";
  vars["sump_macro_F11"] = "Cursor1_to_Here";
  vars["sump_macro_F12"] = "Cursor2_to_Here";

  vars["bd_connection"         ] = "tcp";
  vars["bd_protocol"           ] = "poke";
  vars["bd_server_ip"          ] = "localhost";
  vars["bd_server_socket"      ] = "21567";
  vars["uut_name"              ] = "UUT";
# vars["sump_addr"             ] = "00000090" ;# Addr of sump2_ctrl_reg
  vars["sump_addr"             ] = "00000010" ;# Addr of sump2_ctrl_reg
  vars["sump_script_startup"   ] = "sump2_startup.txt";
  vars["sump_script_shutdown"  ] = "sump2_shutdown.txt";
  vars["sump_script_inc_filter"] = "*.txt";
  vars["sump_script_exc_filter"] = "sump2_*.txt";
  vars["sump_trigger_type"     ] = "or_rising";
  vars["sump_trigger_field"    ] = "00000000";
  vars["sump_trigger_delay"    ] = "0000";
  vars["sump_trigger_nth"      ] = "0001";
  vars["sump_acquisition_len"  ] = "44";
  vars["sump_rle_autocull"     ]   = "1";
  vars["sump_rle_gap_remove"   ]   = "1024";
  vars["sump_rle_gap_replace"  ]   = "16";
  vars["sump_rle_event_en"     ] = "FFFFFFFF";
  vars["sump_rle_pre_trig_len" ] = "00010000";
  vars["sump_rle_post_trig_len"] = "00010000";
  vars["sump_user_ctrl"        ] = "00000000";
  vars["sump_user_pattern0"    ] = "00000000";
  vars["sump_user_pattern1"    ] = "00000000";
  vars["sump_data_enable"      ] = "00000000";
  vars["sump_watchdog_time"    ] = "00001000";
# vars["sump_rle_undersample"]   = "10";
#  return;

  import os;
  if os.path.exists( file_ini ):
    file_in   = open( file_ini, 'r' );
    file_list = file_in.readlines();
    file_in.close();
    for each in file_list:
      each = each.replace("="," = ");
      words = each.strip().split() + [None] * 4; # Avoid IndexError
      # foo = bar
      if ( words[1] == "=" and words[0][0:1] != "#" ):
        vars[ words[0] ] = words[2];
  else:
    print( "Warning: Unable to open " + file_ini);
  return vars;


###############################################################################
# Dump all the app variables to ini file when application quits.
def var_dump( self, file_ini ):
  log( self, ["var_dump()"] );
  file_out  = open( file_ini, 'w' );
  file_out.write( "# [" + file_ini + "]\n" );
  file_out.write( "# WARNING: \n");
  file_out.write( "#  This file is auto generated on application exit.\n" );
  file_out.write( "#  Safe to change values, but comments will be lost.\n" );
  txt_list = [];
  for key in self.vars:
    val = self.vars[ key ];
    txt_list.append( key + " = " + val + "\n" );
  for each in sorted( txt_list ):
    file_out.write( each );
  file_out.close(); 
  return;

def hexlist2file( self, file_name, my_list ):
  file_out  = open( file_name, 'w' );
  for each in my_list:
    file_out.write( "%08x\n" % each );
  file_out.close(); 
  return;

def list2file( self, file_name, my_list ):
  file_out  = open( file_name, 'w' );
  for each in my_list:
    file_out.write( each + "\n" );
  file_out.close(); 
  return;

def tuplelist2file( self, file_name, my_list ):
  file_out  = open( file_name, 'w' );
  for (dw1,dw2) in my_list:
    file_out.write("%08x %08x" % ( dw1,dw2 ) + "\n" );
  file_out.close(); 
  return;


###############################################################################
# Command Line BD_SHELL
def bd_shell( self, cmd_start = "" ):
  log( self, ["bd_shell()"] );
  import pygame;
  loop_jk = True;
  import msvcrt;# Note: Windows specific
  print("\nMode=BD_SHELL : Enter NULL command to return to GUI");
  self.context = "cli";
  pygame.display.quit();
  self.gui_active = False;
  print("");
  sys.stdout.write( self.prompt );
  sys.stdout.write( cmd_start   );
  sys.stdout.flush();
  h_cnt = 1;# Command history count
  key_buf = cmd_start;
  while ( loop_jk == True ):
    ch = msvcrt.getch();# Wait for single key press from DOS-Box
    if ( ch != "\xe0" ):
      ch = ch.decode();
    else:
      # K=Left,M=Right,H=Up,P=Down,G=Home,O=End
      ch = msvcrt.getch();# The Special KeyCode
      print( ch );
      ch = "";
#   print( ch );
#   ch = (msvcrt.getch().decode());# Wait for single key press from DOS-Box
#   print( ch );
    # Handle Backspace Erase
    if ( ch == chr(  8 ) ):
      sys.stdout.write( str( ch ) );# 
      sys.stdout.write( str(" " ) );# 
      sys.stdout.write( str( ch ) );# 
    else:
      sys.stdout.write( str( ch ) );# Echo typed character to DOS-Box STDOUT
    sys.stdout.flush();
    # If not <ENTER> key then append keypress to a key_buf string
    if ( ch != chr( 13 ) ):
      if ( ch == chr(  8 ) ):
        if ( len(key_buf) > 0 ):
          key_buf = key_buf[:-1];# Subtract last char on Backspace
      else:
        key_buf += str(ch);# Append new character     
    elif ( ch == chr( 13 ) ):
      if ( len( key_buf ) == 0 or key_buf == "gui" ):
        loop_jk = False;
      else:
        print( ("%d>"+key_buf+"     " ) % h_cnt ); h_cnt +=1;
        key_buf = key_buf.replace("="," = ");
        words = " ".join(key_buf.split()).split(' ') + [None] * 8;
        if ( words[1] == "=" ):
          cmd   = words[1];
          parms = [words[0]]+words[2:];
        else:
          cmd   = words[0];
          parms = words[1:];
        rts = proc_cmd( self, cmd, parms );
        for each in rts:
          print( each );
        key_buf = "";
        sys.stdout.write( self.prompt );# "bd>"
        sys.stdout.flush();
  # while ( loop_jk == True ):
  self.context = "gui";
  print("\nMode=GUI");
  # NOTE: set_mode prevents resizing after return to GUI.
  # pygame.display.set_mode();# Set focus back to GUI Window
  display_init( self );
  pygame.display.update();
  flush_surface_cache( self );# Redraw with new values
  draw_screen( self );
  screen_flip( self );
  sump_vars_to_signal_attribs( self );# Assume sump vars were modified
  return;


###############################################################################
# Process Backdoor commands for Writing and Reading to any hardware
def proc_bd_cmd( self, cmd, parms ):
  log( self, ["proc_bd_cmd() : " + cmd + " " + str( parms ) ] );
  rts = [];

  file_mode = None;
  if ( ">" in parms ):
    i = parms.index(">");
    file_mode = "w";# Create new file, overwriting existing
  if ( ">>" in parms ):
    i = parms.index(">>");
    file_mode = "a";# Append to any existing file

  if ( file_mode != None ):
    file_name = parms[i+1];
    file_out = open( file_name, file_mode ); # a or w : Append or Overwite 
    parms = parms[0:i] + [None]*10;# Strip "> foo.txt" prior to processing

# if ( cmd == "w" or cmd == "r" or cmd == "bs" or cmd == "bc" ):
  if ( cmd == "w" or cmd == "r" ):
    addr = parms[0];
    data = parms[1:];
    # Address may be a variable, so look 
    if ( self.vars.get( addr ) != None ):
      addr = self.vars[ addr ];

    if ( cmd == "w" ):
      data_hex = [];
      for each in data:
        if ( each != None ):
          data_hex += [int(each,16)];
      self.bd.wr( int(addr,16), data_hex );
    if ( cmd == "r" ):
      if ( data[0] == None ):
        num_dwords = 1;
      else:
        num_dwords = int( data[0],16 );
      rts = self.bd.rd( int(addr,16) , num_dwords, repeat = False ); 
#     data_hex = [];
#     for each in rts:
#       data_hex += ["%08x" % each];
#     rts = data_hex;
      # Format 8 dwords wide per line
      data_hex = "";
      i = 0;
      for each in rts:
        data_hex += ("%08x " % each );
        i += 1;
        if ( i == 8 ):
          i = 0;
          data_hex += "\n";
      rts = [ data_hex ];

  if ( file_mode != None ):
    for each in rts:
      file_out.write( each + "\n" );
    file_out.close();
    rts = [];

  return rts; 

###############################################################################
# Generate a demo vcd file to display if the hardware isn't present
def make_demo_vcd( self ):
  filename_vcd = "sump2_demo.vcd";
  txt2vcd = TXT2VCD();# Instantiate Class for the VCD Conversion

  # line-0 contains a list of all signal names and ends with clock period
  # Iterate the list and replace each signal name with its nickname
  new_line = "hsync vsync pixel_r pixel_g pixel_b 10000";
  h = 0; v = 597; sample_lines = [ new_line ];
  import random;
  rnd_list = [0]*1000;
  rnd_list += [1];
  pixel_r = 0;
  pixel_g = 0;
  pixel_b = 0;
  for i in range( 0, 10000, 1):
    h+=1;
    hsync = 0;
    vsync = 0;
    pixel_r = random.choice( rnd_list );
    pixel_g = random.choice( rnd_list );
    pixel_b = random.choice( rnd_list );
    if ( h >= 800 ):
      hsync = 1; 
    if ( h == 810 ):
      hsync = 1; h = 0;
      v += 1;
      if ( v == 600 ):
        v = 0;
    if ( v == 599 ):
      vsync = 1;
    sample = "%d %d %d %d %d" % ( hsync,vsync, pixel_r,pixel_g,pixel_b );
    sample_lines += [ sample ];
  vcd_lines = sample_lines[:];
  rts = txt2vcd.conv_txt2vcd( self, vcd_lines );
  print("Saving " + filename_vcd );
  file_out = open( filename_vcd, "w" ); # Append versus r or w
  for each in rts:
    file_out.write( each + "\n" );# Make Windows Friendly
  file_out.close();
  return filename_vcd;


###############################################################################
# Given a file_header ( like foo_ ), check for foo_0000, then foo_0001, etc 
# and return 1st available name.
def make_unique_filename( self, file_header, file_ext ):
  import os;
  num = 0;
  while ( True ):
    file_name = file_header + ( "%04d" % num ) + file_ext;
    if ( os.path.exists( file_name ) == False ):
      return file_name;
    else:
      num +=1;
  return None;
  
###############################################################################
# Read in a file and display it
def more_file( self, parms ):
  log( self, ["more_file() " + str( parms ) ] );
  file_name = parms[0];
  rts = [];
  try: # Read Input File 
    file_in = open( file_name , "r" );
    file_lines = file_in.readlines();
    file_in.close();  
#   rts = file_lines;
    rts = list(map(str.strip, file_lines));# Chomp all the lines
  except:
    print( "ERROR Input File: "+file_name);
    return;
  return rts;


def load_vcd( self, parms ):
  if ( self.signal_list_hw == None ):
    self.signal_list_hw = self.signal_list;# Support returning to HW view

  file_name = parms[0];
# self.bd=None;
  file2signal_list( self, file_name );# VCD is now a signal_list
  self.vcd_import = True;# Prevents saving a VCD specific sump2_wave.txt file
  self.vcd_name   = file_name;
  self.file_name = None; # Make sure we don't overwrite vcd with wave on exit
  proc_cmd( self, "zoom_to", ["0", str( self.max_samples ) ] );# Redraws
  return;

###############################################################################
# interpret a bd_shell script or wave file
# a wave file is easy to spot as 1st char on each line is a "/"
def source_file( self, parms ):
  log( self, ["source_file() " + str( parms ) ] );
  file_name = parms[0];
  rts = [];
  try: # Read Input File 
    file_in = open( file_name , "r" );
    file_lines = file_in.readlines();
    file_in.close();  
  except:
    print( "ERROR Input File: "+file_name);
    return;

  is_wave = False;
  for each in file_lines:
    words = " ".join(each.split()).split(' ') + [None] * 20;
    if ( words[0][0:1] == "/" ):
      is_wave = True;
 
  if ( is_wave == True ):
    load_format( self, file_name ); 
    self.name_surface_valid = False;
    screen_refresh( self );
  else:
    for each in file_lines:
      each = each.replace("="," = ");
      words = " ".join(each.split()).split(' ') + [None] * 20;
      if ( words[0][0:1] != "#" ):
        if ( words[1] == "=" ):
          cmd   = words[1];
          parms = [words[0]]+words[2:];
        else:
          cmd   = words[0];
          parms = words[1:];
        rts += proc_cmd( self, cmd, parms );
  return rts;
  

###############################################################################
# Process generic unknown commands ( GUI,Shell,Backdoor, SUMP )
def proc_cmd( self, cmd, parms ):
  log( self, ["proc_cmd() " + cmd + " " + str( parms )] );
#  print( cmd, parms );
  rts = [];
  if ( cmd == None ):
    return rts;
  cmd = cmd.lower();

  # !! retrieves last command
  if ( cmd == "!!" ):
    cmd = self.last_cmd;
  else:
    self.last_cmd = cmd;
  self.cmd_history.append([ cmd, parms ] );

  if ( cmd[0:1] == "!" ):
    try:
      h_num = cmd[1:];
      ( cmd, parms ) = self.cmd_history[ int(h_num,10) ];
    except:
      print("Invalid Command History");

# print "proc_cmd()", cmd;
  # Commands may have aliases, look them up here:
  if ( self.cmd_alias_hash_dict.get( cmd ) != None ):
    cmd = self.cmd_alias_hash_dict[ cmd ];

  # Returned all assigned variables with their values
  if ( cmd == "env" ):
    for key in self.vars:
      rts += [ key +"=" +  self.vars[key] ];
    return sorted(rts);

  elif ( cmd == "=" ):
    self.vars[parms[0]] = parms[1]; # Variable Assignment

  elif ( cmd == "var_bs" ):
    val = int( self.vars[parms[0]] , 16 );
    val = val | int( parms[1], 16 );
    self.vars[parms[0]] = ("%08x" % val );

  elif ( cmd == "var_bc" ):
    val = int( self.vars[parms[0]] , 16 );
    val = val & ~int( parms[1], 16 );
    self.vars[parms[0]] = ("%08x" % val );

  elif ( cmd == "echo" or cmd == "print" ):
    try:
      rts = [ self.vars[ parms[0] ] ];
    except:
      rts = [ parms[0] ];

  elif ( cmd == "h" or cmd == "history" ):
    rts = self.cmd_history;
#   rts for ( i , sig_obj ) in enumerate( self.signal_list ):

  elif ( cmd == "source" ):
    rts = source_file( self, parms );

  elif ( cmd == "load_vcd" ):
    rts = load_vcd( self, parms );

  elif ( cmd == "system" ):
    import os;
    txt = "";
    for each in parms:
      if ( each != None ):
        txt = txt + each + " ";
    os.system( txt );

  elif ( cmd == "gui_minimize" ):
    self.pygame.display.iconify();
#  elif ( cmd == "gui_maximize" ):
#    self.screen_width  = int( self.vars["screen_width"], 10 );
#    self.screen_height = int( self.vars["screen_height"], 10 );
#    self.screen=pygame.display.set_mode(
#       [ self.screen_width, self.screen_height ],
#       pygame.RESIZABLE | pygame.HWSURFACE | pygame.DOUBLEBUF );


  elif ( cmd == "more" ):
    rts = more_file( self, parms );

  elif ( cmd == "help" or cmd == "?" ):
    rts = self.help;# I'm a funny guy

  elif ( cmd == "list_view_cursors" or
         cmd == "list_view_full"      ):
    filename = make_unique_filename( self, "sump2_list_", ".txt" );
    sump_save_txt( self, filename, mode_list = True, cmd = cmd );
    try:
      import os;
      if ( self.os_sys == "Linux" ):
        os.system('vi ' + filename );
      else:
        os.system('notepad.exe ' + filename );
    except:
      rts += ["ERROR: "+cmd+" "+filename ];

  elif ( cmd == "manual" ):
    try:
      import os;
      filename = "sump2_manual.txt";
      if ( self.os_sys == "Linux" ):
        os.system('vi ' + filename );
      else:
        os.system('notepad.exe ' + filename );
    except:
      rts += ["ERROR: "+cmd+" "+filename ];

  elif ( cmd == "key_macros" ):
    try:
      import os;
      filename = "sump2_key_macros.txt";
      if ( self.os_sys == "Linux" ):
        os.system('vi ' + filename );
      else:
        os.system('notepad.exe ' + filename );
    except:
      rts += ["ERROR: "+cmd+" "+filename ];

  elif ( cmd == "bd_shell" ):
    bd_shell(self, cmd_start ="" );

  elif ( cmd == "load_fpga" ):
    load_fpga(self);

  elif ( cmd=="quit" or cmd=="exit" or cmd=="shutdown" or cmd=="reboot" ):
    self.done=True;
    if ( cmd == "quit" or cmd == "exit" ):
      shutdown( self, False,False );# Normal App exit to command line
    else:
      if ( cmd == "reboot" ):
        shutdown( self, True, True  );# Pi System Shutdown with Reboot
      else:
        shutdown( self, True, False );# Pi System Shutdown

  elif ( cmd == "sump_connect" ):
    sump_connect(self);

  elif ( "[" in cmd and
         "-" in cmd and
         "t" in cmd and
         "]" in cmd ):   
    words = cmd.split("t");
    pre_trig  = words[0].count("-");
    post_trig = words[1].count("-");
    acq_len = ( pre_trig << 4 ) + ( post_trig << 0 );
    self.vars["sump_acquisition_len"] = ( "%02x" % acq_len );
    print( "sump_acquisition_len = " + ( "%02x" % acq_len ));


# "Arm_Normal","Arm_RLE","Acquire_Download",],
  elif ( cmd == "acquire_download" or \
         cmd == "download_acquire" or \
         cmd == "download_normal"  or \
         cmd == "download_rle"     or \
         cmd == "download_acquire" or \
         cmd == "download_rle_lossy"    ):
    if ( self.acq_state == "arm_normal" ):
      self.acq_state = "acquire_single";
    elif ( self.acq_state == "arm_rle" ):
      self.acq_state = "acquire_rle";

    if ( cmd == "download_rle_lossy" ):
      self.acq_state = "download_rle_lossy";
    elif ( cmd == "download_rle" ):
      self.acq_state = "download_rle";
    elif ( cmd == "download_normal" ):
      self.acq_state = "download_normal";

  elif ( cmd == "sump_arm"           or
         cmd == "sump_arm_rle"       or
         cmd == "sump_stop"          or
         cmd == "acquire_single"     or
         cmd == "acquire_normal"     or
         cmd == "acquire_continuous" or
         cmd == "arm_normal"         or
         cmd == "arm_rle"            or
         "acquire_rle" in cmd        or     
         cmd == "acquire_stop"          ):
    if ( cmd == "sump_arm"           or
         cmd == "sump_arm_rle"       or
         cmd == "acquire_single"     or
         cmd == "acquire_normal"     or
         cmd == "acquire_continuous" or
         cmd == "arm_normal"         or
         cmd == "arm_rle"            or
         "acquire_rle" in cmd            ): 
      sump_arm(self, True );# Arm the hardware
#     if ( "acquire_rle" in cmd ): 
      if ( "rle_lossy" in cmd ): 
        self.acq_mode = "rle_lossy";
      if ( "rle" in cmd ): 
        self.acq_mode = "rle";
      else:
        self.acq_mode = "nonrle";
    else:
      sump_arm(self, False);# Cancel an acq in progress
    if ( cmd == "acquire_normal" ):
      cmd = "acquire_single";
    self.acq_state = cmd;
    # if sump_arm has a parm then this is CLI and is a seconds timeout
    if ( ( cmd=="sump_arm" or cmd=="sump_arm_rle" ) and parms[0] != None ):
      timeout = int( parms[0], 16 );
      # Loop until timeout or acquired bit is set
      while ( timeout > 0 and 
              ( self.sump.rd( addr = None )[0] & 
                self.sump.status_ram_post ) == 0x00 ):
        print("Waiting for trigger..");
        sleep( 1 );
        timeout = timeout - 1;
      if ( timeout > 0 ):
        print("ACQUIRED.");
        if ( self.acq_mode == "nonrle" ):
          trig_i = sump_dump_data(self);# Grab data from hardware 
        else:
          trig_i = sump_dump_rle_data(self);# Grab data from hardware 
        self.trig_i = trig_i;

  # Group of OS commands pwd,mkdir,cd,ls,cp,vi
  elif ( cmd == "pwd" ):
    import os;
    rts += [ os.getcwd() ];
  elif ( cmd == "mkdir" ):
    import os;
    try:
      os.path.mkdir();
    except:
      rts += ["ERROR: "+cmd+" "+parms[0] ];
  elif ( cmd == "cd" ):
    import os;
    try:
      os.chdir( parms[0] );
    except:
      rts += ["ERROR: "+cmd+" "+parms[0] ];
  elif ( cmd == "ls" ):
    import os;
    rts += os.listdir( os.getcwd() );
#   rts += os.listdir( "*.ini" );
  elif ( cmd == "vi" ):
    try:
      if ( self.os_sys == "Linux" ):
        os.system('vi ' + parms[0] );
      else:
        os.system('notepad.exe ' + parms[0] );
    except:
      rts += ["ERROR: "+cmd+" "+parms[0] ];
  elif ( cmd == "cp" ):
    from shutil import copyfile;
    try:
      copyfile( parms[0], parms[1] );
    except:
      rts += ["ERROR: "+cmd+" "+parms[0]+" "+parms[1] ];
  
# elif ( cmd == "sump_dump" ):
#   sump_dump_data(self);
#   sump_save_txt(self);
#   sump_save_txt(self, mode_vcd = True );
#   sump_save_vcd( self );
#   txt2vcd = TXT2VCD();
#   file_in = open( "sump_dump.txt4vcd", "r" );
#   file_lines = file_in.readlines();
#   file_in.close();  
#   rts = txt2vcd.conv_txt2vcd( file_lines );
#   filename = make_unique_filename( self, "sump_", ".vcd" );
#   file_out = open( filename, "w" ); # Append versus r or w
#   for each in rts:
#     file_out.write( each + "\r\n" );# Make Windows Friendly
#   file_out.close();
#   file_name = "sump_dump.txt4vcd";
# else:
#   file_name = "sump_dump.txt";

  elif ( cmd == "save_txt" ):
    filename = make_unique_filename( self, "sump2_", ".txt" );
    sump_save_txt( self, filename );

  elif ( cmd == "save_rename" ):
    val1 = self.last_filesave;
    val2 = val1;
    if ( val1 != None ):
      rts = draw_popup_entry(self, ["Save_Rename()", val1],val2);
      import os;
      try:
        os.rename( val1, rts );
        draw_header( self,"Save_Rename() : " + val1 + " " + rts );
      except:
        draw_header( self,"ERROR: Save_Rename() : " + val1 + " " + rts );

# elif ( cmd == "save_vcd" ):
  elif (( cmd == "save_vcd" or 
          cmd == "save_vcd_full" or
          cmd == "save_vcd_cursors" ) and self.acq_mode == "nonrle" ):
    print("save_vcd()");
    screen_flip( self );# Only thing changing is the popup selection
#   sump_dump_data(self);# Grab data from hardware ( might be in CLI Mode )
    filename_txt = make_unique_filename( self, "sump2_", ".txt" );
    filename_vcd = make_unique_filename( self, "sump2_", ".vcd" );
#   draw_popup_msg(self,
#                  ["NOTE:","Saving capture to VCD file "+filename_vcd],1);
    sump_save_txt(self, filename_txt, mode_vcd = True, cmd = cmd );
    txt2vcd = TXT2VCD();# Instantiate Class for the VCD Conversion
    file_in = open( filename_txt, "r" );
    file_lines = file_in.readlines();
    file_in.close();  

    # line-0 contains a list of all signal names and ends with clock period
    # Iterate the list and replace each signal name with its nickname
    words = " ".join(file_lines[0].split()).split(' ');
    new_line = "";
    for each in words:
      nickname = each;# Handles both unknowns and clock period
      for sig_obj in self.signal_list:
        if ( each == sig_obj.name ):
          nickname = sig_obj.nickname;
          if ( nickname == "" ):
            nickname = each;
      new_line += nickname + " ";
    vcd_lines = [new_line] + file_lines[1:];
    print("conv_txt2vcd()");
    rts = txt2vcd.conv_txt2vcd( self, vcd_lines );
#   rts = txt2vcd.conv_txt2vcd( vcd_lines );
    print("Saving " + filename_vcd );
    draw_header( self,"save_vcd() : Saving " + filename_vcd );
    file_out = open( filename_vcd, "w" ); # Append versus r or w
    for each in rts:
      file_out.write( each + "\n" );# Make Windows Friendly
    file_out.close();
    draw_header( self,"save_vcd() : Saved " + filename_vcd );
    self.last_filesave = filename_vcd;
    rts = ["save_vcd() Complete " + filename_vcd ];

# elif ( cmd == "save_vcd" and \
  elif (( cmd == "save_vcd" or 
          cmd == "save_vcd_full" or
          cmd == "save_vcd_cursors" ) and 
         ( self.acq_mode == "rle" or self.acq_mode == "rle_lossy" ) ):
    print("save_rle_vcd()");
    screen_flip( self );# Only thing changing is the popup selection
#   if ( self.mode_cli == True ):
#     sump_dump_rle_data(self);# Grab data from hardware 
    filename_txt = make_unique_filename( self, "sump2_rle_", ".txt" );
    filename_vcd = make_unique_filename( self, "sump2_rle_", ".vcd" );
#   draw_popup_msg(self,
#                  ["NOTE:","Saving capture to VCD file "+filename_vcd],1);
    sump_save_txt(self, filename_txt, mode_vcd = True, cmd = cmd );
    txt2vcd = TXT2VCD();# Instantiate Class for the VCD Conversion
    file_in = open( filename_txt, "r" );
    file_lines = file_in.readlines();
    file_in.close();  

    # line-0 contains a list of all signal names and ends with clock period
    # Iterate the list and replace each signal name with its nickname
    words = " ".join(file_lines[0].split()).split(' ');
    new_line = "";
    for each in words:
      nickname = each;# Handles both unknowns and clock period
      for sig_obj in self.signal_list:
        if ( each == sig_obj.name ):
          nickname = sig_obj.nickname;
          if ( nickname == "" ):
            nickname = each;
      new_line += nickname + " ";
    vcd_lines = [new_line] + file_lines[1:];
    print("conv_txt2vcd()");
    rts = txt2vcd.conv_txt2vcd( self, vcd_lines );
#   rts = txt2vcd.conv_txt2vcd( vcd_lines );
    print("Saving " + filename_vcd );
    draw_header( self,"save_rle_vcd() : Saving " + filename_vcd );
    file_out = open( filename_vcd, "w" ); # Append versus r or w
    for each in rts:
      file_out.write( each + "\n" );# Make Windows Friendly
    file_out.close();
    draw_header( self,"save_rle_vcd() : Saved " + filename_vcd );
    self.last_filesave = filename_vcd;
    rts = ["save_rle_vcd() Complete " + filename_vcd ];

  elif ( cmd == "sump_status" ):
    rts_hex = ( self.sump.rd( addr = None )[0] );
    rts += [ "%08x" % rts_hex ];

# elif ( cmd == "w" or cmd == "r" or cmd == "bs" or cmd == "bc" ):
  elif ( cmd == "w" or cmd == "r" ):
    rts = proc_bd_cmd(self, cmd, parms );

  elif ( cmd == "sleep" or cmd == "sleep_ms" ):
    duration = float(int( parms[0], 16 ));
    if ( cmd == "sleep_ms" ):
      duration = duration / 1000.0;
    sleep( duration );

# elif ( cmd == "debug_vars" ):
#   debug_vars( self );
# elif ( cmd == "scroll_toggle" ):
#   self.scroll_togl *= -1;
#   if ( self.scroll_togl == 1 ):
#     print( "Scroll Wheel is Pan");
#   else:
#     print( "Scroll Wheel is Zoom");

  elif ( cmd == "reload" ):
    proc_cmd( self, "save_format", ["wave_autosave.do"] );
    self.signal_list = [];
    file2signal_list( self, self.file_name );
    flush_surface_cache( self );
    proc_cmd( self, "load_format", ["wave_autosave.do"] );
  elif ( cmd == "load" ):
    self.file_name = parms[0];
    proc_cmd( self, "reload", [""] );
    proc_cmd( self, "load_format", [""] );

  elif ( cmd == "save_jpg" ):
    screen_erase( self );
    draw_screen( self );
    screen_flip( self );
    filename = make_unique_filename( self, "sump2_", ".jpg" );
    self.pygame.image.save( self.screen, filename );
    print("save_jpg() : " + filename);
    draw_header( self,"save_jpg() : Saved " + filename );
    self.last_filesave = filename;
  elif ( cmd == "save_bmp" ):
    screen_erase( self );
    draw_screen( self );
    screen_flip( self );
    filename = make_unique_filename( self, "sump2_", ".bmp" );
    self.pygame.image.save( self.screen, filename );
    print("save_bmp() : " + filename);
    draw_header( self,"save_bmp() : Saved " + filename );
    self.last_filesave = filename;
  elif ( cmd == "save_png" ):
    # Warning - on Pi red and green are swapped on PNG saves
    screen_erase( self );
    draw_screen( self );
    screen_flip( self );
    filename = make_unique_filename( self, "sump2_", ".png" );
    self.pygame.image.save( self.screen, filename );
    print("save_png() : " + filename);
    draw_header( self,"save_png() : Saved " + filename );
    self.last_filesave = filename;

#   # Workaround for Pi Pygame bug swapping Red and Green on PNG save
#   # This requires PIL - not part of PyGame or Raspbian
#   filename = make_unique_filename( self, "sump2_", ".bmp" );
#   self.pygame.image.save( self.screen, filename );
#   from PIL import Image;
#   im = Image.open( filename );
#   filename = make_unique_filename( self, "sump2_", ".png" );
#   im.save( filename ) ;
#   draw_header( self,"save_png() : Saved " + filename );
#   self.last_filesave = filename;

    # Workaround for Pi Pygame bug swapping Red and Green on PNG save
    # This works, but takes a minute on just a 640x480 display
#   img = self.pygame.surfarray.array3d( self.screen )
#   img_copy = self.pygame.surfarray.array3d( self.screen )
#   for y in range(0, self.screen_height ):
#     for x in range(0, self.screen_width ):
#       img_copy[x][y][0] = img[x][y][1];
#       img_copy[x][y][1] = img[x][y][0];
#   surf = self.pygame.surfarray.make_surface(img_copy);
#   filename = make_unique_filename( self, "sump2_", ".png" );
#   self.pygame.image.save( surf , filename );
#   draw_header( self,"save_png() : Saved " + filename );
#   self.last_filesave = filename;

  elif ( cmd == "font_larger" or cmd == "font_smaller" ):
    size = int( self.vars["font_size"] );
    if ( cmd == "font_larger" ):
      size += 2;
    else:
      size -= 2;
      if ( size < 2 ): 
        size = 2;
    self.vars["font_size"] = str( size );
    self.font = get_font( self, self.vars["font_name"],self.vars["font_size"]);
    self.max_w = 0;
    self.max_w_chars = 0;
    flush_surface_cache( self );

  elif ( cmd == "add_wave" ):
    sig_obj = add_wave( self, [ cmd ] + parms );
    if ( sig_obj != None ):
      self.signal_list.append( sig_obj );
    flush_surface_cache( self );

  elif ( cmd == "save_format" ):
    file_name = parms[0];
    if ( file_name == "" ):
#     file_name = "wave_" + self.top_module + ".txt";# Default
      file_name = "sump2_wave.txt";
    save_format( self, file_name, False );

  elif ( cmd == "save_selected" ):
    file_name = parms[0];
    if ( file_name == "" ):
      file_name = "wave_" + self.top_module + ".txt";# Default
    save_format( self, file_name, True );
    load_format( self, file_name );
    flush_surface_cache( self );

  elif ( cmd == "load_format" ):
    file_name = parms[0];
    if ( file_name == "" ):
      file_name = "wave_" + self.top_module + ".txt";# Default
    load_format( self, file_name );
    flush_surface_cache( self );

  # Check for "SUMP_Configuration" menu items and launch entry popup
  elif ( cmd == "sump_trigger_delay"   or
         cmd == "sump_trigger_nth"     or
         cmd == "sump_user_ctrl"       or
         cmd == "sump_user_pattern0"   or
         cmd == "sump_user_pattern1"   or
         cmd == "sump_rle_autocull"    or
         cmd == "sump_rle_gap_remove"  or
         cmd == "sump_rle_gap_replace" or
         cmd == "sump_watchdog_time" 
       ): 
    name = cmd;
    val1 = self.vars[ name ];# Original Value
    val2 = val1;             # New Value to change
    rts = draw_popup_entry(self, [cmd, val1],val2);
    self.vars[ name ] = rts;

  elif ( cmd == "edit_format" ):
    import os, subprocess, platform;
    file_name = parms[0];
    if ( file_name == "" ):
      file_name = "wave_" + self.top_module + ".txt";# Default
    editor = os.getenv('EDITOR', 'vi')
    if ( platform.system() == "Windows" ):
      editor = "notepad.exe";
    subprocess.call('%s %s' % (editor, file_name), shell=True)
    if ( platform.system() == "Windows" ):
      self.pygame.event.clear();# Required for Windows
    load_format( self, file_name );
    flush_surface_cache( self );


  elif ( cmd == "delete_format" ):
    file_name = parms[0];
    if ( file_name == "" ):
      file_name = "wave_" + self.top_module + ".txt";# Default
    import os;
    print( "delete_format() ", file_name);
    os.remove( file_name );
    self.signal_list = [];
    file2signal_list( self, self.file_name );
    flush_surface_cache( self );

  elif ( cmd == "search" or cmd == "backsearch" ):
    if ( cmd == "search" ):
      direction = +1;
    else:
      direction = -1;
    # "/" : Search on last search value
    # Optionally support "/ foo = bar" and convert to "/ foo bar"
    if ( parms[1] == "=" ):
      parms[1] = parms[2];
    if ( parms[0] == None ):
      value = self.last_search_value;
    # "/ foo = bar" : Search for foo = bar
    elif ( parms[1] != None ):
      for each in self.signal_list:
        if ( each.name.lower() == parms[0].lower() ):
          self.sig_obj_sel = each;
          for sig_obj in self.signal_list:
            sig_obj.selected = False;# DeSelect All
          self.sig_obj_sel.selected = True;
          value = parms[1].lower();
          break;
    # "/ bar" : Search for self.sig_obj_sel = bar
    else:
      value = parms[0].lower();
    self.last_search_value = value; # Support "/<enter>" to search again
    self.sample_start = search_values( self, self.sig_obj_sel, 
                                       self.sample_start, value, direction  );

  elif ( cmd == "zoom_out" ):
# Warning routine appears twice, here and again for Mouse Wheel
# search on ZOOM_OUT.
# Make sure there are enough samples to our right, if not recalculate
# the center sample position so that the zoom_out fits within avail samples
    self.prev_sample_start = self.sample_start;
    self.prev_sample_stop  = self.sample_start + self.sample_room;
    sample_room = self.sample_room * 2;
    sample = self.sample_start - sample_room // 4;
    if ( ( sample + sample_room ) > self.max_samples ):
      sample = self.max_samples - sample_room;
    if ( sample < 0 ): sample = 0;
    self.sample_start = int( sample );
    set_zoom_x( self, self.zoom_x / 2.0 );

  elif ( cmd == "zoom_in" ):
    self.prev_sample_start = self.sample_start;
    self.prev_sample_stop  = self.sample_start + self.sample_room;
    # If called from popup, center the zoom on mouse position of popup
#   print( self.popup_x );
    if ( self.popup_x == None ):
#     (sample, Null) = get_sample_at_mouse( self, self.popup_x, self.popup_y );
#     sample_room = self.sample_room // 2; # zoom_in results in 1/2 sample_room
#     sample = sample - sample_room // 2;  # Center on select by sub 1/2 
#     if ( sample < 0 ):
#       sample = 0;
#     self.sample_start = sample;
      self.sample_start += ( self.sample_room // 4 );
    else:
      (sample, Null) = get_sample_at_mouse( self, self.popup_x, self.popup_y );
      delta = sample - self.sample_start;
      delta = delta // 2;
      self.sample_start = sample - delta;
      if ( self.sample_start < 0 ):
        self.sample_start = 0;
#     sample = sample - sample_room // 2;  # Center on select by sub 1/2 
#     if ( sample < 0 ):
#       sample = 0;
#     self.sample_start = sample;

    set_zoom_x( self, self.zoom_x * 2.0 );

  elif ( cmd == "zoom_previous" ):
    if ( self.prev_sample_start != None and
         self.prev_sample_stop  != None      ):
      proc_cmd( self, "zoom_to", [str(self.prev_sample_start),
                                  str(self.prev_sample_stop ) ] );

  elif ( cmd == "zoom_to_cursors" ):
    self.prev_sample_start = self.sample_start;
    self.prev_sample_stop  = self.sample_start + self.sample_room;
    sample_left = None;
    sample_right = None;
    for cur_obj in self.cursor_list:
      if   ( sample_left  == None ): sample_left  = cur_obj.sample;
      elif ( sample_right == None ): sample_right = cur_obj.sample;
    if ( sample_left != None and sample_right != None ):
      if ( sample_left > sample_right ):
        sample_left, sample_right = sample_right, sample_left;# Swap
      # Now fudge a bit as we want to actually see the cursors after to zoom
      delta = sample_right - sample_left;
      # If delta is large, use a small percentage, otherwise use a bunch of 
      # samples. Example is after triggering, cursors are at +/-1 from trigger
      if ( delta > 20 ):
        sample_left  -= delta // 32;
        sample_right += delta // 32;
      else:
        sample_left  -= 4*delta;
        sample_right += 4*delta;

      if ( sample_left  < 0                ): sample_left  = 0;
      if ( sample_right > self.max_samples ): sample_right = self.max_samples;
      proc_cmd( self, "zoom_to", [str(sample_left), str( sample_right ) ] );

  elif ( cmd == "crop_to_cursors" ):
    sample_left = None;
    sample_right = None;
    for cur_obj in self.cursor_list:
      if   ( sample_left  == None ): sample_left  = cur_obj.sample;
      elif ( sample_right == None ): sample_right = cur_obj.sample;
    if ( sample_left != None and sample_right != None ):
      if ( sample_left > sample_right ):
        sample_left, sample_right = sample_right, sample_left;# Swap
      if ( sample_left  < 0                ): sample_left  = 0;
      if ( sample_right > self.max_samples ): sample_right = self.max_samples;
      proc_cmd( self, "crop_to", [str(sample_left), str( sample_right ) ] );

  elif ( cmd == "crop_to_ini" ):
    trig_i = self.trig_i;	   
    sample_left  = trig_i - int( self.vars["sump_crop_pre_trig_len" ],16 );
    sample_right = trig_i + int( self.vars["sump_crop_post_trig_len" ],16 );

    if ( sample_left  < 0                ): sample_left  = 0;
    if ( sample_right > self.max_samples ): sample_right = self.max_samples;
    proc_cmd( self, "crop_to", [str(sample_left), str( sample_right ) ] );

  elif ( cmd == "zoom_full" ):
    proc_cmd( self, "zoom_to", ["0", str( self.max_samples ) ] );

  elif ( cmd == "crop_to" ):
    # If a sample range is specified, zoom to it
    if ( parms[0] != None and parms[1] != None ):
      if ( int( parms[0] ) < int( parms[1] ) ):
        crop_to_left  = int( parms[0] );
        crop_to_right = int( parms[1] );
      else:
        crop_to_left  = int( parms[1] );
        crop_to_right = int( parms[0] );
      for sig_obj in self.signal_list:
#       print(      sig_obj.name    );
#       print( len( sig_obj.values ));
        if ( len( sig_obj.values )  >= crop_to_right ):
          sig_obj.values = sig_obj.values[crop_to_left:crop_to_right];
      recalc_max_samples( self );
      proc_cmd( self, "zoom_full", [] );

  elif ( cmd == "zoom_to" ):
    # If a sample range is specified, zoom to it
    if ( parms[0] != None and parms[1] != None ):
      if ( int( parms[0] ) < int( parms[1] ) ):
        self.zoom_to_left  = int( parms[0] );
        self.zoom_to_right = int( parms[1] );
      else:
        self.zoom_to_left  = int( parms[1] );
        self.zoom_to_right = int( parms[0] );
    # Otherwise, zoom in so that current selectec signal is visible
    else:
      sig_obj = self.sig_obj_sel;
      if ( sig_obj.bits_total > 1 ):
#       nibs = sig_obj.bits_total / 4;  
#       nibs = nibs / 2;
        nibs = sig_obj.bits_total // 4;  
        nibs = nibs // 2;
        if ( nibs < 2 ):
          nibs = 2;
        nibs += 1; # Extra whitespace
        zoom_x = self.txt_width * nibs;
      value_width_x = self.sig_value_stop_x - self.sig_value_start_x;
#     value_width_samples = value_width_x / zoom_x;
      value_width_samples = int( value_width_x / zoom_x );
      self.zoom_to_left  = self.sample_start;
      self.zoom_to_right = self.sample_start + value_width_samples;
    
    self.sample_start = int( self.zoom_to_left );
    # Given the zoom_to region, calculate new zoom_x, it is pixels/samples
    # fudge_more_right = 3; # Need to grab more samples then calculated, strang
    fudge_more_right = 0; # Need to grab more samples then calculated, strang
#   set_zoom_x( self, ( self.sig_value_stop_x - self.sig_value_start_x ) / \
#    ( fudge_more_right+self.zoom_to_right    - self.zoom_to_left      )     );

    # Check for divide by zero and set new zoom if safe to, else ignore
    if ( ( self.zoom_to_right - self.zoom_to_left ) != 0 ):
      set_zoom_x( self,
         ( 1.0*(self.sig_value_stop_x - self.sig_value_start_x )) / \
         ( 1.0*( self.zoom_to_right   - self.zoom_to_left      ))    );
    else:
      print("ERROR: Div-by-zero attempt on set_zoom_x()");

  elif ( cmd == "scroll_right" or cmd == "scroll_left" ):
#   print "cmd", cmd;
    if ( cmd == "scroll_right" ):
      direction = 0+int( parms[0] );
    else:
      direction = 0-int( parms[0] );
    self.sample_start += int( direction );
    # Prevent scrolling too far to right
    if ( self.sample_start + self.sample_room > self.max_samples ):
      self.sample_start = int( self.max_samples - self.sample_room );
    if ( self.sample_start < 0 ):
      self.sample_start = 0;

  elif ( cmd == "scroll_up" or cmd == "scroll_down" ):
    # Scroll thru the selected signal names. When at the top or bottom of 
    # the visible window, scroll the window.
    self.name_surface_valid = False;
#   self.curval_surface_valid = False;
    index = 1;# Default if none found
    if ( self.sig_obj_sel != None ):
      if ( self.sig_obj_sel.selected == True ):
        index = self.signal_list.index( self.sig_obj_sel );
      self.sig_obj_sel.selected = False; # Deselect last scroll selected

    if ( cmd == "scroll_up" ):
      direction = 0-int( parms[0] );
    else:
      direction = 0+int( parms[0] );

    # Keep moving in the desired direction until we get a visible signal
    obj_is_visible = False;
    while ( obj_is_visible == False ):
      # Make sure new index is valid
      index = index + direction;
      if ( index < 0 ):
        index = 0;
        break;
      if ( index >= len( self.signal_list ) ):
        index = len( self.signal_list ) -1;
        break;
      obj_is_visible = self.signal_list[ index ].visible;

    # Scroll the signal name viewport if newly selected is outside existing
    self.vertical_scrolled_offscreen = False;
    if ( index < self.sig_top ):
      self.sig_top -= 1;
      self.vertical_scrolled_offscreen = True;
      flush_surface_cache( self );
    if ( index > self.sig_bot ):
      self.sig_top += 1;
      self.vertical_scrolled_offscreen = True;
      flush_surface_cache( self );

    # Assign selected signal object to sig_obj_sel
    sig_obj = self.signal_list[ index ];
    sig_obj.selected = True;
    self.sig_obj_sel = sig_obj;

  # Rename a signal - popup bd_shell for text entry
  # TODO: Would be nicer to have a GUI entry window for single bd_shell cmds
  elif ( cmd == "rename" ):
#   cmd_start = "rename_signal " + self.sig_obj_sel.name + " ";
#   bd_shell(self, cmd_start );
    cmd = "Rename_Signal";
    val1 = self.sig_obj_sel.name;
    val2 = self.sig_obj_sel.nickname;
    rts = draw_popup_entry(self, [cmd, val1],val2);
    self.sig_obj_sel.nickname = rts;
    self.name_surface_valid = False;
#   flush_surface_cache( self );# Redraw with new values

# proc_cmd( self, cmd, parms ):
  elif ( cmd == "rename_signal" ):
    if ( parms[1] != "" ):
      for sig_obj in self.signal_list:
        if ( sig_obj.name == parms[0] ):
          sig_obj.nickname = parms[1];


#   self.txt_entry = True; # Enable Dialog Box to show up
#   

# # Rename a signal
# elif ( cmd == "rename" ):
#   self.txt_entry = True; # Enable Dialog Box to show up

# # Rename a signal ( Process the Text Entry )
# elif ( cmd == "rename_signal" ):
#   if ( self.sig_obj_sel.bits_total == 0 ):
#     self.sig_obj_sel.name     = parms[0]; # A Divider
#   else:
#     self.sig_obj_sel.nickname = parms[0]; # A Signal
#   flush_surface_cache( self );

  # Delete selected signal(s) ( Make Invisible )
  elif ( cmd == "delete" ):
    flush_surface_cache( self );
    for sig_obj in self.signal_list:
      if ( sig_obj.selected == True ):
        index = self.signal_list.index( sig_obj );
        self.signal_list[ index ].visible = False;
#       del self.signal_list[ index ];
        self.sig_obj_sel = None;   
#       print "deleting ", self.signal_list[ index ].name, str( index );

  # Delete selected signal(s)
  elif ( cmd == "cut" ):
    flush_surface_cache( self );
    self.clipboard = [];
    for sig_obj in self.signal_list:
      if ( sig_obj.selected == True ):
        index = self.signal_list.index( sig_obj );
        self.clipboard.append( self.signal_list.pop( index ) );

  elif ( cmd == "paste" ):
    flush_surface_cache( self );
    for sig_obj in self.signal_list:
      if ( sig_obj.selected == True ):
        index = self.signal_list.index( sig_obj );
        for each in reversed( self.clipboard ):
          self.signal_list.insert( index, each );
        break;

  # Make all signals visible - only way to undo a Make_Invisible
  elif ( cmd == "make_all_visible" ):
    flush_surface_cache( self );
    for sig_obj in self.signal_list:
      sig_obj.visible = True;
    sump_signals_to_vars( self );# Update sump variables

  # Hide a signal at this mouse location
  elif ( cmd == "make_invisible" ):
    flush_surface_cache( self );
    for sig_obj in self.signal_list:
      if ( sig_obj.selected == True ):
        sig_obj.visible = False;
    sump_signals_to_vars( self );# Update sump variables

  # Hide a selected signal. Note that hidden and invisible are different
  # hidden means display the signal name, but hide the signal values.
  # invisible means don't display at all ( kinda like delete ).
  elif ( cmd == "hide" or cmd == "hide_all" ):
    flush_surface_cache( self );
    for sig_obj in self.signal_list:
      if ( sig_obj.selected == True or cmd == "hide_all" ):
        sig_obj.hidden = True;

    # New 2017_03_14. If a group parent was hidden, hide children too
    for sig_obj in self.signal_list:
      if ( sig_obj.selected == True ):
        found_jk = False;
        hier_level = -1;   # Keeps track of group nesting
        for ( i , sig_obj ) in enumerate( self.signal_list ):
          if ( found_jk == True ):
            if ( sig_obj.hier_level <= hier_level ):
              found_jk = False;# Found the endgroup so done
              break;
            else:
              sig_obj.hidden = True;# Hide the children
          if ( sig_obj == self.sig_obj_sel ):
            found_jk = True; # Found our specified parent
            hier_level = sig_obj.hier_level;

    sump_signals_to_vars( self );# Update sump variables
    screen_refresh( self );

  # Show a selected signal 
  elif ( cmd == "show" or cmd == "show_all" ):
    flush_surface_cache( self );
    for sig_obj in self.signal_list:
      if ( sig_obj.selected == True or cmd == "show_all" ):
        sig_obj.hidden  = False;
        sig_obj.visible = True;
    # New 2017_03_14. If a group parent was show, show children too
    for sig_obj in self.signal_list:
      if ( sig_obj.selected == True ):
        found_jk = False;
        hier_level = -1;   # Keeps track of group nesting
        for ( i , sig_obj ) in enumerate( self.signal_list ):
          if ( found_jk == True ):
            if ( sig_obj.hier_level <= hier_level ):
              found_jk = False;# Found the endgroup so done
              break;
            else:
              sig_obj.hidden = False;# Show the children
              sig_obj.visible = True;
          if ( sig_obj == self.sig_obj_sel ):
            found_jk = True; # Found our specified parent
            hier_level = sig_obj.hier_level;


    sump_signals_to_vars( self );# Update sump variables
    screen_refresh( self );

  # When "Trigger_Rising" or "Trigger_Falling" is selected, set the bit
  # in the sump variable and then update the signals to match.
  elif ( cmd == "trigger_rising" or cmd == "trigger_falling" or
#        cmd == "trigger_watchdog" ):
         cmd == "trigger_remove"    or 
         cmd == "trigger_remove_all"    or 
         cmd == "trigger_watchdog" or 
	 cmd == "trigger_and"      ):
    print("Setting new trigger");
    trig_field = int( self.vars["sump_trigger_field"],16 );# Value
    # Find which signal is selected
    for sig_obj in self.signal_list:
      sig_obj.trigger = 0;
      if ( sig_obj.selected == True ):
        for i in range( 0, 32 , 1):
          if ( sig_obj.name == ( "event[%d]" % i ) ):
            if ( cmd == "trigger_and" ):
              self.vars["sump_trigger_type"] = "and_rising";
            if ( cmd == "trigger_rising" ):
              self.vars["sump_trigger_type"] = "or_rising";
            if ( cmd == "trigger_falling" ):
              self.vars["sump_trigger_type"] = "or_falling";
            if ( cmd == "trigger_watchdog" ):
              self.vars["sump_trigger_type"] = "watchdog";

            if ( cmd == "trigger_falling" ):
              trig_field = 0x00000000 | ( 1<<i );# Only 1 Falling Trigger Works
              self.vars["sump_trigger_field" ] = ("%08x" % ( trig_field) );
            elif ( cmd != "trigger_remove" and cmd != "trigger_remove_all"):
              trig_field = trig_field | ( 1<<i );
              self.vars["sump_trigger_field" ] = ("%08x" % ( trig_field) );
            elif ( cmd == "trigger_remove_all" ):
              trig_field = 0x00000000;
              self.vars["sump_trigger_field" ] = ("%08x" % ( trig_field) );
            else:	       
              trig_field = trig_field & ~( 1<<i );
              self.vars["sump_trigger_field" ] = ("%08x" % ( trig_field) );
            print("%08x" % trig_field );

#           flush_surface_cache( self );
    sump_vars_to_signal_attribs( self );
    self.name_surface_valid = False;
    screen_refresh( self );

  elif ( cmd == "set_pattern_0" or cmd == "set_pattern_1" or \
         cmd == "clear_pattern_match" ):
    # Find which signal is selected
    user_pattern0 = int( self.vars["sump_user_pattern0" ],16 );# Mask
    user_pattern1 = int( self.vars["sump_user_pattern1" ],16 );# Value
    for sig_obj in self.signal_list:
      if ( sig_obj.selected == True ):
        for i in range( 0, 32 , 1):
          if ( sig_obj.name == ( "event[%d]" % i ) ):
            if ( cmd == "clear_pattern_match" ):
              user_pattern0 = user_pattern0 & ~( 1<<i );# Clear bit
              user_pattern1 = user_pattern1 & ~( 1<<i );# Clear bit
            else:
              user_pattern0 = user_pattern0 |  ( 1<<i );# Set bit
            if ( cmd == "set_pattern_0" ):
              user_pattern1 = user_pattern1 & ~( 1<<i );# Clear bit
              self.vars["sump_trigger_type"] = "pattern_rising";   
            if ( cmd == "set_pattern_1" ):
              user_pattern1 = user_pattern1 |  ( 1<<i );# Set   bit
              self.vars["sump_trigger_type"] = "pattern_rising";   
    self.vars["sump_user_pattern0" ] = ("%08x" % user_pattern0 );
    self.vars["sump_user_pattern1" ] = ("%08x" % user_pattern1 );
    sump_vars_to_signal_attribs( self );
    flush_surface_cache( self );

  elif ( cmd == "set_data_enable" or cmd == "clear_data_enable" ):
    data_en = int( self.vars["sump_data_enable"   ],16 );
    for sig_obj in self.signal_list:
      if ( sig_obj.selected == True ):
        for i in range( 0, 32 , 1):
          if ( sig_obj.name == ( "event[%d]" % i ) ):
            if ( cmd == "set_data_enable" ):
              data_en = data_en |  ( 1<<i );# Set bit
            elif ( cmd == "clear_data_enable" ):
              data_en = data_en & ~( 1<<i );# Clear bit
    self.vars["sump_data_enable" ] = ("%08x" % data_en );
    sump_vars_to_signal_attribs( self );
    flush_surface_cache( self );


#       sig_obj = get_sig_obj_by_name( self, ("event[%d]" % i ) );
#   flush_surface_cache( self );
#   for sig_obj in self.signal_list:
#     sig_obj.trigger = 0;
#     if ( sig_obj.selected == True ):
#       if ( cmd == "trigger_rising" ):
#         sig_obj.trigger = +1;
#       elif ( cmd == "trigger_falling" ):
#         sig_obj.trigger = -1;



  # Make a signal Signed, Unsigned or Hex format
  elif ( cmd == "signed" or cmd == "unsigned" or cmd == "hex" ):
    flush_surface_cache( self );
    for sig_obj in self.signal_list:
      if ( sig_obj.selected == True ):
        sig_obj.format = cmd.lower();# unsigned, signed or hex

  # Insert a Divider at this mouse location
  elif ( cmd == "insert_divider" ):
    flush_surface_cache( self );
    (Null,index) = get_sample_at_mouse( self, self.popup_x, self.popup_y );
    try:
      sig_obj = self.signal_list[ index ];   
      new_div = signal( name="--------" );
      new_div.bits_per_line = 0;
      new_div.bits_total    = 0;
      new_div.bit_top       = 0;
      new_div.bit_bot       = 0;
      new_div.format        = "";
      self.signal_list.insert( index, new_div );
    except:
      print("ERROR 5519:index = " + str( index ) );

  # Expand  : Iterate list and make visible signals under current hier level
  elif ( cmd == "expand" ):
    flush_surface_cache( self );
    if ( self.sig_obj_sel.collapsable == True ):
      proc_cmd(self, "collapse",[""] );
      return;
    found_jk = False;
    hier_level = -1;   # Keeps track of group nesting
    for ( i , sig_obj ) in enumerate( self.signal_list ):
      if ( found_jk == True ):
        if ( sig_obj.hier_level <= hier_level ):
          found_jk = False;# Found the endgroup so done
          break;
        if ( sig_obj.type != "endgroup" ):
          sig_obj.visible = True;# Make all signals visible after expand
        if ( sig_obj.collapsable == True or \
             sig_obj.expandable  == True     ):
          sig_obj.collapsable = True;
          sig_obj.expandable  = False;
      if ( sig_obj == self.sig_obj_sel ):
        found_jk = True; # Found our specified divider
        sig_obj.collapsable = True;
        sig_obj.expandable  = False;
        hier_level = sig_obj.hier_level;

  # Collapse : Iterate list and hide signals under current hier level
  elif ( cmd == "collapse" ):
    flush_surface_cache( self );
    found_jk = False;
    hier_level = -1;   # Keeps track of group nesting
    for ( i , sig_obj ) in enumerate( self.signal_list ):
      if ( found_jk == True ):
        if ( sig_obj.hier_level <= hier_level ):
          found_jk = False;# Found the endgroup so done
          break;
        if ( sig_obj.type != "endgroup" ):
          sig_obj.visible = False;# Make all signals invisible after expand
        if ( sig_obj.collapsable == True or \
             sig_obj.expandable  == True     ):
          sig_obj.collapsable = False;
          sig_obj.expandable  = True;
      if ( sig_obj == self.sig_obj_sel ):
        found_jk = True; # Found our specified divider
        sig_obj.collapsable = False;
        sig_obj.expandable  = True;
        hier_level = sig_obj.hier_level;


  # Group a bunch of selected signals together. 
  elif ( cmd == "group_with_divider" or cmd == "group_with_parent" ):
    flush_surface_cache( self );
    start = None;
    stop  = None;
    hier_name  = "";
    hier_level = 0;
    top_list = [];
    mid_list = [];
    bot_list = [];
    for ( i , sig_obj ) in enumerate( self.signal_list ):
      if ( sig_obj.selected == True ):
        if ( start != None or cmd == "group_with_divider" ):
          sig_obj.visible = False;  
          sig_obj.grouped = True;
        if ( start == None ):
          start = i;
          hier_name  = sig_obj.hier_name;# Divider inherits hier of 1st signal
          hier_level  = sig_obj.hier_level;# Divider inherits hier of 1st signal

          # Make a group divider and insert above 1st signal
          if ( cmd == "group_with_divider" ):
            new_div = signal( name="Group" );
            new_div.type          = "group";
            new_div.hier_name     = hier_name;
            new_div.hier_level    = hier_level;
            new_div.bits_per_line = 0;
            new_div.bits_total    = 0;
            new_div.bit_top       = 0;
            new_div.bit_bot       = 0;
            new_div.format        = "";
            new_div.collapsable = False;
            new_div.expandable  = True;
            mid_list.append( new_div );
            sig_obj.hier_level = hier_level + 1;# Not a parent, so change level
          else:
            sig_obj.hier_level = hier_level;    # Parent Keeps Original Level
            sig_obj.collapsable = False;
            sig_obj.expandable  = True;
        else:
          stop  = i;
          sig_obj.hier_level = hier_level + 1;
        mid_list.append( sig_obj );
      else:
        if ( start == None ):
          top_list.append( sig_obj );
        else:
          bot_list.append( sig_obj );
    self.signal_list = top_list + mid_list + bot_list;

#   if ( cmd == "group_with_divider" ):
#   # Make a group divider and insert above 1st signal
#     new_div = signal( name="Group" );
#     new_div.type          = "group";
#     new_div.hier_name     = hier_name;
#     new_div.hier_level    = hier_level;
#     new_div.bits_per_line = 0;
#     new_div.bits_total    = 0;
#     new_div.bit_top       = 0;
#     new_div.bit_bot       = 0;
#     new_div.format        = "";
#     self.signal_list.insert( start, new_div );
#   else:
#     self.signal_list[start].type = "group";# Change from Signal to Group

#   self.signal_list[start].collapsable = False;
#   self.signal_list[start].expandable  = True;

#   # TODO : Remove this as no longer necessary
#   # Now make a divider that marks the end of the group, but invisible
#   new_div = signal( name="^^-EndGroup-^^" );
#   new_div.type          = "endgroup";
#   new_div.hier_name     = hier_name;
#   new_div.hier_level    = hier_level+1;
#   new_div.bits_per_line = 0;
#   new_div.bits_total    = 0;
#   new_div.bit_top       = 0;
#   new_div.bit_bot       = 0;
#   new_div.format        = "";
#   new_div.visible       = False;
#   if ( cmd == "group_with_divider" ):
#     self.signal_list.insert( stop+2,  new_div );
#   else:
#     self.signal_list.insert( stop+1,  new_div );

  # Bring both cursors into view
  elif ( cmd == "cursors_to_view" ):
    ( sample, Null )  = get_sample_at_mouse( self, self.mouse_x, self.mouse_y );
    for each in self.cursor_list:
      each.selected = False;
      if ( sample < 0 ):
        sample = 0;
      each.sample = int( sample );
    self.curval_surface_valid = False;# curval surface invalid when cur move

  # Bring both cursors into view
  elif ( cmd == "cursor1_to_here" or cmd == "cursor2_to_here" ):
    for ( i , each ) in enumerate( self.cursor_list ):
      if ( i == 0 and cmd == "cursor1_to_here" or 
           i == 1 and cmd == "cursor2_to_here"    ):
        each.sample = self.popup_sample;
    self.curval_surface_valid = False;# curval surface invalid when cur move


  # Find nearest signal transition to mouse x,y and snap nearest cursor to it
  elif ( cmd == "cursor_snap" ):
    mouse_x = int( parms[0] );
    mouse_y = int( parms[1] );
    (sample,index) = get_sample_at_mouse( self, mouse_x, mouse_y );
    if ( index != None and index < len( self.signal_list ) ):
      sig_obj = self.signal_list[ index ];

      # Calculate the maximum distance from "sample" to search
      max_left  = sample - 0;
      max_right = self.max_samples - sample;
      if ( max_left < max_right ):
        max_search = max_left;
      else:
        max_search = max_right;
      edge_sample = sample; # Default to starting point
      # Simultanesouly find closest edge ( left or right ) of sample. 
      try:
        for i in range( 0, max_search, 1 ):
          org_sample   = sig_obj.values[sample];
          left_sample  = sig_obj.values[sample-i];
          right_sample = sig_obj.values[sample+i];
          if ( left_sample != org_sample ):
            edge_sample = sample-i+1;
            break;
          if ( right_sample != org_sample ):
            edge_sample = sample+i;
            break;

        # Unselect both cursors
        for each in self.cursor_list:
          each.selected = False;
        # Now move Cursor that is closest to our mouse position sample
        cur0_obj = self.cursor_list[0];
        cur1_obj = self.cursor_list[1];
        cur0_delta = abs( sample - cur0_obj.sample );
        cur1_delta = abs( sample - cur1_obj.sample );
        if ( cur0_delta < cur1_delta ):
          cur_obj = cur0_obj;
        else:
          cur_obj = cur1_obj;
        cur_obj.selected = True;               # Select Closest Cursor
        cur_obj.sample   = int( edge_sample ); # Move it to pulldown location
      except:
        print("ERROR: cursor_snap()");
      self.curval_surface_valid = False;# curval surface is invalid 

  else:
#   print( "Unknown Command " + cmd);
    # Try a DOS command when all else fails
    if ( cmd != "" ):
      try:
        from subprocess import call;
        call( [ cmd, parms[0] ] );
      except:
#     print("ERROR: I'm sorry Dave, I'm afraid I can't do that");
        resp = [ "Error" ]
        import random;
        print( ">"+cmd+"<" );
        print( random.choice( resp ) );

  if ( self.mode_cli == False ):
    screen_refresh( self );
  return rts;

  # Avoid refreshing screen if we have scroll events queued up. This prevents
  # this display from getting hopelessly behind on slower machines.
  # After skipping 20, refresh regardless. 
  # Might want to make this value a user config variable.
  if ( self.pygame.event.peek( self.pygame.MOUSEBUTTONUP   ) == False and
       self.pygame.event.peek( self.pygame.MOUSEBUTTONDOWN ) == False     ):
    screen_refresh( self );
  else:
    self.skipped_refresh_cnt +=1;
#   if ( self.skipped_refresh_cnt > 20 ):
    if ( self.skipped_refresh_cnt > 10 ):
      self.skipped_refresh_cnt =0;
      screen_refresh( self );
  return;


###############################################################################
# zoom_x defines the number of x pixels a single sample gets
# for example, if self.txt_width is 10 and zoom_x = 20:
#   <><><><><><>    : zoom_x = 5 
#   <0><1><2><3>    : zoom_x = 10
#   < 0 >< 1 >< 2 > : zoom_x = 20
def set_zoom_x( self, new_zoom_x ):
  flush_surface_cache( self );
  if ( new_zoom_x > 0.0 ):
    self.zoom_x = new_zoom_x;
    # As we zoom out, scroll rate increases beyond +1;
#   self.scroll_num_samples = self.txt_width / new_zoom_x;
#   self.scroll_num_samples = int( 4 * self.txt_width / new_zoom_x );
    self.scroll_num_samples = int( 4 * self.txt_width // new_zoom_x );
    if ( self.scroll_num_samples < 1 ):
      self.scroll_num_samples = 1;
    
#   print( "zoom_x is now " + str( self.zoom_x ));
#   print "scroll_num_samples is now = " + str( self.scroll_num_samples );
    
  else:
    print( "Invalid zoom_x " + str ( new_zoom_x ));
  draw_header( self, ( "Zoom = %0.2f" % new_zoom_x ) );
# print( "Zoom = %0.2f" % new_zoom_x );
  return;


###############################################################################
# Create some cache surfaces for drawing signal values and names too.
# This is an attempt to speed things up by minimizing text and graphics 
# rendering until something drastic ( zoom, signal hide, etc ) happens.
# Most of the time during left and right scroll, just blit a rectangle region
# onto the screen surface.
def create_surfaces( self ):
  self.value_surface = self.pygame.Surface( ( self.screen_width*4, \
                                              self.screen_height  )  );
  self.value_surface = self.value_surface.convert();# Makes blitting faster
  self.name_surface = self.pygame.Surface( ( self.screen_width, \
                                             self.screen_height  )  );
  self.name_surface = self.name_surface.convert();# Makes blitting faster
  self.curval_surface = self.pygame.Surface( ( self.screen_width, \
                                               self.screen_height  )  );
  self.curval_surface = self.curval_surface.convert();# Makes blitting faster
  return;


def create_icon( self ):
  self.icon_surface = self.pygame.Surface( ( 32,32 ) );
  self.icon_surface = self.icon_surface.convert();# Makes blitting faster
  # Convert "00FF00" to ( 0,255,0 );
  color_fg = self.vars["color_screen_foreground"];
  self.color_fg = ( int( color_fg[0:2], 16 ) ,
                    int( color_fg[2:4], 16 ) ,
                    int( color_fg[4:6], 16 )   );
  color_bg = self.vars["color_screen_background"];
  self.color_bg = ( int( color_bg[0:2], 16 ) ,
                    int( color_bg[2:4], 16 ) ,
                    int( color_bg[4:6], 16 )   );
  self.icon_surface.fill( self.color_bg );
  self.pygame.draw.lines(self.icon_surface,self.color_fg,False,
     [ (0,2),(8,2),(8,8),(16,8),(16,2),(24,2),(24,8),(32,8) ],
     2 );

  self.pygame.draw.lines(self.icon_surface,self.color_fg,False,
     [ (0,18),              (16,18),(16,12),(32,12)                 ],
     2 );
  self.pygame.draw.lines(self.icon_surface,self.color_fg,False,
     [ (0,22),(8,22),(8,28)                        ,(24,28),(32,28) ],
     2 );
  return self.icon_surface;


###############################################################################
def flush_surface_cache( self ):
  if ( self.debug ):
    print( "flush_surface_cache()");
  self.surface_stop = -1;# Force a flush on the self.value_surface
  self.name_surface_valid = False;
  self.curval_surface_valid = False;


###############################################################################
def draw_header( self, txt ):
  if ( txt != "" ):
    # print( txt );
    txt = (": "+txt );
  if ( self.mode_cli == True ):
    return;
  
  if ( self.vcd_import == True ):
    uut_name = self.vcd_name;
  else:
    uut_name = self.vars["uut_name" ];

  if ( self.rle_lossy == True ):
    uut_name += " RLE_LOSSY";
  if ( self.fatal_msg != None ):
    uut_name = "Software DEMO Mode :";
    txt = uut_name + " " + self.fatal_msg;
  msg ="SUMP2 " + self.vers + " (c) 2017 BlackMesaLabs : "+uut_name+" "+txt;
# msg ="SUMP2 " + self.vers + " (c) 2017 BlackMesaLabs : "+txt;
  self.pygame.display.set_caption( msg );

  if ( self.os_sys == "XXLinux" ):
    # Pi display header at top of surface since full screen
    msg += "                    ";# Erase old text that was wider
    txt = self.font.render( msg , True, self.color_fg, self.color_bg );
    y1 = self.txt_height // 2; # Gap from top border
    x1 = self.txt_width;    # Gap from left border
    self.screen.blit(txt, (x1,y1 ));
    screen_flip( self );

  if ( self.gui_active == True ):
    import pygame;
    pygame.event.get();# Avoid "( Not Responding )"
  return;


###############################################################################
def draw_popup_entry( self, txt_list, default_txt ):
  if ( self.mode_cli == True ):
    print( txt_list );
    return;
  done = False;
  self.key_buffer = default_txt;# Preload the key buffer with a default
  import pygame;
  while ( done == False ):
    txt2_list = [];
    for each in txt_list:
      txt2_list += ["  " + each ];# Need some whitespace padding on left
    txt2_list += [ "  " + self.key_buffer + "_" ];# Draw a fake cursor
    draw_popup_msg(self, txt2_list, 1 );
    screen_flip( self );
    for event in pygame.event.get(): # User did something
      if event.type == pygame.KEYDOWN:
        if ( event.key == pygame.K_BACKSPACE ):
          self.key_buffer = self.key_buffer[:-1];# Remove last char
        elif ( event.key == pygame.K_INSERT ):
          self.key_buffer += "a";
        elif ( event.key == pygame.K_DELETE ):
          done = True;
        elif ( event.key == pygame.K_RETURN ):
          done = True;
        else:
#         ch = pygame.key.name( event.key );
#         if ( len(ch) == 1 ):
          try:
            self.key_buffer += event.unicode; 
          except:
            pass;
  return self.key_buffer;

###############################################################################
def draw_popup_msg( self, txt_list, wait_time = 0, txt_entry = False ):
  if ( self.mode_cli == True ):
    print( txt_list );
    return;

  import types;
  (mouse_x,mouse_y) = self.pygame.mouse.get_pos();
  x1 = self.popup_x;
  y1 = self.popup_y;
 
  # If popup won't fit, adjust y location to fit screen   
  popup_height = (len(self.popup_list)+2) * self.txt_height;
  if ( ( y1 + popup_height ) > self.screen_height ):
    y1 = self.screen_height - popup_height - self.txt_height;
  self.popup_y2 = y1; # Remember where popup is displayed
 
  # Draw a box with a border with text inside
  draw_popup_box( self, x1,y1, txt_list );
 
  return;


###############################################################################
def draw_popup_cmd( self ):
  import types;
  (mouse_x,mouse_y) = self.pygame.mouse.get_pos();
  x1 = self.popup_x;
  y1 = self.popup_y;

  # If popup won't fit, adjust y location to fit screen   
  popup_height = (len(self.popup_list)+2) * self.txt_height;
  if ( ( y1 + popup_height ) > self.screen_height ):
    y1 = self.screen_height - popup_height - self.txt_height;
  self.popup_y2 = y1; # Remember where popup is displayed

  txt_list = [];
  y2 = y1;
  y3 = False;

  # Calc pixel width of widest text and use to decide if subpop to be visible
  max_w = 0;
  for each in self.popup_list:
    if ( type( each ) != list ):
      txt = each;
      txt1 = self.font.render( txt, True, self.color_fg,self.color_bg );
      w = txt1.get_width();# Calculate the Maximum String Width
      if ( w > max_w ):
        max_w = w;

  subpop_list = [];
  for each in self.popup_list:
    y2 += self.txt_height;
    txt = each;
    # each might define a subpop list, so check for listtype and conv to string
#   if ( type( each ) is types.ListType ):
    if ( type( each ) == list ):
      txt = str(txt[0]) + ">";# If List, take 1st List Item and Conv to String

    # Check to see if mouse is over this one and add select "[]" brackets
    if ( (  txt[0:-1] == self.popup_sel or 
            txt       == self.popup_sel    ) and
         txt[0:2] != "--"  ):
#     if ( type( each ) is types.ListType ):
      if ( type( each ) == list ):
        txt = "[" + str(txt) + "]";# Highlight text the mouse is hovering over
        txt1 = self.font.render( txt, True, self.color_fg,self.color_bg );
        w = max_w;
        # If mouse is on right edge, calc x,y for subpop and make list
        if ( mouse_x > ( x1 + w ) ):
          y3 = y2;
          x3 = x1 + w;
          subpop_list = each[1:];
      else:
        txt = "[" + str(txt) + "]";# Highlight text the mouse is hovering over
    else:
      txt = " " + str(txt) + " ";
    txt_list.append( str(txt) );
  draw_popup_box( self, x1,y1, txt_list );

  # Check to see if exiting a subpop, if so, restore parent
  if ( mouse_x < x1 ):
    if ( self.popup_parent_x != None ):
      self.popup_x    = self.popup_parent_x;
      self.popup_y    = self.popup_parent_y;
      self.popup_list = self.popup_parent_list;
      self.popup_parent_x = None;# NEW
      screen_refresh( self );# Erase the subpop

  # Check if subpop needs to be created. Store parent info for return
  if ( y3 != False ):
    # Remember Parent info
    self.popup_parent_x    = self.popup_x;
    self.popup_parent_y    = self.popup_y;
    self.popup_parent_list = self.popup_list;
    # then create new popup
    self.popup_x = x3;
    self.popup_y = y3 - self.txt_height;
    self.popup_list = subpop_list;
    draw_popup_cmd( self );
  return;

def draw_popup_box( self, x1,y1, txt_list ):
  # Calculate how big the box needs to be for the text list
  tw = 0; w = 0;
  for each in txt_list:
    if ( len( each ) > tw ):
      tw = len( each );
      txt = self.font.render( " "+each+" ", True, self.color_fg,self.color_bg );
      w = txt.get_width();# Calculate the Maximum String Width in pels
  h = len ( txt_list ) * self.txt_height + ( self.txt_height );
  w = w + ( self.txt_height//2 );

  # HERE : Abort if there isn't enough room
# if ( ( x1 + w ) > self.screen_width ):
#   x1 -= w;
#   return False;

  # Make all the text the same width by padding spaces 
  new_txt_list = [];
  for each in txt_list:
    txt = (each + 30*" ")[0:tw];
    new_txt_list.append(txt);
  txt_list = new_txt_list;

  # Draw a black box with a green border of size of text list
  self.pygame.draw.rect( self.screen, self.color_bg,(x1,y1,w,h), 0);
  self.pygame.draw.rect( self.screen, self.color_fg,(x1,y1,w,h), 1);
  self.popup_w = w;

  # Now draw txt_list inside the box
  y = y1 + ( self.txt_height // 2 ); 
  x = x1 + ( self.txt_height // 4 );

  # If ">" exists ( indicating sublist exists ), move to far right then render
  for each in txt_list:
    if ( ">" in each ):
      each = each.replace(">"," ");
      each = each[0:tw-1] + ">"; # Place on Far Right Instead
    txt = self.font.render( each, True, self.color_fg, self.color_bg );
    self.screen.blit( txt , ( x,y ) );
    y = y + self.txt_height;
# return True;
  return;


###############################################################################
# Determine which command the popup has selected. 
def get_popup_sel( self ):
  import types;
  # Calculate selection the mouse is hovering over.
  (mouse_x,mouse_y) = self.pygame.mouse.get_pos();
  x1 = self.popup_x;
  y1 = self.popup_y;

  # If popup won't fit, adjust y location to fit screen   
  popup_height = (len(self.popup_list)+2) * self.txt_height;
  if ( ( y1 + popup_height ) > self.screen_height ):
    y1 = self.screen_height - popup_height - self.txt_height;
    self.popup_y2 = y1; # Remember where popup is displayed

# y = y1 + ( self.txt_height / 2 ); 
# x = x1 + ( self.txt_height / 4 );
  y = y1 + ( self.txt_height // 2 ); 
  x = x1 + ( self.txt_height // 4 );
  rts = "";
  for each in self.popup_list:
#   if ( type( each ) is types.ListType ):
    if ( type( each ) == list ):
      each = each[0];# If List, take 1st Item in List and Convert to String
    if ( mouse_y > y            and mouse_y <  y+self.txt_height       and \
         mouse_x > self.popup_x and mouse_x < self.popup_x+self.popup_w):
      rts = each;
    y = y + self.txt_height;
  return rts;


###############################################################################
# Find a monospaced font to use
def get_font( self , font_name, font_height ):
  log( self, ["get_font() " + font_name ] );
  if ( self.os_sys == "XXLinux" ):
    # Pi Specific
    font_size = int(font_height);
    font = self.pygame.font.Font('freesansbold.ttf', font_size );
    txt = font.render("4",True, ( 255,255,255 ) );
    self.txt_width  = txt.get_width();
    self.txt_height = txt.get_height();
  else:
    import fnmatch;
    font_height = int( font_height, 10 ); # Conv String to Int
    font_list = self.pygame.font.get_fonts(); # List of all fonts on System
    self.font_list = [];
    for each in font_list:
      log( self, ["get_font() : Located Font = " + each ] );
      # Make a list of fonts that might work based on their name
      if ( ( "mono"    in each.lower()    ) or
           ( "courier" in each.lower()    ) or
           ( "fixed"   in each.lower()    )    ):
        self.font_list.append( each );

    if ( font_name == None or font_name == "" ):
      font_list = self.pygame.font.get_fonts(); # List of all fonts on System
      for each in font_list:
        log( self, ["get_font() : Located Font = " + each ] );
      ends_with_mono_list = fnmatch.filter(font_list,"*mono");
      if  ends_with_mono_list :
        font_name = ends_with_mono_list[0];# Take 1st one
      else:
        font_name = self.font_list[0]; # Take 1st one
    try:
      font = self.pygame.font.SysFont( font_name , font_height );
    except:
      font = self.pygame.font.Font( None , font_height );# Default Pygame Font

    # Calculate Width and Height of font for future reference
    txt = font.render("4",True, ( 255,255,255 ) );
    self.txt_width  = txt.get_width();
    self.txt_height = txt.get_height();
  return font;


###############################################################################
def screen_refresh( self ):
  if ( self.gui_active == True ):
#   Note: Doing a draw_header() here erases message for things like save_vcd
#   draw_header( self,"screen_refresh()." );
    screen_erase( self );# Erase all the old stuff
#   draw_header( self,"screen_refresh().." );
    draw_screen( self ); # Draw the new stuff
#   draw_header( self,"screen_refresh()..." );
    screen_flip( self ); # and xfer from pending to active
    draw_header( self,"" );
  return;


###############################################################################
def screen_flip( self ):
  if ( self.gui_active == True ):
    self.pygame.display.flip();# This MUST happen after all drawing commands.


###############################################################################
def screen_erase( self ):
  if ( self.gui_active == True ):
    # Convert "00FF00" to ( 0,255,0 );
    color_fg = self.vars["color_screen_foreground"];
    self.color_fg = ( int( color_fg[0:2], 16 ) ,
                      int( color_fg[2:4], 16 ) ,
                      int( color_fg[4:6], 16 )   );
    color_bg = self.vars["color_screen_background"];
    self.color_bg = ( int( color_bg[0:2], 16 ) ,
                      int( color_bg[2:4], 16 ) ,
                      int( color_bg[4:6], 16 )   );
    self.screen.fill( self.color_bg );
  return;


###############################################################################
def draw_screen( self ):
  if ( self.gui_active == False ):
    return;
# import math;
# print "draw_screen()";
# t0 = self.pygame.time.get_ticks();
  if ( self.debug ):
    print( "draw_screen()");
  screen_w = self.screen.get_width();
  screen_h = self.screen.get_height();
# v_scale = 1.25;# This provides a proportional gap between text lines
# v_scale = 1.10;# This provides a proportional gap between text lines
  v_scale = 1.25;# This provides a proportional gap between text lines
# bot_region_h = 5;
  bot_region_h = 6;# HERE
  self.sig_name_stop_y = screen_h - ( bot_region_h * self.txt_height );
  self.sig_value_stop_y = self.sig_name_stop_y;

  # 1st Display the Net Names
# y = self.txt_height / 2; # Gap from top border
  if ( self.os_sys == "XXLinux" ):
    y = self.txt_height * 2; # Pi top header
  else:
    y = self.txt_height // 2; # Gap from top border

  x = self.txt_width;     # Gap from left border
  self.sig_name_start_x = x;
  self.sig_name_start_y = y;

# # Place all objects off-screen as they might be scrolled
# for sig_obj in self.signal_list:
#   sig_obj.y = -100;

  # Calculate how many signals will fit vertically on screen then make a 
  # scrolled copy of the signal list of only the signals to be displayed.
  sample_h = int( (screen_h - (bot_region_h*self.txt_height)) / \
                         ( self.txt_height*v_scale) );
# last_sig = int( self.sig_top + sample_h );
# self.sig_bot = last_sig-1;
# if ( last_sig > len( self.signal_list ) ):
#   last_sig = len( self.signal_list );

# self.signal_list_cropped = self.signal_list[self.sig_top:last_sig];
  self.signal_list_cropped = [];
  vis_sigs = 0; i = 0;
  for each in self.signal_list[self.sig_top:]:
    i +=1;
    if ( each.visible == True and vis_sigs < sample_h ):
      self.signal_list_cropped.append( each );
      vis_sigs += 1;
      if ( vis_sigs == sample_h ):
        break;# No Mas
# self.sig_bot = self.sig_top + i - 2;
  self.sig_bot = self.sig_top + i - 1;
# print "vis_sigs = " + str( vis_sigs );

  self.signal_list_cropped = tuple( self.signal_list_cropped );# List->Tuple

  # 1st : Display the signal names on the left
  # for sig_obj in self.signal_list_cropped:
  # Iterate the entire list for the signal names as we dont want the max_w 
  # calculation to change on vertical scroll ( its annoying ). Make max_w
  # calculated from the entire list. Try and reuse existing surface if valid
  surface = self.name_surface;
  if ( self.name_surface_valid != True ):
    surface.fill( self.color_bg );
    if ( self.debug ):
      print( "name_surface_valid==False");
#   for ( i , sig_obj ) in enumerate( self.signal_list ):
    for ( i , sig_obj ) in enumerate( tuple(self.signal_list) ):
      if ( 1 == 1 ):
        # Binary Signal? If standalone, no rip, if exp, display (n) bit pos
        if ( sig_obj.bits_total == 1 or sig_obj.bits_total == 0 ):
          if ( sig_obj.is_expansion == True ):
            exp_str = "    ";
            rip_str = "(" + str( sig_obj.bit_top ) + ")";
          else:
            exp_str = "    ";
            rip_str = "";
        # Hex signal, so display rip positions (n:m)
        else:
          rip_str="("+str(sig_obj.bit_top)+":"+str(sig_obj.bit_bot)+")";#(31:0)
          exp_str="[+] ";
        # Disable Signal Expansion and Collapse. Add back later
        exp_str = "    ";
    
        # Divider Attributes
        if ( sig_obj.collapsable == True ):
          exp_str = "[-] ";
        if ( sig_obj.expandable  == True ):
          exp_str = "[+] ";

        if ( sig_obj.trigger == +1 ):
          exp_str = "__/ ";
        elif ( sig_obj.trigger == -1 ):
          exp_str = "\__ ";
        elif ( sig_obj.trigger == -2 ):
          exp_str = "=WD ";
        elif ( sig_obj.trigger ==  2 ):
          exp_str = "==0 ";# Pattern of 0
        elif ( sig_obj.trigger ==  3 ):
          exp_str = "==1 ";# Pattern of 1
        elif ( sig_obj.trigger ==  +4 ):
          exp_str = "=1  ";# AND Rising
        elif ( sig_obj.data_enable == True ):
          exp_str = "=== ";

        if ( sig_obj.selected == True ):
          exp_str = exp_str + "[";
          end_str =           "]";
        elif ( sig_obj.hidden == True ):
          exp_str = exp_str + "#";
          end_str =           "#";
#       elif ( sig_obj.grouped == True ):
#         exp_str = exp_str + "  ";# Indent group members
#         end_str =           "";  # Kinda wiggy if they get selected though
        else:
          exp_str = exp_str + " ";
          end_str =           " ";

        # Indent to Hierarchy Level
        exp_str = (sig_obj.hier_level*"  ") + exp_str;

        # Finally, if a nickname has been assigned display it instead of name
        if ( sig_obj.nickname != "" ):
          disp_name = sig_obj.nickname;
        else:
          disp_name = sig_obj.name;

        txt_str = exp_str + disp_name + rip_str + end_str;# ie "[foo(7:0)]"

        # If this is the widest net name of all, calc and remember pel width
        if ( len( txt_str ) > self.max_w_chars ):
          txt = self.font.render(txt_str,True,self.color_fg,self.color_bg);
          self.max_w_chars = len( txt_str );# minimize measuring pels
          self.max_w       = txt.get_width();

        # Only render and blit the TXT if visible and in current view
        if ( ( sig_obj.visible == True   ) and \
             ( i >= self.sig_top ) and \
             ( i <= self.sig_bot         )     ):
            txt = self.font.render(txt_str,True,self.color_fg,self.color_bg);
            surface.blit(txt, (x,y ));
            sig_obj.y = int( y );
            sig_obj.h = self.txt_height*v_scale;
            sig_obj.w = self.zoom_x;
            y += self.txt_height*v_scale;
        else:
          sig_obj.y = -100; # Place it off screen for mouse lookup
    self.name_surface_valid = True; # Our surface is now valid
    self.sig_name_stop_x = self.sig_name_start_x + self.max_w;
  # ^^ if ( self.name_surface_valid != True ) ^^
  self.screen.blit( surface, ( 0, 0),  \
                    ( 0,0, self.sig_name_stop_x, self.sig_name_stop_y ) ) ;


  # 2 1/2 Display signal value at active cursor position
  self.net_curval_start_x = self.sig_name_stop_x;
  self.net_curval_start_y = self.sig_name_start_y;
  self.net_curval_stop_x  = self.net_curval_start_x +  8 * self.txt_width;
  self.net_curval_stop_y  = self.sig_name_stop_y;
  cur_obj = None;
  surface = self.curval_surface;
  if ( self.curval_surface_valid != True ):
    surface.fill( self.color_bg );
    if ( self.debug ):
      print( "curval_surface_valid==False");
    for each in self.cursor_list:
      if ( each.selected == True ):
        cur_obj = each;
    if ( cur_obj != None ):
      c_val = cur_obj.sample; # Sample Number 
      for sig_obj in self.signal_list_cropped:
        if ( sig_obj.visible == True ):
          if ( c_val < len( sig_obj.values ) ):
            val = sig_obj.values[c_val]; 
            # VV
            if   ( val == True ):
              val = "1";
            elif ( val == False ):
              val = "0";
          else:
            val = "X";
          y1  = sig_obj.y;
          x1  = self.net_curval_start_x;
          txt = self.font.render( val , True, self.color_fg, self.color_bg );
          surface.blit(txt, (x1,y1 ));
#         self.screen.blit(txt, ( x1, y1 ) );
    self.curval_surface_valid = True; # Our surface is now valid
  self.screen.blit( surface, 
                    ( self.net_curval_start_x, self.net_curval_start_y ),
                    ( self.net_curval_start_x, self.net_curval_start_y,      
                      self.net_curval_stop_x, self.net_curval_stop_y ) ) ;

  # 2nd Display the Net Values by corner turning data
  # and calculate how many samples will fit in screen space
  sample_start = self.sample_start;
  self.sig_value_start_x = self.net_curval_stop_x + self.txt_width;
  self.sig_value_start_y = self.sig_name_start_y;
  start_x = self.sig_value_start_x;
  y = self.sig_value_start_y;

  # Warning: This sample_room calculation assumes samples are 1 nibble wide.
  x2 = self.screen_width - start_x - 2*self.txt_width;
  self.sample_room = int( float(x2) / float(self.zoom_x) );
  self.sample_stop  = sample_start + self.sample_room;

  # Make sure we don't zoom out too far relative to total samples captured
  if ( self.sample_room > self.max_samples ):
    self.stop_zoom = True;
  else:
    self.stop_zoom = False;

  # Check to see if our existing surface contains the sample range we need.
  # IF it does, don't redraw, instead save time and blit region of interest.
  # This saves considerable CPU time during standard left and right scrolling
# self.surface_stop = -1;
# surface = self.screen;
  surface = self.value_surface;
# print("%d %d , %d %d" % ( sample_start, self.surface_start,    
#                         self.sample_stop , self.surface_stop ));

  if (      sample_start >= self.surface_start and 
       self.sample_stop  <= self.surface_stop ):
    None;
  else:
    # print("Rendering samples.");
#   line_list_of_lists = [];
#   for sig_obj in self.signal_list_cropped:
#     if ( sig_obj.format == "bin" ):
#       sig_obj.values = [True if x=="1" else False for x in sig_obj.values];

    import time;
    start_time = time.time();
    surface.fill( self.color_bg );
    if ( self.debug ):
      print( "value_surface_valid==False");
    # Grab 4x the number of samples needed to fill display
    # this greatly speeds up scrolling in time back and forth, but only when
    # there aren't that many samples to draw.
    stop_4x = ( self.sample_stop-sample_start)*4 + sample_start;
    stop_1x = ( self.sample_stop-sample_start)   + sample_start;
    if ( stop_4x > self.max_samples ):
      stop_4x = self.max_samples;
    if ( stop_1x > self.max_samples ):
      stop_1x = self.max_samples;

    # Only Look-ahead render 4x if num samples < 1000
    if ( ( self.sample_stop-sample_start) > 1000 ):
      stop_4x = stop_1x;
#     print("NOTE: 4x look-ahead rendering disabled");

    # Rip thru all the signals ( vertically cropped ) and display visible ones
    import time;
    render_max_time = 0;# Don't Render DWORDs if rendering too slow
    perc_updates_en = True;
    fast_render = False;
    no_header = True;
#   if ( self.sample_room > 50000 ):
    if ( self.sample_room > 1000 ):
#     print("Enabling fast_render engine");
      fast_render = True;
    for sig_obj in self.signal_list_cropped:
      # Save time by not rendering DWORDs outside of viewport if RLE capture
      # Does this work without SUMP displaying VCD files??
      # Note: This didn't work after cropping and doesnt buy much, so removed
      render = True;

      # Don't render DWORDs if any prior signal took more than 2 seconds   
      if ( self.bd != None ):
        ram_dwords = self.sump.cfg_dict['ram_dwords'];
        for j in range( 0, ram_dwords, 1 ):
          if ( sig_obj.name == "dword[%d]" % j ):
            if ( render_max_time > 2 ):
              print("Culling "+sig_obj.name);
              render = False;

      if ( sig_obj.visible == True and render == True ):
        x = start_x;
        y = sig_obj.y;
        val_last = "";
        last_trans_x = start_x;
        last_width = None;
        last_x = 0;
        x_last = x;
        y_last = y;
        # Rip thru the visible values and display. Also convert number format
        sample = sample_start;
        total_count = stop_4x - sample_start;
        next_perc = 0;# Display an update every 5%
        render_start_time = time.time();
        if ( sig_obj.hidden == False and len( sig_obj.values ) > 0 ):
          if ( fast_render == False ):
            hdr_txt = "Full Rendering ";
          else:
            hdr_txt = "Fast Rendering ";
          if ( no_header == False ):
            draw_header( self,hdr_txt+sig_obj.name );

          k = 0; perc_cnt = 0;
          perc5 = total_count * 0.05;
          # Use Python set() function to determine if all samples are same
          samples_diff=(len(set(sig_obj.values[sample_start:stop_4x+1]))!=1);
          fast_list = []; 
          line_list = [];
          threshold_too_many_samples = 5000;
          if ( total_count > threshold_too_many_samples ):
            perc_updates_on = True;
          else:
            perc_updates_on = False;
          if ( sig_obj.format == "bin" ):
            format_bin = True;# Saves 300ms doing this string compare once
          else:
            format_bin = False;

          # TIME CRITICAL LOOP of going thru each sample in time
          for val in tuple(sig_obj.values[sample_start:stop_4x+1]):
            if ( perc_updates_on ):
              perc_updates_en = True;
              k +=1;# This takes 400ms of the 2.4 total, so bypass for small
#             if ( k > perc5 and perc_updates_en == True ):
              if ( k > perc5 ):
                k = 0;
                perc_cnt += 5;
                if ( no_header == False ):
                  draw_header(self,hdr_txt+sig_obj.name+" "+str(perc_cnt)+"%");
                if ( fast_render==False ):
                  if ((time.time()-render_start_time)>2):
                    print("Enabling fast_render engine");
                    fast_render = True;
                if ( (time.time()-render_start_time)< 0.2):
                  no_header = True;
                else:
                  no_header = False;

            # Only draw_sample() if integer portion of X has changed since last
            # this handles zoom_full case of zoom_x < 1.0 to minimize drawing
            if ( True  ):
              if ( format_bin and fast_render and samples_diff ):
                # Draw "_/ \___/   \___" lines for binary format
                # fast_render doesnt draw every sample but instead draws lines
                # whenever sample value changes. 3x faster, but leaves voids
                if ( val != val_last ):
                  x1 = int(x)+1;
                  x2 = x1;
                  y1 = y + 2;
                  y2 = y + self.txt_height - 2;
                  if ( val ):   # VV
#                 if ( val == "1" ):
#                   self.pygame.draw.line(surface,self.color_fg,
#                      (x_last,y_last),(x2,y2));
                    line_list += [ (x_last,y_last),(x2,y2) ];
                    x_last = x1;
                    y_last = y1;
                  else:
#                   self.pygame.draw.line(surface,self.color_fg,
#                      (x_last,y_last),(x1,y1));
                    line_list += [ (x_last,y_last),(x1,y1) ];
                    x_last = x2;
                    y_last = y2;
                  # Vertical Line
#                 self.pygame.draw.line(surface,self.color_fg,(x1,y1),\
#                                       (x2,y2));
                  line_list += [(x1,y1),(x2,y2)];# 8x slower. GoFigure
              # If all samples are same, just draw a single line of 1 or 0
              if ( format_bin and fast_render and not samples_diff ): 
                if ( sample == sample_start ):
                  x1 = int(x)+1;
#                 x2 = x1 + self.sample_room;# artifacts on zoom_out too far
                  x2 = x1 + int(float(stop_4x-sample_start) * self.zoom_x );
                  y1 = y + 2;
                  y2 = y + self.txt_height - 2;
                  if ( val ):   # VV
#                   self.pygame.draw.line(surface,self.color_fg,
#                      (x1,y1),(x2,y1));
                    line_list += [ (x1,y1),(x2,y1) ];
                  else:
#                   self.pygame.draw.line(surface,self.color_fg,
#                      (x1,y2),(x2,y2));
                    line_list += [ (x1,y2),(x2,y2) ];
                  break;# Save time and don't iterate thru all static samples

              elif ( format_bin ):
                # Saved 600ms ( from 3.4 to 2.8 ) by avoiding func call
                if ( val != val_last ):
                  x1 = int(x+1);
                  x2 = x1;
                  y1 = y + 2;
                  y2 = y + self.txt_height - 2;
                  if ( val == "0" ):
                    y1,y2 = y2,y1;# Swap to make direction correct
                  line_list += [ (x1,y1),(x2,y2) ];
#                 self.pygame.draw.line(surface,self.color_fg,(x1,y1),\
#                                       (x2,y2),1);
#               if ( val == "1" ):
                if ( val ):   # VV
                  x1 = int(x+1);
                  x2 = int(x+1 + self.zoom_x);
                  y1 = y + 2;
                  y2 = y1;
                  line_list += [ (x1,y1),(x2,y2) ];
#                 self.pygame.draw.line(surface,self.color_fg,(x1,y1),\
#                                       (x2,y2),1);
                else:
                  x1 = int(x+1);
                  x2 = int(x+1 + self.zoom_x);
                  y1 = y + self.txt_height - 2;
                  y2 = y1;
                  line_list += [ (x1,y1),(x2,y2) ];
#                 self.pygame.draw.line(surface,self.color_fg,(x1,y1),\
#                                       (x2,y2),1);
#             elif ( sig_obj.format != "bin" or fast_render == False ):
              else:
                (last_trans_x,last_width) = draw_sample( self, surface, \
                    val,val_last,last_trans_x,last_width, \
                    sig_obj.format,format_bin, x,y);
              val_last = val;
            x += self.zoom_x;
            sample +=1;
            # Remember x location of last sample drawn
            if ( sample == self.sample_stop ):
              self.sig_value_stop_x  = x;
          # END OF TIME CRITICAL LOOP

          if ( len( line_list ) > 0 ):
            self.pygame.draw.lines(surface,self.color_fg,False,line_list,1);
          if ( perc_updates_on ):
            refresh(self);

          render_stop_time = time.time();
          if ( ( render_stop_time - render_start_time ) > render_max_time ):
            render_max_time = render_stop_time - render_start_time;

#         if ( ( render_stop_time - render_start_time ) < 2 ):
#           perc_updates_en = False;# Don't update if rendering in less 2sec
#         else:
#           print(sig_obj.name+" %.2f Seconds" % \
#                 (render_stop_time-render_start_time )  );

#         if ( fast_render==False and (render_stop_time-render_start_time)>3):
          if ( fast_render==False and (render_stop_time-render_start_time)>2):
            print("Enabling fast_render engine");
            fast_render = True;

        self.sig_value_stop_y  = y;
        # Remember whats in the value_surface start:stop samples
        self.surface_start = sample_start;

        # Fix for strange performance bug. When viewing all samples, the
        # variable sample is less than self.sample_stop and this surface never
        # gets cached. Normally sample is greater than self.sample_stop which
        # support fast scrolling when zoomed in.
        #if ( sample_start == 0 ):
        #  self.surface_stop  = self.sample_stop;
        #else:
        #  self.surface_stop  = sample;
        if ( sample < self.sample_stop ):
          self.surface_stop  = self.sample_stop;
        else:
          self.surface_stop  = sample;
#   if ( len( line_list_of_lists ) > 0 ):
#     for each in line_list_of_lists:
#       self.pygame.draw.lines(surface,self.color_fg,False,each,1);

#   print("Rendering samples done");
    if ( fast_render == True ):
      txt = "Fast Rendering Complete"; 
    else:
      txt = "Full Rendering Complete"; 
    stop_time = time.time();
    total_time = stop_time - start_time;
#   if ( True ):
    if ( total_time > 2 ):
      print("Render Time = %.1f" % total_time );
      draw_header( self, txt );
    if ( self.display_hints and total_time > 15 ):
      print("I noticed rendering took a REALLY long time. That must hurt.");
      print("Here are some tips:");
      print(" 1) Limit the number of RLE samples that will get decompressed");
      print("    This will limit your samples to 512K pre and post trigger");
      print("    sump_rle_pre_trig_len  = 00080000 ");
      print("    sump_rle_post_trig_len = 00080000 ");
      print(" 2) Use cursors to either zoom_in or crop to a smaller region.");
      print(" 3) Check your DRAM usage. A couple of million SUMP samples   ");
      print("    can easily use up a couple of Gigabytes of DRAM.          ");
      self.display_hints = False;# Only display this once per session
     
  x = self.sig_value_start_x;
  y = self.sig_value_start_y;
  w = self.sig_value_stop_x - self.sig_value_start_x;
  h = self.sig_value_stop_y - self.sig_value_start_y + self.txt_height;
  x_offset = x + int( ( sample_start - self.surface_start ) * self.zoom_x );

  # Speed up the Vertical Scroll Operations by not redrawing the value surface
  # while the signal list is scrolling offscreen.
  if ( self.vertical_scrolled_offscreen == False ):
    self.screen.blit( self.value_surface, ( x, self.sig_value_start_y),
                      (x_offset,y, w, h ) );

  # 3rd Display any cursors
  self.cursor_list[0].y = self.screen_height - (4*self.txt_height) + \
                                            int(self.txt_height/2);
  self.cursor_list[1].y = self.cursor_list[0].y + self.txt_height;
  self.cursor_start_y   = self.cursor_list[0].y;
  self.cursor_stop_y    = self.cursor_list[1].y;
  for cur_obj in self.cursor_list:
    if ( cur_obj.visible == True ):
      x1 = self.sig_value_start_x + \
           (( cur_obj.sample - self.sample_start) * self.zoom_x );
      x1 += 1; # Draw right at the transition markers
      x2 = x1;
      y1 = self.sig_value_start_y;
      y2 = cur_obj.y -1 ;
      cur_obj.x = x1;
      if ( x1 >= self.sig_value_start_x and
           x1 <= self.sig_value_stop_x      ):
        if ( cur_obj.selected == True ):
          self.pygame.draw.line(self.screen,self.color_fg,(x1,y1),(x2,y2),2);
        else:
          self.pygame.draw.line(self.screen,self.color_fg,(x1,y1),(x2,y2),1);
        # txt = cur_obj.name;# ie "Cursor1"
        c_val = cur_obj.sample; # Display Location Instead
        c_mult = float( self.vars["cursor_mult"] );
        txt = " " + str( c_val ) +                                  " ";
        if ( cur_obj.selected == True ):
          self.font.set_bold( True );
        txt = self.font.render( txt , True, self.color_fg, self.color_bg );
        if ( cur_obj.selected == True ):
          self.font.set_bold( False );
        x1 -= int( txt.get_width()/2 );
        self.screen.blit(txt, ( x1, cur_obj.y ) );
  
  # 4th Measure num samples betwen two cursors and display
  # Make c1 always smaller than c2 to avoid negatives
  if ( self.cursor_list[0].sample < self.cursor_list[1].sample ):
    c1_sample = self.cursor_list[0].sample;
    c2_sample = self.cursor_list[1].sample;
    x1        = self.cursor_list[0].x;
    x2        = self.cursor_list[1].x;
  else:
    c1_sample = self.cursor_list[1].sample;
    c2_sample = self.cursor_list[0].sample;
    x1        = self.cursor_list[1].x;
    x2        = self.cursor_list[0].x;

  # If a cursor is off screen, make x1,x2 the screen edge
  if ( c1_sample < sample_start ):
    x1   = self.sig_value_start_x;
  if ( c1_sample > self.sample_stop  ):
    x1   = self.sig_value_stop_x;
  if ( c2_sample < sample_start ):
    x2   = self.sig_value_start_x;
  if ( c2_sample > self.sample_stop  ):
    x2   = self.sig_value_stop_x;


  # Calculate the time unit per sample
  if ( self.bd != None ):
    freq_mhz = self.sump.cfg_dict['frequency'];
  else:
    freq_mhz = 100.0;# HACK PLACEHOLDER ONLY !!
  c_mult = 1000.0 / freq_mhz;


  # 5th calculate where to put the measurement text, centered between markers
  # or edge of the screen and on-screen-marker. Only display if a cursor is vis
  if (  ( c1_sample >= sample_start and c1_sample <= self.sample_stop ) or \
        ( c2_sample >= sample_start and c2_sample <= self.sample_stop )      ):
    # Draw horizontal measurement bar at y location of Cur1
    y1 = self.cursor_list[0].y - int ( self.txt_height / 2 );
    y2 = y1;
    self.pygame.draw.line(self.screen,self.color_fg,(x1,y1),(x2,y2),1);
   
    # Now draw the measurement text for the cursor
    y = y1 - int(self.txt_height/2);
    c2c1_delta =  float(c2_sample-c1_sample);
    if ( self.undersample_data == True ):
      c2c1_delta *= self.undersample_rate;

    c2c1_delta_ns = c2c1_delta * float(c_mult);
    # Smart Time measurement scale between ns,us and ms
    if ( c2c1_delta_ns   > 1000000 ):
      cursor_units = "ms";
      c2c1_delta_ns = c2c1_delta_ns / 1000000.0;
    elif ( c2c1_delta_ns > 1000 ):
      cursor_units = "us";
      c2c1_delta_ns = c2c1_delta_ns / 1000.0;
    else:
      cursor_units = "ns";

    c2c1_delta    = int(c2c1_delta);
    c2c1_delta_str = str(c2c1_delta);

    delta_str = locale.format('%.1f', c2c1_delta_ns, True );

    # For undersampled data, label measurements with "~" for approximate
    if ( self.undersample_data == True ):
      delta_str      = "~"+delta_str;
      c2c1_delta_str = "~"+c2c1_delta_str;

    if ( self.rle_lossy == True ):
      txt = " RLE_LOSSY ~"+delta_str+"~ "+cursor_units+", ~"+ \
              c2c1_delta_str+"~ clocks";
    else:
      txt = " "+delta_str+""+cursor_units+", "+c2c1_delta_str+" clocks";

    txt = self.font.render( txt, True, self.color_fg, self.color_bg );
    w = txt.get_width();
    h = self.txt_height;
    # If the width of text is less than the space between cursors, display
    # between, otherwise, display to the right of rightmost cursor if it fits
    # or left of leftmost if it doesn't fit.
    small_gap =  self.txt_width;# Small Gap 
    if ( (w + small_gap) < ( x2-x1 ) ):
      x = x1 + int(( x2-x1 )/2) - int(w/2);
    else:
      x = x2 + self.txt_width;
      if ( ( x+w ) > self.screen_width ):
        x = min(x1,x2) - ( w + small_gap );# Draw left of leftmost cursor
    self.pygame.draw.rect( self.screen, self.color_bg ,(x,y,w,h), 0);
    self.screen.blit(txt, ( x, y ) );

  # 5 1/2 Draw Time scales ie "500.5us of 2.4ms" above the sample range
  # HERE
  time_width_ns_list  = [];
  time_width_ns_list += [ float( self.sample_room ) * float( c_mult ) ];
  time_width_ns_list += [ float( self.max_samples ) * float( c_mult ) ];
  for (i,each) in enumerate( time_width_ns_list ):
    if ( each > 1000000 ):
      each_time = each / 1000000.0;
      each_unit = "ms";
    elif ( each > 1000 ):
      each_time = each / 1000.0;
      each_unit = "us";
    else:
      each_time = each;
      each_unit = "ns";
    if ( i == 0 ):
      txt_str = locale.format('%.1f', each_time, True )+""+each_unit+" of ";
    else:
      txt_str += locale.format('%.1f', each_time, True )+""+each_unit;
  x =  self.txt_width;# Small Gap from left border
  y1 = self.cursor_list[1].y;
  txt_str = self.font.render( txt_str, True, self.color_fg, self.color_bg );
  self.screen.blit(txt_str, ( x, y1 ) );
    

  # 6th Draw the sample viewport dimensions 
  #  Example: 100-200 of 0-1024. Make the width 1024-1024 so it doesnt change
  txt1 = str(sample_start)+"-"+str(self.sample_stop);
  txt2 = str(     0      )+"-"+str(self.max_samples);
# txt3 = txt2 + " : " + txt1;
  txt3 = txt1 + " of " + txt2;
# txt1 = self.font.render( txt1, True, self.color_fg, self.color_bg );
# txt2 = self.font.render( txt2, True, self.color_fg, self.color_bg );
  txt3 = self.font.render( txt3, True, self.color_fg, self.color_bg );
  y1 = self.cursor_list[0].y;
  y2 = self.cursor_list[1].y;
# x  = self.net_curval_start_x;
  x =  self.txt_width;     # Small Gap from left border
# self.screen.blit(txt1, ( x, y1 ) );
# self.screen.blit(txt2, ( x, y2 ) );
# self.screen.blit(txt3, ( x, y2 ) );
# print (str(self.max_samples));# 

  y = self.screen_height - int(self.txt_height * 1.5 );
  x = self.sig_name_start_x;

  # Draw slider graphics for current view windows  |     |--|     |
  x1 = self.sig_value_start_x;
  x2 = self.sig_value_stop_x;
  y1 = y;
  y2 = y1 + self.txt_height;
  y3 = y1 + int(self.txt_height/2);
  self.screen.blit(txt3, ( x, y1 ) );

  lw = 1;# Line Width skinny, deselected
  self.pygame.draw.line(self.screen,self.color_fg,(x1,y1),(x1,y2),lw);
  self.pygame.draw.line(self.screen,self.color_fg,(x2,y1),(x2,y2),lw);
# print("max_samples is " + str( self.max_samples ) );
  x3 = x1 + ( ( (x2-x1) * sample_start // self.max_samples ) );
  x4 = x1 + ( ( (x2-x1) * self.sample_stop  // self.max_samples ) );
  w = x4-x3;
  h = y2-y1;
  self.slider_width = w;
  lw = 1;# Line Width skinny, deselected
  self.pygame.draw.line(self.screen,self.color_fg,(x3,y1),(x3,y1+h),lw);
  self.pygame.draw.line(self.screen,self.color_fg,(x3+w,y1),(x3+w,y1+h),lw);
  self.pygame.draw.line(self.screen,self.color_fg,(x3,y1+h/2),(x3+w,y1+h/2),lw);


  # 7th - cleanup. Draw black box over area on right, one character width. 
  w = self.txt_width;
  y = self.sig_value_start_y;
  h = self.sig_value_stop_y - y;
  x = self.screen_width - w;
  self.pygame.draw.rect( self.screen, self.color_bg ,(x,y,w,h), 0);

  # 8th Display the keyboard buffer and command history in a text box
  if ( self.txt_entry == False):
    x = self.sig_name_start_x;
    y = self.sig_name_stop_y + int(self.txt_height/2);
    h = self.screen_height - ( y                                       );
    w = self.sig_value_start_x - x;
#   prompt = ">";
#   cursor = "_";
#   cmd_txt = prompt + self.key_buffer+cursor+"                ";
#   txt_list = self.cmd_history[-3:] + [ cmd_txt ];
    cmd_txt = "";

    if ( self.acq_state != "acquire_stop" ):
      cmd_txt = "ACQUIRING";
#     cmd_txt = "ACQUIRING                    ";
#     if   ( self.spin_char == "-" ): self.spin_char = "\\";
#     elif ( self.spin_char == "\\" ): self.spin_char = "|";
#     elif ( self.spin_char == "/" ): self.spin_char = "-";
#     else                          : self.spin_char = "-";
      if   ( self.spin_char == "." )     : self.spin_char = "..";
      elif ( self.spin_char == ".." )    : self.spin_char = "...";
      elif ( self.spin_char == "..." )   : self.spin_char = "....";
      elif ( self.spin_char == "...." )  : self.spin_char = ".....";
      elif ( self.spin_char == "....." ) : self.spin_char = "";
      else                               : self.spin_char = ".";
      draw_header( self,"Waiting for Trigger "+self.spin_char );
#     print( self.spin_char );

#   txt_list = [ "","","", cmd_txt ];
#   draw_txt_box( self, txt_list, x, y, w, h, False );
#   draw_header( self,cmd_txt);

  # Note: This moved to DOS-Box
  # 9th or display a text entry popup box
# if ( self.txt_entry == True ):
#   txt_list = ["Hello There"];
#   w = ( self.txt_width  * 20 );
#   h = ( self.txt_height *  3 );
#   x = ( self.screen_width  / 2 ) - ( w / 2 );
#   y = ( self.screen_height / 2 ) - ( h / 2 );
#   prompt = ">";
#   cursor = "_";
#   cmd_txt = prompt + self.key_buffer+cursor+"                ";
#   txt_list = [ self.txt_entry_caption, cmd_txt ];
#   draw_txt_box( self, txt_list, x, y, w, h, True  );

# Just for Debug, display the regions by drawing boxes around them.
# x = self.sig_name_start_x;
# w = self.sig_name_stop_x - x;
# y = self.sig_name_start_y;
# h = self.sig_name_stop_y - y;
# self.pygame.draw.rect( self.screen, self.color_fg ,(x,y,w,h), 1);

# x = self.sig_value_start_x;
# w = self.sig_value_stop_x - x;
# y = self.sig_value_start_y;
# h = self.sig_value_stop_y - y;
# self.pygame.draw.rect( self.screen, self.color_fg ,(x,y,w,h), 1);

# t1 = self.pygame.time.get_ticks();
# td = t1-t0;
# print td;
  return;


###############################################################################
# Draw an individual sample on a surface. Returns the x location of the last
# transition point, as this determines where and when new hex values are to be
# displayed. Its a bit of a tricky algorithm as it centers the values when
# zoom_x is large ( and there is room to display ). When zoom_x is small, it
# only displays values to the right of the last transition point assuming there
# are multiple samples with the same value. When zoom_x is small and the values
# are transitioning, display nothing.
# CRITICAL FUNCTION
def draw_sample(self,surface,val,val_last,last_transition_x,last_width, \
                format,format_bin, x,y):
  if ( self.gui_active == False ):
    return;

  # Draw "_/ \___/   \___" lines for binary format
# if ( format == "bin" ):
# if ( format_bin ):
#   x = x + 1; # Align transition with hex transition spot
#   if ( val == "0" ):
#     x1 = int(x);
#     x2 = int(x + self.zoom_x);
#     y1 = y + self.txt_height - 2;
#     y2 = y1;
#     self.pygame.draw.line(surface,self.color_fg,(x1,y1),(x2,y2),1);
#   elif ( val == "1" ):
#     x1 = int(x);
#     x2 = int(x + self.zoom_x);
#     y1 = y + 2;
#     y2 = y1;
#     self.pygame.draw.line(surface,self.color_fg,(x1,y1),(x2,y2),1);
#   if ( val != val_last ):
#     x1 = int(x);
#     x2 = int(x);
#     y1 = y + 2;
#     y2 = y + self.txt_height - 2;
#     self.pygame.draw.line(surface,self.color_fg,(x1,y1),(x2,y2),1);
  if ( False ):
    pass;

  # Draw "<012345678><><>" for hex format
  elif ( format == "hex" or format == "unsigned" or format == "signed" ):
    # display Hex if diff from last time OR last time there wasnt room
    # Note: Dramatic speedup (2x) by not doing this render here on hex
    # txt = self.font.render( val , True, self.color_fg );
    if ( format == "hex" ):
      if ( last_width != None ):
        txt_width = last_width;# For 13s render this saved 1s
      else:
        txt_width = len( val ) * self.txt_width;
        last_width = txt_width;
      # Drawing X's was costly in time 10s of 13s total. So Don't, just return
      if ( val == "XXXXXXXX" ):
        return (last_transition_x,last_width);
    else:
      txt = self.font.render( val , True, self.color_fg );
      txt_width = txt.get_width();

    # Is there room to display sample value?
    free_space_x = x - last_transition_x;
    if ( ( val != val_last                                      ) or
         ( val == val_last and txt_width+5 > free_space_x )  
       ):
      if ( val != val_last ):
        last_transition_x = x;
      free_space_x = x + self.zoom_x - last_transition_x;
      if ( txt_width+5 < free_space_x ):
        x3 = last_transition_x + int(free_space_x//2) - int(txt_width//2);
        txt = self.font.render( val , True, self.color_fg );
        surface.blit(txt, ( x3 , y+1 ));
   
      # If current sample is different than last, draw transition X
      if ( val != val_last ):
        y1 = y+0;
        y2 = y+self.txt_height - 2;
        # Draw crossing "X" for transitions
        x1 = x+2; x2 = x-0; 
        self.pygame.draw.line(surface,self.color_fg,(x1,y1),(x2,y2),1);
        self.pygame.draw.line(surface,self.color_fg,(x2,y1),(x1,y2),1);

    if ( val != val_last ):
      x1 = x+2; x2 = x-0 + self.zoom_x;# Dash for 'X' space
    else:
      x1 = x+0; x2 = x-0 + self.zoom_x;# Solid for non transition

    # Draw Line above and below the value
    if ( True ):
      y1 = y+0; y2 = y1;
      self.pygame.draw.line(surface,self.color_fg,(x1,y1),(x2,y2),1);
      y1 = y + self.txt_height - 2; y2 = y1;
      self.pygame.draw.line(surface,self.color_fg,(x1,y1),(x2,y2),1);

  return (last_transition_x,last_width);


###############################################################################
# draw_txt_box(): Draw a txt box from a list to (x,y) and crop to (w,h)
def draw_txt_box( self, txt_list, x, y, w, h, border ):
  if ( self.gui_active == False ):
    return;
  if ( border == True ):
    x1 = x; 
    y1 = y; 
    self.pygame.draw.rect( self.screen, self.color_bg,(x1,y1,w,h), 0 );
    self.pygame.draw.rect( self.screen, self.color_fg,(x1,y1,w,h), 3 );

  x1 = x + int(self.txt_width  / 2); # Provide whitespace
  w  = w - int(self.txt_width     ); # Provide whitespace
  y1 = y;
  for each in txt_list:
    txt = self.font.render( each , True, self.color_fg, self.color_bg );
    if ( ( y1 + self.txt_height ) < (y+h-(self.txt_height/2)) ):
      self.screen.blit(txt, (x1,y1), ( (0,0) , (w,h) ) );
      y1 += self.txt_height;
    else:
      break;# Outside of height region
  return;


def debug_vars( self ):
  print( "debug_vars()");
# print "self.sig_name_start_x  " + str( self.sig_name_start_x );
# print "self.sig_name_start_y  " + str( self.sig_name_start_y );
# print "self.sig_value_start_x " + str( self.sig_value_start_x );
# print "self.sig_value_start_y " + str( self.sig_value_start_y );
# print "self.sig_value_stop_x  " + str( self.sig_value_stop_x );
# print "self.sig_value_stop_y  " + str( self.sig_value_stop_y );
  return;


###############################################################################
# Take a sig_obj of N nibbles and return 2 new sig_objs of N/2 nibbles
def expand_signal( sig_obj ):
# num_nibs = sig_obj.bits_total / 4 ; # ie 8 nibs for 32 bits
  num_nibs = sig_obj.bits_total // 4 ; # ie 8 nibs for 32 bits
  pad_nib = False;
  if ( (num_nibs/2.0) != int(num_nibs/2) ):
    num_nibs += 1;# If 7 nibbles, convert to 8, etc so can divide in half
    pad_nib = True;
# num_nibs = num_nibs / 2;
  num_nibs = num_nibs // 2;
  num_bits = num_nibs * 4;

  new_signals = [];
  bits_top = "";
  bits_bot = "";

  sig_obj_top = signal(name = sig_obj.name + bits_top );# ie "foo(31:16)
  sig_obj_bot = signal(name = sig_obj.name + bits_bot );# ie "foo(15:0)

  for each in sig_obj.values:
    if ( pad_nib == True ):
      each = "0" + each; # Converts 28bits to 32bits, etc
    value = each[::-1];# Reverse "12345678" to "87654321" so that 8 is at [3:0]
    value_bot = ( value[0:num_nibs]          );    
    value_top = ( value[num_nibs:2*num_nibs] );
    sig_obj_bot.values.append( value_bot[::-1] );
    sig_obj_top.values.append( value_top[::-1] );
  sig_obj_bot.bits_total = num_nibs * 4;
  sig_obj_top.bits_total = num_nibs * 4;

  sig_obj_bot.bit_bot    = sig_obj.bit_bot;
  sig_obj_bot.bit_top    = sig_obj_bot.bit_bot + sig_obj_bot.bits_total - 1;

  sig_obj_top.bit_bot    = sig_obj.bit_bot + sig_obj_top.bits_total;
  sig_obj_top.bit_top    = sig_obj_top.bit_bot + sig_obj_top.bits_total - 1;

  sig_obj_top.is_expansion = True;
  sig_obj_bot.is_expansion = True;

  new_signals.append( sig_obj_top );
  new_signals.append( sig_obj_bot );
  return new_signals;

###############################################################################
# Take a signal of 1 nibbles and return 4 new binary signals
def expand_signal_nib2bin( sig_obj ):
  new_signals = [];
  bit_val = 8;
  bit_pos = sig_obj.bit_top;
  for i in range( 0,4, 1):
    new_bit = signal(name=sig_obj.name);
    new_bit.bits_total   = 1;
    new_bit.bit_bot      = bit_pos;
    new_bit.bit_top      = bit_pos;
    new_bit.format       = "bin";
    new_bit.is_expansion = True;
    for each in sig_obj.values:
      if ( (int( each, 16 ) & bit_val ) == 0 ):
#       bit = "0";
        bit = False;# VV
      else:
#       bit = "1";
        bit = True;# VV
      new_bit.values.append( bit );
    new_signals.append( new_bit );
#   bit_val = bit_val / 2;
    bit_val = bit_val // 2;
    bit_pos = bit_pos - 1;
  return new_signals;


# Give "/tb_resampler/u_dut/din(7:0)" return "din(7:0)"
def split_name_from_hier( hier_name ):
  words = "".join(hier_name.split()).split('/');
  return words[len( words )-1];


# load_format_delete_list() : This is similar to load_format() but is used to
# create a special delete list that tells the VCD parser to not bother with
# deleted signals
def load_format_delete_list( self, file_name ):
  new_signal_delete_list = [];
  try: # Read Input File 
    file_in = open( file_name , "r" );
    file_lines = file_in.readlines();
    file_in.close();  
  except:
    print( "ERROR Input File: "+file_name);
    return;
  for each in file_lines:
    words = " ".join(each.split()).split(' ') + [None] * 20;
    if ( words[0][0:1] != "#" ):
      name = words[0].lstrip();
      # Create a new sig_obj
      sig_obj = add_signal( self, name );
      # Assign Attribs 
      sig_obj.visible     = True;
      sig_obj.hidden      = False;
      sig_obj.deleted     = False;
      if ( "-hidden" in each ):
        sig_obj.hidden = True;
      if ( "-deleted" in each ):
        sig_obj.deleted = True;
      if ( "-invisible" in each ):
        sig_obj.visible = False;
      new_signal_delete_list.append( sig_obj );
  self.signal_delete_list = new_signal_delete_list[:];

# load_format() : A format file ( wave.txt ) looks like a ChipVault HLIST.TXT
# indentation indicates hierarchy order
#/tb_vcd_capture
#  /tb_vcd_capture/u_dut
#    clk
#    reset
#    /tb_vcd_capture/u_dut/mode
#
def load_format( self, file_name ):
  new_signal_list = [];
  try: # Read Input File 
    file_in = open( file_name , "r" );
    file_lines = file_in.readlines();
    file_in.close();  
  except:
    print( "ERROR Input File: "+file_name );

  # 1st Iteration assigns a space count to each hierarchy level
  # Makes the 1st one level 0
  hier_level = -1;
  hier_space = -1; 
  hier_dict  = {};  
  last_sig_obj = None;
  for each in file_lines:
    words = " ".join(each.split()).split(' ') + [None] * 20;
    if ( words[0][0:1] != "#" ):
      name = words[0].lstrip();

      # Create a new sig_obj
      sig_obj = add_signal( self, name );
      # Assign Attribs 
      sig_obj.collapsable = False;
      sig_obj.expandable  = False;
      sig_obj.visible     = True;
      sig_obj.hidden      = False;
      if ( "-bundle" in each ):
        sig_obj.type   = "bundle";
      if ( "-hidden" in each ):
        sig_obj.hidden = True;
      if ( "-deleted" in each ):
        sig_obj.deleted = True;
      if ( "-invisible" in each ):
        sig_obj.visible = False;
      if ( "-hex"       in each ):
        sig_obj.format  = "hex";
      if ( "-unsigned"  in each ):
        sig_obj.format  = "unsigned";
      if ( "-signed"    in each ):
        sig_obj.format  = "signed";
      if ( "-nickname"  in each ):
        for ( i , each_word ) in enumerate( words ):
          if ( each_word == "-nickname" ):
            if ( words[i+1] != "None" ):
              sig_obj.nickname = words[i+1];# Assume this word exists

      # Calculate Hierarchy Location by counting whitespace
      space_cnt = len( each ) - len( each.lstrip() );
      if ( space_cnt > hier_space ):
        hier_space = space_cnt;
        hier_level += 1;
        hier_dict[ hier_space ] = hier_level;
        # Since the hierarchy level got deeper, the last guy is a parent
        # so assign parent attribute collapsable. 
        # Assign [+] or [-] based on visibility of 1st object
        if ( last_sig_obj != None ):
          if ( sig_obj.visible == False ):
            last_sig_obj.collapsable = False;
            last_sig_obj.expandable  = True;
          else:
            last_sig_obj.collapsable = True;
            last_sig_obj.expandable  = False;
      else:
        hier_level = hier_dict[ space_cnt ];
        hier_space = space_cnt;

      sig_obj.hier_level = hier_level;
      new_signal_list.append( sig_obj );
      last_sig_obj = sig_obj;

  self.signal_list = new_signal_list[:];

  # Unselect Everything
  self.sig_obj_sel = None;
  for sig_obj in self.signal_list:
    sig_obj.selected = False;# DeSelect All

  return;

# Given a name, return an object that matches the name or create a new one
def add_signal( self, name ):
  sig_obj = None;
  # Look for the name in the signal list and assign to sig_obj if found
  for each in self.signal_list:
    # Find object of signal_hier_name in old signal list, append to new
    # after assigning some attributes
    if ( ( (each.hier_name + "/" + each.name) == name ) or \
         ( (                       each.name) == name )    ):
      sig_obj = each;
  # If name wasnt found, create new object ( Divider, Group, etc )
  if ( sig_obj == None ):
    sig_obj = signal( name= split_name_from_hier( name ) );
    sig_obj.type          = ""; "signal", "group", "endgroup", "divider"
    sig_obj.bits_per_line = 0;
    sig_obj.bits_total    = 0;
    sig_obj.bit_top       = 0;
    sig_obj.bit_bot       = 0;
    sig_obj.format        = "";
  return sig_obj;


def add_wave( self, words ):
    # Change "foo(7:0)" to "foo" so that it matches hier_name+"/"+name
    signal_hier_name = words[2];
    i = signal_hier_name.find("(");
    if ( i != -1 ):
      signal_hier_name = signal_hier_name[0:i];# Strip the rip

    if ( words[0] == "add_wave" ):
      sig_obj = None;
      # Look for the name in the signal list and assign to sig_obj if found
      for each in self.signal_list:
        # Find object of signal_hier_name in old signal list, append to new
        # after assigning some attributes
        if ( ( (each.hier_name + "/" + each.name) == signal_hier_name ) or \
             ( (                       each.name) == signal_hier_name )    ):
          sig_obj = each;
      # If name wasnt found, create new object ( Divider, Group, etc )
      if ( sig_obj == None ):
        sig_obj = signal( name= split_name_from_hier( signal_hier_name ) );
        sig_obj.type          = words[1];# "group", "endgroup", "divider"
        sig_obj.bits_per_line = 0;
        sig_obj.bits_total    = 0;
        sig_obj.bit_top       = 0;
        sig_obj.bit_bot       = 0;
        sig_obj.format        = "";

      # Search for "-hidden" and turn off visible if found
      sig_obj.visible = True;  # Default to visible
      sig_obj.grouped = False; # Default to not grouped
      for ( i , each_word ) in enumerate( words ):
        if ( each_word == "-hidden" ):
          sig_obj.visible = False; # Hide
        elif ( each_word == "-expandable" ):
          sig_obj.expandable  = True;
          sig_obj.collapsable = False;
        elif ( each_word == "-collapsable" ):
          sig_obj.collapsable = True;
          sig_obj.expandable  = False;
        elif ( each_word == "-grouped" ):
          sig_obj.grouped = True;  # Part of a group
        elif ( each_word == "-nickname" ):
          sig_obj.nickname = words[i+1];# Assume this word exists;
      # Append old object to new list
      return sig_obj;


###############################################################################
# Dump the signal_list to an ASCII hlist.txt 
def save_format( self, file_name, selected_only ):
  log( self, ["save_format() " + file_name ] );
  out_list = [];
  for sig_obj in self.signal_list:
#   if ( sig_obj.visible == True ):
#     hier_str = (sig_obj.hier_level*"  ");
#   else:
#     hier_str = "# " + ((sig_obj.hier_level-1)*"  ");
    hier_str = (sig_obj.hier_level*"  ");

    attribs = "";
    if ( sig_obj.type == "bundle" ):
      attribs += " -bundle";
    if ( sig_obj.hidden == True ):
      attribs += " -hidden";
    if ( sig_obj.visible == False ):
      attribs += " -invisible";
    if ( sig_obj.format != "bin" and sig_obj.format != "" ):
      attribs += " -" + sig_obj.format;
    if ( sig_obj.nickname != "" ):
      attribs += " -nickname " + sig_obj.nickname;
   
    rts = hier_str + sig_obj.hier_name + "/" + sig_obj.name + " " + attribs;
    if ( selected_only == False or each.selected == True ):
#     file_out.write( rts + "\n" );
      out_list += [ rts ];

  # When SUMP2 crashes, it tends to leave empty signal list, so keep old file
  if ( len( out_list ) > 0  and self.vcd_import == False ):
    import os;
    if (  os.path.exists( file_name ) == True ):
      os.remove( file_name );
    file_out = open( file_name , "w" ); # Append versus r or w
    for each in out_list:
      file_out.write( each + "\n" );
    print( "closing ", file_name);
    file_out.close();  
  else:
    print("ERROR: Empty Signal List");
  return;


########################################################
# Given a VCD or TXT file, make signal_list from it
def file2signal_list( self, file_name ):
  log( self, ["file2signal_list()"] );
  import os.path
  file_ext = os.path.splitext(file_name)[1].lower();
  if ( file_ext != ".vcd" ):
    txtfile2signal_list( self, file_name );
  else:
    vcdfile2signal_list( self, file_name );
  return;


########################################################
# Write a DWORD to specified SUMP Nibble Ctrl Address 
#def sump_wr( self, addr, data ):
#  self.bd.wr( self.sump_ctrl, [ addr ] );
#  self.bd.wr( self.sump_data, [ data ] );
#  return;


########################################################
# Read one or more DWORDs from SUMP Nibble Ctrl Address 
# if address None - don't change from existing Address
#def sump_rd( self, addr, num_dwords = 1):
#  if ( addr != None ):
#    self.bd.wr( self.sump_ctrl, [ addr ] );
#  return self.bd.rd( self.sump_data, num_dwords, repeat = True); 


########################################################
# This is for removing an item from the popup list. It
# handles going down a hierarchy level into a sublist
def list_remove( my_list, item ):
  try:
    my_list.remove( item );
  except:
    None;
  for each in my_list:
    if ( type( each ) == list ):
      try:
        each.remove( item );
      except:
        None;
  return;
    

########################################################
# Establish connection to Sump2 hardware
def sump_connect( self ):
  log( self, ["sump_connect()"] );
  if ( self.os_sys == "XXLinux" ):
    import spidev;# SPI Interface Module for Pi
    self.spi_port = spidev.SpiDev();
    self.spi_port.open(0,1);# Note: icoboard uses CE0 for Mach, CE1 for Ice
    self.spi_port.max_speed_hz = 32000000;# 1-32 MHz typical
    self.mb = mesa_bus( self.spi_port, self.os_sys);# Establish a MesaBus link
    self.bd = local_bus( self.mb, False );# Establish a LocalBus link
  else:
    self.bd=Backdoor(  self.vars["bd_server_ip"],
                       int( self.vars["bd_server_socket"], 10 ) );# Note dec
    if ( self.bd.sock == None ):
      txt = "ERROR: Unable to locate BD_SERVER";
      self.fatal_msg = txt;
      print( txt );
      log( self, [ txt ] );
      return False;
  
  self.sump = Sump2( self.bd, int( self.vars["sump_addr"],16 ) );
  self.sump.rd_cfg();# populate sump.cfg_dict[] with HW Configuration
  if ( self.sump.cfg_dict['hw_id'] != 0xABBA ):
    txt = "ERROR: Unable to locate SUMP Hardware";
    self.fatal_msg = txt;
    print( txt );
    log( self, [ txt ] );
    return False;

  # Adjust the GUI menu to remove features that don't exist in this hardware
  if ( self.sump.cfg_dict['nonrle_dis'] == 1 ):
    list_remove( self.popup_list_values, "Acquire_Normal");
    list_remove( self.popup_list_values, "Acquire_Single");
    list_remove( self.popup_list_values, "Acquire_Continuous");
    list_remove( self.popup_list_values, "Arm_Normal");

  if ( self.sump.cfg_dict['rle_en'] == 0 ):
    self.popup_list_values.remove("Acquire_RLE_1x");
    self.popup_list_values.remove("Acquire_RLE_8x");
    self.popup_list_values.remove("Acquire_RLE_64x");
    self.popup_list_values.remove("Arm_RLE");

  if ( self.sump.cfg_dict['trig_wd_en'] == 0 ):
    list_remove( self.popup_list_names, "Trigger_Watchdog");
    list_remove( self.popup_list_names,  "sump_watchdog_time");
  if ( self.sump.cfg_dict['data_en']    == 0 ):
    self.popup_list_names.remove("Set_Data_Enable");
    self.popup_list_names.remove("Clear_Data_Enable");
  if ( self.sump.cfg_dict['pattern_en']    == 0 ):
    self.popup_list_names.remove("Set_Pattern_0");
    self.popup_list_names.remove("Set_Pattern_1");
    self.popup_list_names.remove("Clear_Pattern_Match");
  if ( self.sump.cfg_dict['trig_nth_en']    == 0 ):
    list_remove( self.popup_list_names, "sump_trigger_nth");
  if ( self.sump.cfg_dict['trig_dly_en']    == 0 ):
    list_remove( self.popup_list_names, "sump_trigger_delay");

  sump_size = self.sump.cfg_dict['ram_len'];
  self.sump.wr( self.sump.cmd_wr_user_ctrl,     0x00000000 );
  self.sump.wr( self.sump.cmd_wr_watchdog_time, 0x00001000 );
  self.sump.wr( self.sump.cmd_wr_user_pattern0, 0x000FFFFF );# Pattern Mask
  self.sump.wr( self.sump.cmd_wr_user_pattern1, 0x000055FF );# Pattern
  self.sump.wr( self.sump.cmd_wr_trig_type,     self.sump.trig_pat_ris );
  self.sump.wr( self.sump.cmd_wr_trig_field,    0x00000000 );#
  self.sump.wr( self.sump.cmd_wr_trig_dly_nth,  0x00000001 );#Delay + nTh
# self.sump.wr( self.sump.cmd_wr_trig_position, sump_size/2);#SamplesPostTrig
  self.sump.wr( self.sump.cmd_wr_trig_position, sump_size//2);#SamplesPostTrig
# self.sump.wr( self.sump.cmd_wr_rle_event_en,  0xFFFFFFF0 );#RLE event en
  self.sump.wr( self.sump.cmd_wr_rle_event_en,  0xFFFFFFFF );#RLE event en

  self.sump.wr( self.sump.cmd_state_reset,      0x00000000 );
# self.sump.wr( self.sump.cmd_state_arm,        0x00000000 );
  return True;


########################################################
# Talk to sump2 hardware and arm for acquisition ( or dont )
# determining the BRAM depth.
def sump_arm( self, en ):
  log( self, ["sump_arm()"]);
  if ( en == True ):
    try:
      trig_type     =      self.vars["sump_trigger_type"  ];
      trig_field    = int( self.vars["sump_trigger_field" ],16 );
      rle_event_en  = int( self.vars["sump_rle_event_en"  ],16 );
      trig_delay    = int( self.vars["sump_trigger_delay" ],16 );
      trig_nth      = int( self.vars["sump_trigger_nth"   ],16 );
      data_en       = int( self.vars["sump_data_enable"   ],16 );
      user_ctrl     = int( self.vars["sump_user_ctrl"     ],16 );
      user_pattern0 = int( self.vars["sump_user_pattern0" ],16 );
      user_pattern1 = int( self.vars["sump_user_pattern1" ],16 );
      wd_time       = int( self.vars["sump_watchdog_time" ],16 );

      # Convert trigger ASCII into integers
      if ( trig_type == "or_rising" ):
        trig_type_int = self.sump.trig_or_ris;
      elif ( trig_type == "or_falling" ):
        trig_type_int = self.sump.trig_or_fal;
      elif ( trig_type == "and_rising" ):
        trig_type_int = self.sump.trig_and_ris;
      elif ( trig_type == "watchdog" ):
        trig_type_int = self.sump.trig_watchdog;
      elif ( trig_type == "pattern_rising" ):
        trig_type_int = self.sump.trig_pat_ris;
      else:
        trig_type_int = 0;

      # Pack 16bit trig_delay and trig_nth into single dword
      trig_dly_nth  = ( trig_delay << 16 ) + ( trig_nth << 0 );
      if ( trig_dly_nth == 0x0 ):
        print("WARNING: trig_nth is ZERO!!"); 

      print("%08x" % trig_type_int    );
      print("%08x" % trig_field       );
      print("%08x" % trig_dly_nth     );
      print("%08x" % data_en          );
      print("%08x" % user_ctrl        );
      print("%08x" % user_pattern0    );
      print("%08x" % user_pattern1    );

      self.sump.wr( self.sump.cmd_wr_trig_type ,    trig_type_int );
      self.sump.wr( self.sump.cmd_wr_trig_field,    trig_field   );
      self.sump.wr( self.sump.cmd_wr_trig_dly_nth,  trig_dly_nth );
      self.sump.wr( self.sump.cmd_wr_rle_event_en,  rle_event_en );
      self.sump.wr( self.sump.cmd_wr_user_data_en,  data_en  );
      self.sump.wr( self.sump.cmd_wr_user_ctrl   ,  user_ctrl);
      self.sump.wr( self.sump.cmd_wr_watchdog_time, wd_time  );
      self.sump.wr( self.sump.cmd_wr_user_pattern0, user_pattern0);
      self.sump.wr( self.sump.cmd_wr_user_pattern1, user_pattern1);
      self.sump.wr( self.sump.cmd_state_reset,      0x00000000 );
      self.sump.wr( self.sump.cmd_state_arm,        0x00000000 );
    except:
      print("ERROR: Unable to convert sump variables to hex");
  else:
    self.sump.wr( self.sump.cmd_state_reset,   0x00000000 );

# self.bd.wr( 0x0,[0] );
# self.bd.wr( 0x0,[0] );
# rts = self.bd.rd( 0x0,1 )[0];
# rts = self.bd.rd( 0x0,1 )[0];
# self.bd.wr( 0x0,[0] );
  return;

#   self.trig_and_ris          = 0x00;# Bits AND Rising
#   self.trig_and_fal          = 0x01;# Bits AND Falling
#   self.trig_or_ris           = 0x02;# Bits OR  Rising
#   self.trig_or_fal           = 0x03;# Bits OR  Falling
#   self.trig_pat_ris          = 0x04;# Pattern Match Rising
#   self.trig_pat_fal          = 0x05;# Pattern Match Falling
#   self.trig_in_ris           = 0x06;# External Input Trigger Rising
#   self.trig_in_fal           = 0x07;# External Input Trigger Falling
  return;

#   self.trig_and_ris          = 0x00;# Bits AND Rising
#   self.trig_and_fal          = 0x01;# Bits AND Falling
#   self.trig_or_ris           = 0x02;# Bits OR  Rising
#   self.trig_or_fal           = 0x03;# Bits OR  Falling
#   self.trig_pat_ris          = 0x04;# Pattern Match Rising
#   self.trig_pat_fal          = 0x05;# Pattern Match Falling
#   self.trig_in_ris           = 0x06;# External Input Trigger Rising
#   self.trig_in_fal           = 0x07;# External Input Trigger Falling
  return;

#   self.trig_and_ris          = 0x00;# Bits AND Rising
#   self.trig_and_fal          = 0x01;# Bits AND Falling
#   self.trig_or_ris           = 0x02;# Bits OR  Rising
#   self.trig_or_fal           = 0x03;# Bits OR  Falling
#   self.trig_pat_ris          = 0x04;# Pattern Match Rising
#   self.trig_pat_fal          = 0x05;# Pattern Match Falling
#   self.trig_in_ris           = 0x06;# External Input Trigger Rising
#   self.trig_in_fal           = 0x07;# External Input Trigger Falling
  return;

#   self.trig_and_ris          = 0x00;# Bits AND Rising
#   self.trig_and_fal          = 0x01;# Bits AND Falling
#   self.trig_or_ris           = 0x02;# Bits OR  Rising
#   self.trig_or_fal           = 0x03;# Bits OR  Falling
#   self.trig_pat_ris          = 0x04;# Pattern Match Rising
#   self.trig_pat_fal          = 0x05;# Pattern Match Falling
#   self.trig_in_ris           = 0x06;# External Input Trigger Rising
#   self.trig_in_fal           = 0x07;# External Input Trigger Falling
#   self.cmd_wr_trig_type      = 0x04;
#   self.cmd_wr_trig_field     = 0x05;# Correspond to Event Bits
#   self.cmd_wr_trig_dly_nth   = 0x06;# Trigger Delay and Nth
#   self.cmd_wr_trig_position  = 0x07;# Samples post Trigger to Capture
#   self.cmd_wr_rle_event_en   = 0x08;# Enables events for RLE detection
#   self.cmd_wr_ram_ptr        = 0x09;# Load specific pointer.
#   self.cmd_wr_ram_page       = 0x0a;# Load DWORD Page.
#   self.cmd_rd_hw_id_rev      = 0x0b;
#   self.cmd_rd_ram_width_len  = 0x0c;
#   self.cmd_rd_sample_freq    = 0x0d;
#   self.cmd_rd_trigger_ptr    = 0x0e;
#   self.cmd_rd_ram_data       = 0x0f;
#   self.cmd_wr_user_ctrl      = 0x10;
#   self.cmd_wr_user_pattern0  = 0x11;# Also Mask    for Pattern Matching
#   self.cmd_wr_user_pattern1  = 0x12;# Also Pattern for Pattern Matching
#   self.cmd_wr_user_data_en   = 0x13;# Special Data Enable Capture Mode


########################################################
# Dump acquired data to a file. This is a corner turn op
def sump_save_txt( self, file_name, mode_vcd=False, mode_list=False,cmd=None ):
  log( self, ["sump_save_txt()"]);
  print("sump_save_txt()");
  ram_dwords = self.sump.cfg_dict['ram_dwords'];
  ram_bytes  = self.sump.cfg_dict['ram_event_bytes'];
  ram_len    = self.sump.cfg_dict['ram_len'];
  events = ram_bytes * 8;
  start_time = time.time();

  file_out  = open( file_name, 'w' );

  if ( mode_vcd == False ):
    name_str     = "#";
    nickname_str = "#";
  else:
    name_str     = "";
    nickname_str = "";

  percent = 0;
  percent_total = ((1.0)*self.max_samples );
  print("max_samples = " + str( self.max_samples ) );

  # Speed things up by making a list of only the visible (non hidden) sig_objs
  sig_vis_list = [];
  for sig_obj in self.signal_list:
    if ( sig_obj.hidden == False ):
      sig_vis_list += [ sig_obj ];


# HERE05
  # For list view, only save the selected signals
  if ( mode_list ):
    old_vis_list = sig_vis_list[:];
    sig_vis_list = [];
    for sig_obj in old_vis_list:
      if ( sig_obj.selected ):
        sig_vis_list += [ sig_obj ];

  if ( cmd == "list_view_cursors" or cmd == "save_vcd_cursors" ):
    sample_left = None;
    sample_right = None;
    for cur_obj in self.cursor_list:
      if   ( sample_left  == None ): sample_left  = cur_obj.sample;
      elif ( sample_right == None ): sample_right = cur_obj.sample;
    if ( sample_left != None and sample_right != None ):
      if ( sample_left > sample_right ):
        sample_left, sample_right = sample_right, sample_left;# Swap
      if ( sample_left  < 0                ): sample_left  = 0;
      if ( sample_right > self.max_samples ): sample_right = self.max_samples;
    first_sample = sample_left;
    last_sample  = sample_right;
  else:
    first_sample = 0;
    last_sample  = self.max_samples;

  # 10x performance improvement making these "fast lists" once up front
  fast_event_list = [];
  for j in range( ram_bytes*8, 0, -1):
    for sig_obj in sig_vis_list:      
      if ( sig_obj.name == "event[%d]" % (j-1) and sig_obj.hidden == False ):
        fast_event_list += [ sig_obj ];

  fast_dword_list = [];
  for j in range( 0, ram_dwords, 1 ):
    for sig_obj in sig_vis_list:      
      if ( sig_obj.name == "dword[%d]" % j and sig_obj.hidden == False ):
        fast_dword_list += [ sig_obj ];

  fast_bundle_list = [];
  for sig_obj in sig_vis_list:      
    if ( sig_obj.type == "bundle" and sig_obj.hidden == False ):
      fast_bundle_list += [ sig_obj ];

  # Now go thru all the time samples and do the corner turning
  first_line = True;# Generate the signal name and time unit legend
  for i in range( first_sample, last_sample, 1):
    # This takes a while, so calculate and print percentage as it goes by
    if ( ((i*1.0) / percent_total) > percent ):
      perc_str = ( str( int(100*percent) ) + "%");
      draw_header( self, "VCD Conversion " + perc_str );
      percent += .05;

    txt_str = "";
    m = 0;
    # Iterate the list searching for all the events in binary order
#   for j in range( ram_bytes*8, 0, -1):
#     for sig_obj in sig_vis_list:      
#       if ( sig_obj.name == "event[%d]" % (j-1) and sig_obj.hidden == False ):
    if ( True ):
      if ( True ):
        for sig_obj in fast_event_list:
          if ( i >= len( sig_obj.values )):
            txt_str += "X";
          else:
            # VV convert from True to "1"
            value = sig_obj.values[i];
            if ( isinstance( value, bool ) ):
              if ( value ):
                value = "1";
              else:
                value = "0";
            txt_str += value;
          m +=1;
          if ( m == 8 or ( m == 1 and mode_vcd == True ) ):
            txt_str += " ";# Add whitespace between each byte group
            m = 0;
          if ( i == first_sample ):
            name_str     += sig_obj.name + " ";
            if ( sig_obj.nickname != "" ):
              nickname_str += sig_obj.nickname + " ";
            else:
              nickname_str += sig_obj.name + " ";
    if ( mode_vcd == False ):
      txt_str += " ";# Add whitespace between events and dwords

    # Iterate the list searching for all the bundles        
    if ( True ):
      if ( True ):
        for sig_obj in fast_bundle_list:
          if ( i == first_sample ):
            name_str     += sig_obj.name + " ";
            nick = sig_obj.nickname;
            if ( nick == None or nick == "" ):
              nick = sig_obj.name;
            nick = nick.replace( "[", "_" );# vcd2wlf fix for 2D arrays
            nick = nick.replace( "]", "_" );# vcd2wlf fix for 2D arrays
            nickname_str += nick + " ";
          txt_str += sig_obj.values[i];
          txt_str += " ";# Add whitespace between each dword

    # Iterate the list searching for all the dwords in order
#   for j in range( 0, ram_dwords, 1 ):
#     for sig_obj in sig_vis_list:      
#       if ( sig_obj.name == "dword[%d]" % j and sig_obj.hidden == False ):
    if ( True ):
      if ( True ):
        for sig_obj in fast_dword_list:
          if ( i >= len( sig_obj.values )):
            txt_str += "XXXXXXXX";
          else:
            txt_str += sig_obj.values[i];
          txt_str += " ";# Add whitespace between each dword
          if ( i == first_sample ):
            name_str     += sig_obj.name + " ";
            nick = sig_obj.nickname;
            nick = nick.replace( "[", "_" );# vcd2wlf fix for 2D arrays
            nick = nick.replace( "]", "_" );# vcd2wlf fix for 2D arrays
            nickname_str += nick + " ";
    if ( first_line ):
      freq_mhz = self.sump.cfg_dict['frequency'];
      freq_ps  = 1000000.0 / freq_mhz;
      file_out.write( nickname_str + " " + ("%f" % freq_ps ) + "\n" );
      first_line = False;
    file_out.write( txt_str + "\n" );
  file_out.close();
  stop_time = time.time();
  total_time = stop_time - start_time;
  print("VCD Generation Time = %d" % total_time );
  return;


########################################################
# Dump acquired data to a file
def sump_save_vcd( self ):
# print("ERROR: sump_save_vcd() does not yet exist!");
  return;

def refresh( self ):
  if ( self.mode_cli == False ):
    import pygame;
    pygame.event.get();# Avoid "( Not Responding )"
    pygame.display.update();
  return;

#########################################################################
# Dump acquired data from SUMP engine and merge with existing signal list
def sump_dump_data( self ):
  log( self, ["sump_dump_data()"]);
  ram_dwords = self.sump.cfg_dict['ram_dwords'];
  ram_bytes  = self.sump.cfg_dict['ram_event_bytes'];
  ram_rle    = self.sump.cfg_dict['ram_rle'];

# ram_len    = self.sump.cfg_dict['ram_len'];
  ( ram_pre, ram_post, ram_len, ram_phys ) = sump_ram_len_calc(self);

  events = ram_bytes * 8;# Example, 32 events total for 4 ram_bytes
  self.dwords_start = 0;
  self.dwords_stop  = ram_phys;
  start_time = time.time();
  # Event Signals
  rd_page = 0;
  dump_data = sump_dump_var_ram(self,rd_page = rd_page );
  for i in range( 0, events, 1 ):
    txt = ("Event %d of %d" % ( i+1, events ) );
    draw_header( self, "sump_dump_data() " + txt); 
    refresh( self );
    # Iterate the list of signals and find one with correct physical name
    my_signal = None;
    for each_signal in self.signal_list:
      if ( each_signal.name == "event[%d]" % i ):
        my_signal = each_signal;
    if ( my_signal != None ):
      my_signal.values = [];
      my_signal.format = "bin";
      my_signal.bits_total = 1;
      my_signal.bit_top   = 0;
      my_signal.bit_bot   = 0;
      bit_val = (1 << i );
      for j in range( 0, ram_len, 1):
        if ( ( dump_data[j] & bit_val ) != 0x0 ):
#         bit = "1";
          bit = True;# VV
        else:
#         bit = "0";
          bit = False;# VV
        my_signal.values.append( bit );

  # DWORD Signals
  for i in range( 0, ram_dwords , 1 ):
    txt = ("DWORD %d" % i );
    txt = ("DWORD %d of %d" % ( i+1, ram_dwords ) );
    draw_header( self, "sump_dump_data() " + txt); 
    refresh(self);
    dump_data = sump_dump_var_ram(self, rd_page = ( 0x10 + i ) );
    # Iterate the list of signals and find one with correct physical name
    my_signal = None;
    for each_signal in self.signal_list:
      if ( each_signal.name == "dword[%d]" % i ):
        my_signal = each_signal;
    if ( my_signal != None ):
      my_signal.values = [];
      my_signal.format = "hex";
      my_signal.bits_total = 32;
      my_signal.bit_top   = 31;
      my_signal.bit_bot   = 0;
      for j in range( 0, ram_len, 1):
        my_signal.values.append( "%08x" % dump_data[j] );
  stop_time = time.time();
  total_time = stop_time - start_time;
  print("Download Time = %d" % total_time );

  sump_bundle_data( self );
  recalc_max_samples( self );
  trig_i = (self.max_samples // 2);# Trigger fixed at 50/50 for now
  return trig_i;


#########################################################################
# Search the signal list for any type bundles and calculate their sample
# values based on their children
def sump_bundle_data( self ):
  my_signal = None;
# for each_signal in self.signal_list:
  for ( signal_cnt, each_signal ) in enumerate( self.signal_list ):
    if ( my_signal != None ):
      if ( each_signal.hier_level > my_level ):
        rip_list += [ each_signal.values ];

#     else:
      if ( each_signal.hier_level == my_level or 
           signal_cnt == len(self.signal_list)-1 ):
        value_list =      zip( *rip_list );
        my_signal.values = [];
        my_signal.bit_top    = len( rip_list )-1;
        my_signal.bit_bot    = 0;
        my_signal.bits_total = my_signal.bit_top + 1;
        for each_sample in value_list:
          bit = 0;
          for (i,each_bit) in enumerate ( each_sample ):
#           if ( each_bit == "1" ):
            if ( each_bit ):    # VV
              bit += ( 1 << i );
          my_signal.values += [ "%x" % bit ];
        my_signal = None;
    if ( each_signal.type == "bundle" ):
      my_signal = each_signal;
      my_level  = my_signal.hier_level;
      rip_list  = [];
  return;


#########################################################################
# Dump acquired data from SUMP engine and merge with existing signal list
def sump_dump_rle_data( self ):
  print("sump_dump_rle_data()");
  log( self, ["sump_dump_rle_data()"]);
  ram_dwords = self.sump.cfg_dict['ram_dwords'];
  ram_bytes  = self.sump.cfg_dict['ram_event_bytes'];
  ram_rle    = self.sump.cfg_dict['ram_rle'];
  
  rle_pre_trig_len          = self.vars["sump_rle_pre_trig_len" ];
  rle_post_trig_len         = self.vars["sump_rle_post_trig_len" ];
  trig_delay                = int( self.vars["sump_trigger_delay" ],16 );
# self.undersample_rate     = int(self.vars["sump_rle_undersample" ],16);

# if ( self.acq_state == "acquire_rle_undersampled" ):
#   self.undersample_data = True;
  if   ( self.acq_state == "acquire_rle_1x" ):
    self.undersample_data = False;
    self.undersample_rate = 1;
  elif ( self.acq_state == "acquire_rle_4x" ):
    self.undersample_data = True;
    self.undersample_rate = 4;
  elif ( self.acq_state == "acquire_rle_8x" ):
    self.undersample_data = True;
    self.undersample_rate = 8;
  elif ( self.acq_state == "acquire_rle_16x" ):
    self.undersample_data = True;
    self.undersample_rate = 16;
  elif ( self.acq_state == "acquire_rle_64x" ):
    self.undersample_data = True;
    self.undersample_rate = 64;

  if   ( self.acq_state == "acquire_rle_lossy" ):
    self.rle_lossy = True;
  else:
    self.rle_lossy = False;

  rle_pre_trig_len  *= self.undersample_rate;
  rle_post_trig_len *= self.undersample_rate; 

# print("##");
# print( rle_pre_trig_len );
# print( rle_post_trig_len );
# ram_len    = self.sump.cfg_dict['ram_len'];

  ( ram_pre, ram_post, ram_len, ram_phys ) = sump_ram_len_calc(self);

  events = ram_bytes * 8;# Example, 32 events total for 4 ram_bytes

  # Event Signals
  rd_page = 0;
  print("sump_dump_ram( rle_data )");
  rle_data = sump_dump_ram(self,rd_page = 0x2, rd_ptr = 0x0000 );
  print("sump_dump_ram( rle_time )");
  rle_time = sump_dump_ram(self,rd_page = 0x3, rd_ptr = 0x0000 );
  rle_list = list(zip( rle_time, rle_data ));

# print("Oy");
# print( len(rle_time ) );
# print( len(rle_data ) );
# print( len(rle_list ) );

  print("process_rle()");
  (start_t,stop_t, pre_trig, post_trig ) = process_rle(self,rle_list);

  # HERE30
  # It is possible for the RLE samples to be culled shorter than the DWORD
  # window. Check for that case and pad the RLE data in order to fit DWORDs
  if ( ram_dwords != 0 ):
    (trig_time,trig_data )   = pre_trig[-1];
    (start_time,start_data ) = pre_trig[ 0];
    (stop_time,stop_data )   = post_trig[-1];
    if ( ( trig_time - start_time ) < ( ram_phys//2 ) ):
      print("WARNING: Need more pre-trig samples to fit DWORDs in");
      print("start_t was %08x" % start_t );
      start_t = start_t - ram_phys//2;
      print("start_t now %08x" % start_t );
      pre_trig = [( (start_time-(ram_phys//2)), start_data ) ] + pre_trig[:];
    if ( ( stop_time - trig_time ) < ( ram_phys//2 ) ):
      print("Need more post-trig samples to fit DWORDs in");
      print("stop_t was %08x" % stop_t );
      stop_t = stop_t + ram_phys//2;
      print("stop_t now %08x" % stop_t );
      post_trig = post_trig[:] + [( (stop_time+(ram_phys//2)), stop_data ) ];

  rle_hex_list = [];
  for ( rle_time, rle_data ) in ( pre_trig + post_trig ):
    rle_hex_list += [ ("%08x %08x" % ( rle_time, rle_data ) )];
  list2file( self, "sump2_rle_dump.txt", rle_hex_list );

  print("expand_rle()");
  start_time = time.time();
  (dump_data,trig_i) = expand_rle( self, start_t,stop_t,pre_trig,post_trig );
# print( len( dump_data ) );

  txt ="Generating RLE Event Signal List of values";
  log( self, [ txt ]); print( txt ); refresh( self ); draw_screen( self );
  for i in range( 0, events, 1 ):
    txt = ("Event %d of %d" % ( i+1, events ) );
    draw_header( self, "sump_dump_rle_data() " + txt); 
    refresh( self );
    # Iterate the list of signals and find one with correct physical name
    my_signal = None;
    for each_signal in self.signal_list:
      if ( each_signal.name == "event[%d]" % i ):
        my_signal = each_signal;
    if ( my_signal != None ):
      my_signal.values = [];
      my_signal.format = "bin";
      my_signal.bits_total = 1;
      my_signal.bit_top   = 0;
      my_signal.bit_bot   = 0;
      bit_val = (1 << i );
      if ( my_signal.hidden == False ):
        for j in range( 0, len( dump_data ) , 1):
          if ( ( dump_data[j] & bit_val ) != 0x0 ):
#           bit = "1";
            bit = True;# VV
          else:
#           bit = "0";
            bit = False;# VV
          my_signal.values.append( bit );
        if ( self.undersample_data == True ):
          rle_undersample_signal( self, self.undersample_rate, my_signal );

  # Align non-RLE dword data with the RLE samples by calculating Null samples
  # before and after trigger event
  #  |                T                |  : RLE dump_data
  #  | pre_pad | dword_data | post_pad |
  pre_pad  = ( trig_delay + 2 + trig_i - ram_phys//2) * \
               [ "XXXXXXXX" ];
  post_pad = ( len( dump_data ) - len( pre_pad ) - ram_phys - trig_delay ) * \
               [ "XXXXXXXX"];

  # Remember where DWORDs are within RLE samples and use to speed up rendering
  # by not bothering with DWORDs if outside of current view.
  self.dwords_start = len(pre_pad);
  self.dwords_stop  = self.dwords_start + ram_phys;

  if ( self.undersample_data == False ):
    txt ="Generating RLE DWORD Signal List of values";
    log( self, [ txt ]); print( txt ); refresh( self ); draw_screen( self );
    # DWORD Signals. Just Null out all samples since RLE acquisition
    trig_ptr = self.sump.rd( self.sump.cmd_rd_trigger_ptr )[0];
    ram_ptr  = 0xFFFF & (trig_ptr - ram_phys//2 );
    for i in range( 0, ram_dwords , 1 ):
      txt = ("DWORD %d of %d" % ( i+1, ram_dwords ) );
      draw_header( self, "sump_dump_rle_data() " + txt); 
      refresh( self );
      dump_data = sump_dump_ram(self,rd_page = (0x10+i), rd_ptr = ram_ptr );
      # Iterate the list of signals and find one with correct physical name
      my_signal = None;
      for each_signal in self.signal_list:
        if ( each_signal.name == "dword[%d]" % i ):
          my_signal = each_signal;
      if ( my_signal != None ):
        my_signal.values = pre_pad[:];
        my_signal.format = "hex";
        my_signal.bits_total = 32;
        my_signal.bit_top   = 31;
        my_signal.bit_bot   = 0;
        for j in range( 0, ram_phys, 1):
          my_signal.values.append( "%08x" % dump_data[j] );
        my_signal.values += post_pad;
  # Undersampling Events, so just create NULL DWORDs
  else:
    for i in range( 0, ram_dwords , 1 ):
      my_signal = None;
      for each_signal in self.signal_list:
        if ( each_signal.name == "dword[%d]" % i ):
          my_signal = each_signal;
      if ( my_signal != None ):
        my_signal.values = [];
        my_signal.format = "hex";
        my_signal.bits_total = 32;
        my_signal.bit_top   = 31;
        my_signal.bit_bot   = 0;

  # Note: This doesn't work the best, so disabling
  if ( False ):
    print("Culling excess RLE sample pre and post trigger");
    # Cull samples to max pre and post trig lengths to keep display usable
    rle_pre_trig_len  = int( self.vars["sump_rle_pre_trig_len" ],16);
    rle_post_trig_len = int( self.vars["sump_rle_post_trig_len" ],16);
    total_samples = len(pre_pad ) + ram_phys + len( post_pad );
    pre_trig = trig_i;
    post_trig = total_samples - trig_i;
    start_ptr = 0;
    stop_ptr  = -1;
    if ( pre_trig > rle_pre_trig_len ):
      start_ptr = trig_i - rle_pre_trig_len;
    if ( post_trig > rle_post_trig_len ):
      stop_ptr = trig_i + rle_post_trig_len;
    for i in range( 0, events, 1 ):
      my_signal = None;
      for each_signal in self.signal_list:
        if ( each_signal.name == "event[%d]" % i ):
          my_signal = each_signal;
      if ( my_signal != None ):
        my_signal.values = my_signal.values[start_ptr:stop_ptr];
    for i in range( 0, ram_dwords , 1 ):
      my_signal = None;
      for each_signal in self.signal_list:
        if ( each_signal.name == "dword[%d]" % i ):
          my_signal = each_signal;
      if ( my_signal != None ):
        my_signal.values = my_signal.values[start_ptr:stop_ptr];

  # Experimental idle gap removal
  if ( False ):
    all_data = [];
    new_data = [];
    for i in range( len(self.signal_list[0].values )):
      data_at_t = [];
      for each_signal in self.signal_list:
        if ( each_signal.type != "bundle" and len(each_signal.values) > 0):
          data_at_t +=[ each_signal.values[i] ];
      all_data += [ data_at_t ];
    k = 0; j = None;
    prev_data = [];
    for i in range( len(all_data) ):
      now_data = all_data[i];
      if ( now_data == prev_data ):
        if ( k == 0 ):
          j = i;
        k+=1;# Count the repeats
      else:
        if ( k == 0 ):
          new_data += [ now_data ];
        elif ( k < 1024 ):
          new_data += [ all_data[i-k:i] ];
        else:
#         new_data += [ now_data ];# Remove the entire idle run
#         new_data += [ now_data ];# Remove the entire idle run
          new_data += [ all_data[i-64:i] ];
      prev_data = now_data;

    k = 0;
    for (i,each_signal) in enumerate( self.signal_list ):
      if ( each_signal.type != "bundle" and len(each_signal.values) > 0):
        each_signal.values = [];
        for each_data in new_data:
          each_signal.values += [ each_data[k] ];
        k+=1;

 
  sump_bundle_data( self );
  recalc_max_samples( self );
  stop_time = time.time();
  total_time = stop_time - start_time;
  print("RLE Decompress Time = %.1f" % total_time );
  return trig_i;

def rle_undersample_signal( self, undersample_rate, my_signal ):
  print("rle_undersample_signal()");
  val = "0";
  new_values = [];
  i = 0;
  for each in my_signal.values:
    if ( each == "1" ):
      val = "1";
    i +=1;
    if ( i == undersample_rate ):
      i = 0;
      new_values += [val];
      val = "0";
  my_signal.values = new_values[:];
  return;


# Given a RLE compressed list, expand to regular time sample list.
# Return the list and the index location of the trigger
def expand_rle( self, start_t,stop_t,pre_trig,post_trig ):
  i = start_t;
  j = 0;
  rle_list = pre_trig + post_trig;
  trigger_index = 0;
# ( trigger_time, trigger_data ) = pre_trig[-2];
# print("RLE TRIGGER Compressed-2 %08x " % ( trigger_data ) );
  ( trigger_time, trigger_data ) = pre_trig[-1];
  print("RLE TRIGGER Compressed   %08x " % ( trigger_data ) );
  sample_list = [];
  ( rle_time, rle_data ) = rle_list[j];
  hold_data = rle_data; 
  sample_list += [ hold_data ];# Add old sample
  old_rle_time = 0;
  j +=1;
  ( rle_time, rle_data ) = rle_list[j];
  while ( i <= stop_t and j < (len(rle_list)-1) ):
    if ( i < rle_time ):
      sample_list += [ hold_data ];# Add old sample
    else:
      sample_list += [ rle_data ];# Add the new sample
      hold_data    = rle_data; 
      j +=1;
      old_rle_time = rle_time;
      ( rle_time, rle_data ) = rle_list[j];
#     if ( rle_time == trigger_time ):
#       trigger_index = len( sample_list )-1;
#     if ( old_rle_time == trigger_time ):
#       trigger_index = len( sample_list );
    if ( i == trigger_time ):
      trigger_index = len( sample_list )-1;
#     print("RLE TRIGGER Decompressed %08x " % ( sample_list[trigger_index] ) );
    i+=1;
  print("RLE TRIGGER Decompressed %08x " % ( sample_list[trigger_index] ) );
  return ( sample_list, trigger_index );

# Example RLE List
# 000007ff 0000000d
# 00000800 0000000d 2nd to last of pre-trig
# 1d4c4ad3 0000000b Last item of pre-trig
# 1d4c4ad4 0000000b 1st item of post-trig
def process_rle( self, rle_list ):
  ln = len( rle_list ) // 2;# Size of pre and post lists
  pre_list  = list(rle_list[0:ln]);
  post_list = list(rle_list[ln:]);
  culls = [];
# tuplelist2file( self, "rle_prelist1.txt", pre_list );

  # Figure out oldest RLE sample pre-trigger and then rotate list
  start_time = 0xFFFFFFFF;
  i = 0;
  for ( rle_time, rle_data ) in pre_list:
    # print("%08x %08x" % ( rle_time, start_time) );
    if ( rle_time < start_time ):
      start_time = rle_time;
      n = i;# Location of oldest RLE sample found thus far
    i +=1;
  pre_list = rotate_list(self,pre_list, n );
  print("RLE pre_list %08x %08x" % ( pre_list[-1] ) );
# tuplelist2file( self, "rle_prelist2.txt", pre_list );

  # ini defines hard limits of how many uncompressed samples pre and post trig
  rle_pre_trig_len  = int(self.vars["sump_rle_pre_trig_len" ],16);
  rle_post_trig_len = int(self.vars["sump_rle_post_trig_len" ],16);
  rle_autocull      = int(self.vars["sump_rle_autocull" ],10 );
  rle_gap_remove    = int(self.vars["sump_rle_gap_remove" ],10 );
  rle_gap_replace   = int(self.vars["sump_rle_gap_replace" ],10 );

  # Now scale limits based on the sump_acqusition_len setting 25,50,75,100
  acq_len   = int(self.vars["sump_acquisition_len"],16);
  pre_trig  = (acq_len & 0xF0)>>4;# Expect 1-4 for 25%-100% of 1st RAM Half
  post_trig = (acq_len & 0x0F)>>0;# Expect 1-4 for 25%-100% of 2nd RAM Half

  rle_pre_trig_len =  ( rle_pre_trig_len  // 4 ) * pre_trig; # Div-4, Mult 1-4
  rle_post_trig_len = ( rle_post_trig_len // 4 ) * post_trig;# Div-4, Mult 1-4


  # Cull any non-events pre and post trigger. Non-events are when the HW
  # generates a simple as a MSB timer bit has rolled over. This feature
  # prevents hardware from hanging forever if there are no events.
  if ( False ):
    pre_list_old = pre_list[:];
    (first_time,first_data ) = pre_list[0];
    pre_list = [];
    valid = False;
    prev_time = None;
    for ( rle_time, rle_data ) in list(pre_list_old):
      if ( rle_data != first_data and valid == False ):
        valid = True;
        if ( prev_time != None ):
          # If space between 1st and 2nd RLE samples is large, cull down to 1000
          if ( ( rle_time - prev_time ) < 1000 ): 
            pre_list += [ (prev_time,prev_data) ];# Keep sample before 1st delta
          else:
            pre_list += [ ((rle_time-1000),prev_data)];# sample before 1st delta
      if ( valid == True ):
        pre_list += [ (rle_time,rle_data) ];
      else:
        prev_time = rle_time;
        prev_data = rle_data;
    if ( len( pre_list ) == 0 ):
      pre_list = [ pre_list_old[-1] ];


  # Experimental idle gap removal. This determines if there is NULL activity
  # at the beginning of time and will shorted the rle_pre_trig_len
  # 2017.09.22
  if ( rle_autocull == 1 ):
    ( rle_start_time, rle_start_data ) = pre_list[0];# 1st RLE Sample
    for (i,( rle_time, rle_data )) in enumerate(list(pre_list[:])):
      if ( rle_data != rle_start_data ):
        print( len( pre_list ) );
        pre_list = pre_list[i-1:];
        print("Auto-Culling pre-Trigger at RLE sample %d" % (i-1) );
        print( len( pre_list ) );
        break;

    ( rle_stop_time, rle_stop_data ) = post_list[-1];# Last RLE Sample
    for (i,(rle_time,rle_data)) in enumerate(reversed(post_list[:])):
      if ( rle_data != rle_stop_data ):
        print("Auto-Culling %d post-Trigger RLE samples" % (i) );
        print( len( post_list ) );
        if ( i > 1 ):
          post_list = post_list[0:-(i-1)];
        print( len( post_list ) );
        break;
    if ( (i+1) == len(post_list) ):
      post_list = [ post_list[0] ];# Only keep the 1st

# rle_gap_remove = 128;
  if ( rle_gap_remove != 0 and self.rle_lossy == True ):
    if ( rle_gap_replace > rle_gap_remove ):
      rle_gap_replace = rle_gap_remove;
    pre_list_old = pre_list[:];
    pre_list     = [ pre_list_old[0] ];
    ( rle_start_time, rle_start_data ) = pre_list_old[0];# 1st RLE Sample
    total_time_removed = 0;
    prev_rle_time = rle_start_time;
    for ( rle_time, rle_data ) in list(pre_list_old[1:]):
      time_delta = rle_time - prev_rle_time;
      if ( time_delta > rle_gap_remove ):
        time_removed = time_delta - rle_gap_replace;
        total_time_removed += time_removed;
      prev_rle_time = rle_time;
      pre_list += [ (rle_time-total_time_removed, rle_data ) ];

    post_list_old = post_list[:];
    post_list = [];
    for ( rle_time, rle_data ) in list(post_list_old):
      time_delta = rle_time - prev_rle_time;
      if ( time_delta > rle_gap_remove ):
        time_removed = time_delta - rle_gap_replace;
        total_time_removed += time_removed;
      post_list += [ (rle_time-total_time_removed,rle_data) ];
      prev_rle_time = rle_time;

  # Cull any samples outside the sump_rle_pre_trig_len
  (trig_time,trig_data ) = pre_list[-1];
  pre_list_old = pre_list[:];
  pre_list = [];
  for ( rle_time, rle_data ) in list(pre_list_old):
    if ( rle_time > ( trig_time - rle_pre_trig_len ) ):
      pre_list += [ (rle_time,rle_data) ];
      culls+=[("+ %08x %08x %08x %08x" % 
                 (rle_time,trig_time,rle_pre_trig_len,rle_data))];
    else:
      culls+=[("< %08x %08x %08x %08x" % 
                 (rle_time,trig_time,rle_pre_trig_len,rle_data))];
  if ( len( pre_list ) == 0 ):
    pre_list = [ pre_list_old[-1] ];

  stop_time = 0x00000000;
  i = 0;
  for ( rle_time, rle_data ) in post_list:
    if ( rle_time > stop_time ):
      stop_time = rle_time;
      n = i;# Location of newest RLE sample found thus far
    i +=1;

  # Cull any samples outside the sump_rle_post_trig_len
  post_list_old = post_list[:];
  post_list = [];
  for ( rle_time, rle_data ) in list(post_list_old):
    if ( rle_time < ( trig_time + rle_post_trig_len ) ):
      post_list += [ (rle_time,rle_data) ];
      culls+=[("+ %08x %08x %08x %08x" % 
               (rle_time,trig_time,rle_post_trig_len,rle_data))];
    else:
      culls+=[("> %08x %08x %08x %08x" % 
               (rle_time,trig_time,rle_post_trig_len,rle_data))];
  if ( len( post_list ) == 0 ):
    post_list = [ post_list_old[0] ];

  (start_time,start_data ) = pre_list[0]; 
  (stop_time,stop_data   ) = post_list[-1];
  list2file( self, "sump2_rle_cull_list.txt", culls );
  print( "--------------" );
  print( len( pre_list ) );
  print( len( post_list ) );
  return ( start_time , stop_time, pre_list, post_list );

def rotate_list( self, my_list, n ):
  return ( my_list[n:] + my_list[:n] );

########################################################
# Calculate desired ram length pre,post trig to work with 
def sump_ram_len_calc( self ):
  ram_len    = self.sump.cfg_dict['ram_len'];# Physical RAM Size, ie 1K
  acq_len   = int(self.vars["sump_acquisition_len"],16);
  pre_trig  = (acq_len & 0xF0)>>4;# Expect 1-4 for 25%-100% of 1st RAM Half
  post_trig = (acq_len & 0x0F)>>0;# Expect 1-4 for 25%-100% of 2nd RAM Half

  ram_len_half = ram_len // 2;
  qtr = ram_len_half // 4;# Example 128 of 1K/2

  ram_pre  = qtr * pre_trig; # 25,50,75 or 100% num samples pre-trig
  ram_post = qtr * post_trig;# 25,50,75 or 100% num samples post-trig

  return [ ram_pre, ram_post, ( ram_pre+ram_post ), ram_len ];



########################################################
# Return a list of acquired SUMP capture data using variable length
def sump_dump_var_ram( self, rd_page = 0 ):
  ( ram_pre, ram_post, ram_len, ram_phys ) = sump_ram_len_calc(self);
  trig_ptr = self.sump.rd( self.sump.cmd_rd_trigger_ptr )[0];
  ram_ptr  = 0xFFFF & (trig_ptr - ram_pre - 1);
  self.sump.wr( self.sump.cmd_wr_ram_page, rd_page );
  self.sump.wr( self.sump.cmd_wr_ram_ptr , ram_ptr );# Load at specfd pre-trig
  data = self.sump.rd( self.sump.cmd_rd_ram_data, num_dwords = ram_len );
  name = "%02x" % rd_page;
# hexlist2file( self, "sump_dump_var_ram_" + name + ".txt", data );
  return data;


########################################################
# Return a complete list of acquired SUMP capture data      
def sump_dump_ram( self, rd_page = 0, rd_ptr = None ):
  ram_len    = self.sump.cfg_dict['ram_len'];
  self.sump.wr( self.sump.cmd_wr_ram_page, rd_page );
  if ( rd_ptr != None ):
    self.sump.wr( self.sump.cmd_wr_ram_ptr , rd_ptr );# 
  data = self.sump.rd( self.sump.cmd_rd_ram_data, num_dwords = ram_len );
# hexlist2file( self, "sump_dump_ram.txt", data );
  return data;


########################################################
# Use the wave_list to generate a new signal_list   
#def wave2signal_list( self ):
# ram_len    = self.sump.cfg_dict['ram_len'];
# ram_dwords = self.sump.cfg_dict['ram_dwords'];
# ram_bytes  = self.sump.cfg_dict['ram_event_bytes'];
# ram_rle    = self.sump.cfg_dict['ram_rle'];
#
# events = ram_bytes * 8;
# # Iterate the number of event bits and init with 0s
# for i in range( 0, events , 1):
#   sig_name = "event_%d" % i;
#   self.signal_list.append( signal(name=sig_name) );
#   self.signal_list[i].format = "bin";
#   self.signal_list[i].bits_total = 1;
#   self.signal_list[i].bit_top   = 0;
#   self.signal_list[i].bit_bot   = 0;
#   for j in range( 0, ram_len, 1):
#     self.signal_list[i].values.append( "0" );
#
# # Iterate the number of dwords and init with 0x0s
# for i in range( 0, ram_dwords, 1):
#   sig_name = "dword_%d" % i;
#   self.signal_list.append( signal(name=sig_name) );
#   self.signal_list[events+i].format = "hex";
#   self.signal_list[events+i].bits_total = 32;
#   self.signal_list[events+i].bit_top   = 31;
#   self.signal_list[events+i].bit_bot   = 0;
#   for j in range( 0, ram_len, 1):
#     self.signal_list[events+i].values.append( "%08x" % 0 );
#
# return;


########################################################
# Read values of sump vars and use to update signal objects trigger attrib
def sump_vars_to_signal_attribs( self ):
# try:
  if ( True ):
    trig_type     =      self.vars["sump_trigger_type"  ];#     "or_rising";
    trig_field    = int( self.vars["sump_trigger_field" ],16 );
    trig_delay    = int( self.vars["sump_trigger_delay" ],16 );
    trig_nth      = int( self.vars["sump_trigger_nth"   ],16 );
    rle_event_en  = int( self.vars["sump_rle_event_en"  ],16 );
    data_en       = int( self.vars["sump_data_enable"   ],16 );
    user_ctrl     = int( self.vars["sump_user_ctrl"     ],16 );
    wd_time       = int( self.vars["sump_watchdog_time" ],16 );
    user_pattern0 = int( self.vars["sump_user_pattern0" ],16 );
    user_pattern1 = int( self.vars["sump_user_pattern1" ],16 );

#   self.trigger       = 0;# 0=OFF +1=Rising,-1=Falling,2=Pattern0,3=Pattern1
#   self.data_enable   = False;

    # Note: If trigger_type is pattern_ris or pattern_fal then
    # user_pattern0 is the mask of what bits to pattern match on
    # user_pattern1 is the actual pattern bits 

    # rle_event_en controls the Hidden field
    for i in range( 0, 32 , 1):
      sig_obj = get_sig_obj_by_name( self, ("event[%d]" % i ) );
      if ( sig_obj != None ):
        sig_obj.hidden      = False;
        if ( ( rle_event_en & 1<<i ) != 0x0 ):
          sig_obj.hidden = False;
        else:
          sig_obj.hidden = True;

    # Clear everything to start with. Set any data_en bits
    for i in range( 0, 32 , 1):
      sig_obj = get_sig_obj_by_name( self, ("event[%d]" % i ) );
      if ( sig_obj != None ):
        sig_obj.trigger     = 0;    # OFF
        if ( ( data_en & 1<<i ) != 0x0 ):
          sig_obj.data_enable = True;
        else:
          sig_obj.data_enable = False;
    
    # Set any Rising or Falling edge trigger selection ( 1 only )
    if ( trig_type == "or_rising" or trig_type == "or_falling" or
         trig_type == "and_rising" or
         trig_type == "watchdog" ):
      if ( trig_type == "or_rising" ):
        trig = +1;
      if ( trig_type == "or_falling" ):
        trig = -1;
      if ( trig_type == "watchdog" ):
        trig = -2;
      if ( trig_type == "and_rising" ):
        trig = +4;
      for i in range( 0, 32 , 1):
        sig_obj = get_sig_obj_by_name( self, ("event[%d]" % i ) );
        if ( ( 1<<i & trig_field ) != 0x0 ):
          sig_obj.trigger = trig;

    if ( trig_type == "pattern_rising" or trig_type == "pattern_falling" ):
      for i in range( 0, 32 , 1):
        sig_obj = get_sig_obj_by_name( self, ("event[%d]" % i ) );
        if ( ( 1<<i & user_pattern0 ) != 0x0 ):
          if ( ( 1<<i & user_pattern1 ) != 0x0 ):
            sig_obj.trigger = 3;# Pattern of 1 for this bit 
          else:
            sig_obj.trigger = 2;# Pattern of 0 for this bit 
# except:
#   print("ERROR: Invalid sump variable assignments");
  return;

########################################################
# Read signal attributes and convert to sump variables
def sump_signals_to_vars( self ):
  if ( True ):
    rle_event_en = int( self.vars["sump_rle_event_en"],16 );
    for sig_obj in self.signal_list:
      for i in range( 0, 32 , 1):
        if ( sig_obj.name == ( "event[%d]" % i ) ):
          if ( sig_obj.hidden == False and sig_obj.visible == True ):
            rle_event_en = rle_event_en | ( 1<<i );# Set bit
          if ( sig_obj.hidden == True or sig_obj.visible == False ):
            rle_event_en = rle_event_en & ~( 1<<i );# Clear bit
    self.vars["sump_rle_event_en" ] = ("%08x" % rle_event_en );
  return;

########################################################
# Use sump2 hardware info to generate a signal_list 
def sump2signal_list( self ):
  ram_len    = self.sump.cfg_dict['ram_len'];
  ram_dwords = self.sump.cfg_dict['ram_dwords'];
  ram_bytes  = self.sump.cfg_dict['ram_event_bytes'];
  ram_rle    = self.sump.cfg_dict['ram_rle'];

  events = ram_bytes * 8;
  # Iterate the number of event bits and init with 0s
  for i in range( 0, events , 1):
#   sig_name = "event_%d" % i;
    sig_name = "event[%d]" % i;
    self.signal_list.append( signal(name=sig_name) );
    self.signal_list[i].format = "bin";
    self.signal_list[i].bits_total = 1;
    self.signal_list[i].bit_top   = 0;
    self.signal_list[i].bit_bot   = 0;
    for j in range( 0, ram_len, 1):
      self.signal_list[i].values.append( "0" );

  # Iterate the number of dwords and init with 0x0s
  for i in range( 0, ram_dwords, 1):
    sig_name = "dword_%d" % i;
    self.signal_list.append( signal(name=sig_name) );
    self.signal_list[events+i].format = "hex";
    self.signal_list[events+i].bits_total = 32;
    self.signal_list[events+i].bit_top   = 31;
    self.signal_list[events+i].bit_bot   = 0;
    for j in range( 0, ram_len, 1):
      self.signal_list[events+i].values.append( "%08x" % 0 );

  return;


########################################################
# Given a TXT file, make signal_list from it
# Format is:
# # foo bar addr
# 0 1 2
# 1 0 a
def txtfile2signal_list( self, file_name ):
  # Read in the flat text VCD translation and make lists of net names
  file_in    = open ( file_name , 'r' );
  file_list  = file_in.readlines();
  file_in.close();
  net_names  = file_list[0];
  sig_values = file_list[1:];
  self.sig_name_list   = " ".join(net_names.split()).split(' ');
  self.sig_name_list   = self.sig_name_list[1:]; # Remove Leading #
  self.sig_value_list  = file_list[1:];

  for each in self.sig_name_list[:]:
    self.signal_list.append( signal(name=each) );
  # Rip thru the value list ( of all sigs ) and extract one signal at a time
  # 0 000000000 1 000000000 0 000000000 0 000000000 000000000 000000000
  for ( i , sig_obj ) in enumerate( self.signal_list ):
    self.signal_list[i].format = "bin"; # Assume Binary by default
    self.signal_list[i].bits_total = 1;
    for each in self.sig_value_list:
      words = " ".join(each.split()).split(' ') + [None] * 20;
      sig_obj.values.append( words[i] );
      # If value other than 0 or 1 is found, declare this as hex
      if ( words[i] != "0" and words[i] != "1" and words[i] != None ):
        self.signal_list[i].format = "hex";
        self.signal_list[i].bits_total = len( words[i] ) * 4;
        self.signal_list[i].bit_top   = self.signal_list[i].bits_total-1;
        self.signal_list[i].bit_bot   = 0;
  return;

########################################################
# Given a VCD file, make signal_list from it
def vcdfile2signal_list( self, file_name ):
  try: # Read the Input File and Separate the Header from Data
    file_in = open( file_name , "r" );
    file_lines = file_in.readlines();
    file_in.close();
  except:
    print( "ERROR Input File: "+file_name );
    print( "Possibly a Python MemoryError due to large file size");

  self.signal_list = [];
  self.rip_list = [];
  self.rip_symbs = [];
  self.top_module = "";
  hier_list = [];
  hier_name = "";
  hier_level = 0;# +1 on 1st will be 0;

  print( "vcdfile2signal_list() : Parsing VCD Symbol Definitions");
  start_time = self.pygame.time.get_ticks();
  for ( i , each )  in enumerate ( file_lines ):
    words = each.strip().split() + [None] * 4; # Avoid IndexError
    if ( words[0] == "$enddefinitions" ):
      dump_vars_index = i; # Remember location to start Value Change Parsing
      break; # Save time and don't process entire file

    #####################################
    # Check for Signal Symbol Definitions
    # $var wire 1 * tx_data [15] $end
    #  0     1  2 3   4       5    6
    if ( words[0] == "$var" ):
      type =      words[1];   # ie "wire"
      bits = int( words[2] ); # ie 32
      symb =      words[3];   # ie ","
      name =      words[4];   # ie "lb_addr"
      rip  =      words[5];   # ie "[31:0]"  or "$end" if single bit
      sig_obj = signal( name=name, vcd_symbol=symb );
      sig_obj.hier_name  = hier_name;
      sig_obj.hier_level = hier_level;
      sig_obj.bits_total = bits;   # ie 32
      sig_obj.bit_top    = bits-1; # ie 31
      sig_obj.bit_bot    = 0;      # ie 0
      if ( rip != "$end" ):
        sig_obj.rip = rip;# [15:0] or [1] or ""
      if ( bits > 1 or sig_obj.rip != "" ):
        sig_obj.format = "hex";
      else:
        sig_obj.format = "bin";

      # If a portion of a ripped bus and not [0], add to special rip_list
      # otherwise, add to the regular signal_list
      if ( 
           ( sig_obj.rip != ""       ) and \
           ( ":" not in sig_obj.rip  ) and \
           ( sig_obj.rip != "[0]"    )     \
         ):
        self.rip_list.append( sig_obj );
        self.rip_symbs.append( symb );
      else:
        self.signal_list.append( sig_obj );

      # Now also add "[0]" to rip_list ( It will appear in BOTH lists )
      if ( sig_obj.rip == "[0]" ):
        self.rip_list.append( sig_obj );
        self.rip_symbs.append( symb );
    
    #####################################
    # Check for new hierarchy declaration
    if ( words[0] == "$scope" and    \
         ( words[1] == "module" or   \
           words[1] == "begin"     ) \
       ):
      if ( self.top_module == "" ):
        self.top_module = words[2]; # ie "tb_dut"
        print( "top_module is ", self.top_module);
      name = words[2];   # ie "u_dut"
      sig_obj = signal( name=name );
      sig_obj.hier_name  = hier_name;
      sig_obj.hier_level = hier_level;
      sig_obj.bits_total = 0;
      sig_obj.bit_top    = 0;
      sig_obj.bit_bot    = 0;
      sig_obj.format     = "";
      self.signal_list.append( sig_obj );
      sig_obj.collapsable = True;
      sig_obj.expandable  = False;
      hier_list.append( words[2] );
      rts = ""
      for each in hier_list:
        rts = rts + "/" + each;
      hier_name = rts;

    #####################################
    # Adjust hier level on $scope or $upscope
    if ( words[0] == "$scope" ):
      hier_level += 1;
    if ( words[0] == "$upscope" ):
      hier_level -= 1;
      
    if ( words[0] == "$scope" and words[1] == "begin" ):
      hier_list.append( "process" );
    if ( words[0] == "$upscope" ):
      hier_list.pop(); # Remove last item from list

  # Create a hash lookup of symbol to object index and bits to speed things up
  hash_dict_index = {};
  hash_dict_bits  = {};
  for ( i, sig_obj ) in enumerate( self.signal_list ):
    # Need to make a list for symb lookup as clocks can reuse same symbol
    if ( hash_dict_index.get( sig_obj.vcd_symbol, None ) == None ):
      hash_dict_index[ sig_obj.vcd_symbol ] = [i];
    else:
      hash_dict_index[ sig_obj.vcd_symbol ].append( i );
    hash_dict_bits[ sig_obj.vcd_symbol ]  = sig_obj.bits_total;

  # Go thru the rip_list and determine the number of bits for the busses
  # This finds the parent in self.signal_list that matches the current
  # rip from self.rip_list and adjusts the parents bits_total and bit_top
  # if the rip's exceed the parent's old value. The parent will start with
  # (1,1) since it is based on rip [0]
  # Also create a hash to lookup the parent index for each rip symbol
  hash_rip_list = {};
  hash_rip_parent = {};
  for (j,my_each) in enumerate( self.rip_list ):
    name      = my_each.name;
    hier_name = my_each.hier_name;
    rip       = my_each.rip;
    hash_rip_list[ my_each.vcd_symbol ] = j; # For Fast Lookup later
    # Calculate the weight of each bit, ie [7] is 128
    for foo_each in [ "[", "]" ]:
      rip = rip.replace( foo_each , " "+foo_each+" " );
    words = rip.strip().split() + [None] * 10; # Avoid IndexError
    rip = int( words[1], 10 );
    my_each.bit_weight = 2**rip;# Conv 7->128

    if ( name != None and hier_name != None ):
      for ( i, my2_each ) in enumerate( self.signal_list ):
        if ( name      == my2_each.name      and \
             hier_name == my2_each.hier_name       ):
          hash_rip_parent[ my_each.vcd_symbol ] = i; # For Fast Lookup later
          if ( rip > my2_each.bit_top ):
            my2_each.bits_total = rip+1;
            my2_each.bit_top    = rip;

  symb_parse_list = ["!","#","$","&","'","K" ];

  # Now Parse actual VCD section and try and figure out sample clock period
  # by finding the smallest time delta across the entire VCD file
  sample_period = 99999999;
  prev_time     = 0;
  for ( i , each )  in enumerate ( file_lines[dump_vars_index:] ):
    words = each.strip().split() + [None] * 4; # Avoid IndexError
    if ( words[0][0:1] == "#" ):
      now_time = int( words[0][1:],10 );
      delta_time = now_time - prev_time;
      if ( delta_time < sample_period and delta_time != 0):
        sample_period = delta_time;
        print( sample_period );
      prev_time = now_time;


  # Now Parse the actual VCD section and calculate current values for each
  # signal at every time stamp section.
  print( "vcdfile2signal_list() : Parsing VCD Value Change Dumps");
  start_time = self.pygame.time.get_ticks();
  percent = 0;
  percent_total = ((1.0)*len( file_lines[dump_vars_index:] ) );
  sample_cnt = 0;
  for ( i , each )  in enumerate ( file_lines[dump_vars_index:] ):
    # This takes a while, so calculate and print percentage as it goes by
    if ( ((i*1.0) / percent_total) > percent ):
      perc_str = ( str( int(100*percent) ) + "%");
      draw_header( self, perc_str );
      print( perc_str );
      percent += .05;

    # Handle binary cases for "1>" and convert to "1 >"
    # If the 1st char is 0 or 1 insert a space to make look like vector
    if ( each[0:1] == "0" or
         each[0:1] == "1" or
         each[0:1] == "x" or
         each[0:1] == "z"      ):
        each = each[0:1] + " " + each[1:];
    words = each.strip().split() + [None] * 4; # Avoid IndexError
    symb  = words[1];

    # Skip the initial dumpvars section as nothing to dump yet
    if ( words[0]      == "#0" ):
      None;
      time_stamp = 0;
      time_now   = 0;

    # When we reach a timestamp, append all last_value to values list
    elif ( words[0][0:1] == "#" ):
      time_stamp = int( words[0][1:], 10 );
      while ( time_now <= time_stamp ):
        for sig_obj in self.signal_list:
          # VV switch from "1" "0" ASCII to boolean for smaller DRAM use
          if ( sig_obj.last_value == "1" ):
            value = True;
          elif ( sig_obj.last_value == "0" ):
            value = False;
          else:
            value = sig_obj.last_value;
#         sig_obj.values.append( sig_obj.last_value );
          sig_obj.values.append( value );
          sample_cnt += 1;# Count Total Samples for final report at end
        time_now += sample_period;

   # Read the symbols new value and assign to last_value
    else:
      if ( words[0][0:1]=="0" or
           words[0][0:1]=="1" or
           words[0][0:1]=="x" or
           words[0][0:1]=="z" ):
        value = words[0];
      elif ( words[0][0:1] == "b" ):
        try:
          value = int( words[0][1:],2 );# Convert Binary String to Integer
          if ( symb != None ):
            num_bits = hash_dict_bits[ symb ];
            num_nibs = int(num_bits/4.00 + 0.75 );# ie 29 bits gets 8 nibbles
          else:
            num_nibs = 1;
        except:
          value = 0; 
          num_nibs = 1;
        value = "%08x" % value;# Now Convert Integer to Hex
        value = value[::-1]; # Reverse
        value = value[0:num_nibs]; # Keep desired number of LSBs
        value = value[::-1]; # Reverse Back
      elif ( words[0][0:1] == "$" ):
        value = None;
      else:
        line_num = i + dump_vars_index + 1;
        print( "ERROR line " + str(line_num) + " : " + words[0]);
        value = None;
    
      
      # Is symb in rip_list?  If not, do normal processing
      if ( symb not in self.rip_symbs ):
        if ( value != None and symb != None ):
          # Note: a symb might be used multiple times for clock ports, etc.
          try:
            for i in hash_dict_index[ symb ]:
              self.signal_list[i].last_value = value;
          except:
#           print "VCD Symbol Error " + symb;
            None;
      # Oh SNAP - This is in the rip_list. Find obj for [0] ( Parent )
      # and if 0, AND out bit_weight, if 1 OR in bit_weight.
      # This op takes time since values are stored in ASCII, must convert to 
      # int, perform the bit operation and then convert back to ASCII.
      else:
        my_each  = self.rip_list[ hash_rip_list[ symb ] ];
        my2_each = self.signal_list[ hash_rip_parent[ symb ] ];
        try:
          last_value = int( my2_each.last_value, 16 );
        except:
          last_value = 0;
        if   ( value == "0" ):
          last_value = last_value & ~ my_each.bit_weight;
        elif ( value == "1" ):
          last_value = last_value |   my_each.bit_weight;
        nibs = my2_each.bits_total//4;# ie 32 = 8, num nibs to display
        new_value = "%016x" % last_value;# 16 Nibbles, remove leading next
        my2_each.last_value = new_value[16-nibs:];# Remove leading 0s

  stop_time = self.pygame.time.get_ticks();

  tt = str( (stop_time - start_time) / 1000 ) + "s"; 
  rate = str( sample_cnt / ((stop_time - start_time) * 1000 )) + " MSPS"; 
  print( "vcdfile2signal_list() : Complete : Time " + tt +" : Rate " + rate);
  draw_header( self, "" );
  return;


def load_fpga( self ):
  log( self, ["load_fpga()"]);
  draw_header( self, "load_fpga()" );
  os.system("sudo ./icoprog -f < top_bitmap.bin");# Program IcoBoard Flash
  os.system("sudo ./icoprog -b");
  draw_header( self, "Please reboot and restart SUMP2.py application" );


def shutdown( self, system_shutdown = False, system_reboot = False ):
  log( self, ["shutdown()"]);
  var_dump( self, "sump2.ini" ); # Dump all variable to INI file
  proc_cmd( self, "save_format", [""] ); # Autosave the last format
  if ( system_shutdown == True and system_reboot == False ):
    draw_header( self, "Linux Shutdown initiated" );
    os.system("sudo shutdown -h now");# Pi Shutdown ( Halt )
  if ( system_shutdown == True and system_reboot == True  ):
    draw_header( self, "Linux Reboot initiated" );
    os.system("sudo shutdown -r now");# Pi Reboot

  if ( self.mode_cli == False ):
    self.pygame.quit();# Be IDLE friendly
  print("");
  print("Thank you for using SUMP2 " + self.vers + " by BlackMesaLabs");
  print("Please encourage the development and use of open-source software");

  sys.exit();
  return;


#def init_vars( self ):
#  self.var_hash= {};
#  self.var_hash["bd_connection"      ] = "tcp";
#  self.var_hash["bd_protocol"        ] = "poke";
#  self.var_hash["tcp_port"           ] = "21567";
#  self.var_hash["tcp_ip_addr"        ] = "127.0.0.1";# No Place Like Home
#  self.var_hash["sump_addr"          ] = "00000000" ;# Addr of sump2_ctrl_reg
#  self.var_hash["sump_trigger_type"  ] = "or_rising";
#  self.var_hash["sump_trigger_field" ] = "00000000";
#  self.var_hash["sump_trigger_delay" ] = "0000";
#  self.var_hash["sump_trigger_nth"   ] = "0000";
#  self.var_hash["sump_user_ctrl"     ] = "00000000";
#  self.var_hash["sump_user_pattern0" ] = "00000000";
#  self.var_hash["sump_user_pattern1" ] = "00000000";
#  self.var_hash["sump_data_enable"   ] = "00000000";
#  return;

def init_globals( self ):
  # Define the colors we will use in RGB format

  import platform,os;
  self.os_sys = platform.system();  # Windows vs Linux
  self.fatal_msg = None;

  self.undersample_data = False;
  self.undersample_rate = 1;
  self.gui_active = False;
  self.color_bg = (0,0,0);
  self.color_fg = (0,0,0);
  self.prompt   = "bd>";
  self.done = False; # This breaks the application loop when true
# self.clock = self.pygame.time.Clock();
# self.lcd = self.pygame.display.Info(); # Dimensions of physical LCD screen 
  self.txt_height = 0;
  self.txt_width  = 0;
  self.spin_char  = "";
  self.debug = False;
  self.last_filesave = None;# Name of last file saved, used for Save_Rename
  self.vcd_import = False;
  self.display_hints = True;
  self.acq_state = "acquire_stop";
  self.rle_lossy = False;
  self.acq_mode  = "nonrle";
# if ( self.mode_cli == False ):
#   self.font = get_font( self,self.vars["font_name"],self.vars["font_size"]);
  self.sample_start = 0;
  self.sample_stop  = 0;
  self.sample_room  = 0;
  self.prev_sample_start = None;
  self.prev_sample_stop  = None;
  self.max_samples  = 0;
  self.zoom_x = self.txt_width; # Default zoom ratio is 1 text char width
  self.stop_zoom = False;
  self.sig_obj_sel = None;
  self.key_buffer = "";
  self.last_search_value = "";
  self.vertical_scrolled_offscreen = False;
  self.last_cmd = "";
# self.cmd_history = ["","",""];
  self.skipped_refresh_cnt = 0;
  self.old_list = [];
  self.slider_width = 0;
  self.cmd_history = [];
  self.dwords_start = 0;
  self.dwords_stop  = 0;
  self.signal_list_hw = None;# Stores HW config during VCD viewing

  self.sig_name_start_x = 0;
  self.sig_name_start_y = 0;
  self.sig_name_stop_x = 0;
  self.sig_name_stop_y = 0;
  self.sig_value_start_x = 0;
  self.sig_value_start_y = 0;
  self.sig_value_stop_x = 0;
  self.sig_value_stop_y = 0;
  self.cursor_start_y = 0;
  self.cursor_stop_y = 0;
  self.top_module = "";# ie "tb_foo"
  self.sig_top   = 0;
  self.sig_bot   = 0;
# self.scroll_togl = 1;# +1=Pan, -1=Zoom
  self.surface_start = -1;
  self.surface_stop  = -1;
  self.name_surface_valid = False;
  self.curval_surface_valid = False;

  self.cursor_list = [];
  self.cursor_list.append( cursor(name="Cursor1"));
  self.cursor_list.append( cursor(name="Cursor2"));
  self.cursor_list[0].y = 0;
  self.cursor_list[1].y = 0;
  self.cursor_list[0].sample = 10;
  self.cursor_list[1].sample = 15;

  self.mouse_x = 0;
  self.mouse_y = 0;
  self.mouse_button = 0;
  self.mouse_region = "";
  self.mouse_name_sel_y = -1;
  self.scroll_num_samples = 1;

  self.mouse_btn1dn_x = -1;
  self.mouse_btn1dn_y = -1;
  self.mouse_btn1up_x = -1;
  self.mouse_btn1up_y = -1;
  self.mouse_btn3dn_x = -1;
  self.mouse_btn3dn_y = -1;
  self.mouse_btn3up_x = -1;
  self.mouse_btn3up_y = -1;
  self.mouse_btn1up_time_last = 0;
  self.mouse_btn1up_time = 0;
  self.mouse_btn1dn_time = 0;
  self.resize_on_mouse_motion  = False;
  self.max_w = 0;
  self.max_w_chars = 0;
  
# self.subpop  = False;
  self.popup_x = None;
  self.popup_y = -1;
  self.popup_w =  0;
  self.popup_y2 = -1;
  self.popup_sel = "";
  self.popup_sample =  0;
  self.popup_parent_x    = None;
  self.popup_parent_y    = None;
  self.popup_parent_list = None;
  self.txt_entry = False;
  self.txt_entry_caption = "Rename_Signal";

  # Create a list of files to source in menu given include and exclude filters
  file_inc_filter = self.vars["sump_script_inc_filter"];
  file_exc_filter = self.vars["sump_script_exc_filter"];
  file_load_list = ["File_Load"];
  import glob;
  glob_list = set(glob.glob(file_inc_filter))-set(glob.glob(file_exc_filter));
  for each in glob_list:
    file_load_list += ["source "+each ];

  # Create a list of files to source in menu given include and exclude filters
  file_inc_filter = "*.vcd";
  vcd_load_list = ["VCD_Load"];
  import glob;
  glob_list = set(glob.glob(file_inc_filter));
  for each in glob_list:
    vcd_load_list += ["load_vcd "+each ];


  # Right-Click menu over signal names 
  self.popup_list_names = [
#   "--------","Group","Group+","Expand","Collapse","Insert_Divider",
#   "--------","Delete","Make_Invisible","Make_All_Visible",
#   "--------","Delete","Rename","Restore_All",
#              ["Clipboard","Cut","Paste","Delete","Rename"],
    "--------", "Rename",
                "Insert_Divider",
               ["Clipboard","Cut","Paste"],
               ["Visibility","Delete","Hide","Hide_All","Show","Show_All"],

#              ["Grouping","Group_with_Divider","Group_with_Parent", \
#               "UnGroup","Insert_Divider"],

               [ "Radix", "Hex","Signed","Unsigned" ],
#              [ "Waveform_Format", "Edit_Format","Save_Format","Load_Format",\
#                "Delete_Format", "Save_Selected" ], \

#   "--------",[ "Font_Size", "Font_Larger","Font_Smaller"],\
#   "--------","Trigger_Rising","Trigger_Falling","Trigger_Watchdog",\
    "--------","Trigger_Rising","Trigger_Falling","Trigger_AND",\
               "Trigger_Watchdog","Trigger_Remove","Trigger_Remove_All",\
#   "Trigger_Watchdog",\
#   "--------","Trigger_Rising","Trigger_Falling",\
#              "Trigger_Any_Rising","Trigger_Any_Falling","Trigger_Watchdog",\
    "--------","Set_Pattern_0","Set_Pattern_1","Clear_Pattern_Match",\
    "--------","Set_Data_Enable","Clear_Data_Enable",\
    "--------",["SUMP_Configuration","sump_trigger_delay",\
                                     "sump_trigger_nth",\
                                     "sump_user_ctrl",\
                                     "sump_user_pattern0",\
                                     "sump_user_pattern1",\
                                     "sump_rle_autocull",\
                                     "sump_rle_gap_remove",\
                                     "sump_rle_gap_replace",\
                                     "sump_watchdog_time"],\
    "--------",["Acquisition_Length",
                "[----T----]",
                "  [--T--]  ", 
                "   [-T-]   ", 
                "[----T-]   ",
                "   [-T----]",
               ],
    ];
#   "--------",["File_Load","File1","File2"],
#   "--------",file_load_list,
#              "BD_SHELL","Manual","Quit"];

  # Right-Click menu over waveform area 
  self.popup_list_values = [ ];
  self.popup_list_values += [ 
#   "--------","Debug_Vars",
#   "--------","Reload",
#   "Scroll_Toggle",

    "--------","Zoom_In", "Zoom_Out", "Zoom_Full","Zoom_Previous",
               "Zoom_to_Cursors",

    "--------",["Cursors",
               "Cursors_to_View","Cursor1_to_Here","Cursor2_to_Here",
               "Crop_to_Cursors","Crop_to_INI"],];

  if ( self.os_sys != "XXLinux" ):
    self.popup_list_values+=[["List_View",
                              "List_View_Cursors","List_View_Full"]];# Win


  self.popup_list_values+=[ \

#              ["Acquire",
#              "Acquire_Normal","Acquire_RLE","Acquire_Stop",],

               ["Acquire",
               "Acquire_Normal","Acquire_RLE","Acquire_RLE_Lossy",
               "--------",
               "Arm_Normal","Arm_RLE",
               "--------",
#              "Acquire_Download","Acquire_Stop"],
               "Download_Normal","Download_RLE","Download_RLE_Lossy",
               "--------",
               "Acquire_Stop"],

#              "Acquire_Single","Acquire_Continuous",
#              "Acquire_RLE_1x","Acquire_RLE_8x","Acquire_RLE_64x",

#   "--------","Crop_to_Cursors",
#   "--------","Cursors_to_View","Cursor1_to_Here","Cursor2_to_Here",\
#   "--------","Acquire_Single","Acquire_Continuous","Acquire_Stop",
#   "--------","Acquire_RLE_1x","Acquire_RLE_8x","Acquire_RLE_64x",
#   "--------","Acquire_Single","Acquire_Continuous","Acquire_Stop",
#   "--------","Acquire_RLE_1x","Acquire_RLE_8x","Acquire_RLE_64x",
#   "--------","Acquire_RLE_1x","Acquire_RLE_4x","Acquire_RLE_16x",
#              "Acquire_RLE_64x",
#   "--------",
#              ["File_Load","File1","File2"],
               ["File_Save","Save_PNG","Save_JPG","Save_BMP",
                "Save_TXT","Save_Rename"],
               file_load_list,
               ["VCD_Save","Save_VCD_Full","Save_VCD_Cursors",],
               vcd_load_list,

#              ["Fonts","Font_Larger","Font_Smaller"],
#              ["Misc","Font_Larger","Font_Smaller",
#              "BD_SHELL","Manual"],
               ];
#              "Quit"];
#              ["System","Quit","Shutdown","Reboot","Load_FPGA"]];# Pi
  if ( self.os_sys == "Linux" ):
    self.popup_list_values+=[["Misc","Font_Larger","Font_Smaller"]];
    self.popup_list_values+=[["System","Quit","Shutdown","Reboot","Load_FPGA"]];
  else:
    self.popup_list_values+=[["Misc","Font_Larger","Font_Smaller",
                              "BD_SHELL","Manual","Key_Macros"]];
#   self.popup_list_values+=[["System","Quit"]];# Win
    self.popup_list_values+=["Quit"];# Win


  self.popup_list = self.popup_list_values;
  self.cmd_alias_hash_dict = {};
  self.cmd_alias_hash_dict["zi"]    = "zoom_in";
  self.cmd_alias_hash_dict["zo"]    = "zoom_out";
  self.cmd_alias_hash_dict["zt"]    = "zoom_to";
  self.cmd_alias_hash_dict["q" ]    = "quit";
  self.cmd_alias_hash_dict["find"]  = "search";
  self.cmd_alias_hash_dict["/"]     = "search";
  self.cmd_alias_hash_dict["?"]     = "backsearch";
  return;


###############################################################################
class cursor(object):
  def __init__( self, name="Cursor1", visible=True, \
                bits_per_line=32, bits_total=32,format="hex"):
    self.name          = name;
    self.visible       = visible;
    self.selected      = False;
    self.x             = 0;
    self.y             = 0;
    self.sample        = 0;  
  def __del__(self):
#   print "You are killing me man";
    return;
  def __str__(self):
    return "name     = " + self.name            + "" +\
        "";


###############################################################################
# A signal contains time samples and various display attributes.
class signal(object):
  def __init__( self, name="cnt_a", type="signal",vcd_symbol="",visible=True, \
                bits_per_line=32, bits_total=32,format="hex"):
    self.name          = name;
    self.type          = type;# "signal","divider","group","endgroup"
    self.nickname      = "";
    self.hier_name     = "";
    self.hier_level    = 0;
    self.vcd_symbol    = vcd_symbol;
    self.values        = [];

    self.trigger       = 0;# 0=OFF +1=Rising,-1=Falling,2=Pattern0,3=Pattern1
    self.data_enable   = False;

    self.selected      = False;
    self.last_value    = "";
    self.visible       = visible;
    self.hidden        = False;
    self.deleted       = False;
    self.expandable    = False;
    self.collapsable   = False;
    self.is_expansion  = False;
    self.grouped       = False;
    self.x             = 0;
    self.y             = 0;
    self.h             = 0;  # Height
    self.w             = 0;  # Width
    self.bits_per_line = 32;
    self.bits_total    = 32;
    self.bit_top       = 31;
    self.bit_bot       =  0;
    self.bit_weight    =  0;  # Only used by rip_list, ie [7]->128
    self.rip           =  ""; # [15:0], [1], ""
    self.format        = "hex";
  def __del__(self):
#   print "You are killing me man";
    return;
  def __str__(self):
    return "name     = " + self.name            + "" +\
        "";


##############################################################################
class Sump2:
  def __init__ ( self, backdoor, addr ):
    self.bd = backdoor;


    self.addr_ctrl = addr;
    self.addr_data = addr + 0x4;
    self.cmd_state_idle        = 0x00;
    self.cmd_state_arm         = 0x01;
    self.cmd_state_reset       = 0x02;# Always Reset before Arm.

    self.cmd_wr_trig_type      = 0x04;
    self.cmd_wr_trig_field     = 0x05;# Correspond to Event Bits
    self.cmd_wr_trig_dly_nth   = 0x06;# Trigger Delay and Nth
    self.cmd_wr_trig_position  = 0x07;# Samples post Trigger to Capture
    self.cmd_wr_rle_event_en   = 0x08;# Enables events for RLE detection
    self.cmd_wr_ram_ptr        = 0x09;# Load specific pointer.
    self.cmd_wr_ram_page       = 0x0a;# Load DWORD Page.

    self.cmd_rd_hw_id_rev      = 0x0b;
    self.cmd_rd_ram_width_len  = 0x0c;
    self.cmd_rd_sample_freq    = 0x0d;
    self.cmd_rd_trigger_ptr    = 0x0e;
    self.cmd_rd_ram_data       = 0x0f;

    self.cmd_wr_user_ctrl      = 0x10;
    self.cmd_wr_user_pattern0  = 0x11;# Also Mask    for Pattern Matching
    self.cmd_wr_user_pattern1  = 0x12;# Also Pattern for Pattern Matching
    self.cmd_wr_user_data_en   = 0x13;# Special Data Enable Capture Mode 
    self.cmd_wr_watchdog_time  = 0x14;# Watchdog Timeout

    self.trig_and_ris          = 0x00;# Bits AND Rising
    self.trig_and_fal          = 0x01;# Bits AND Falling
    self.trig_or_ris           = 0x02;# Bits OR  Rising
    self.trig_or_fal           = 0x03;# Bits OR  Falling
    self.trig_pat_ris          = 0x04;# Pattern Match Rising
    self.trig_pat_fal          = 0x05;# Pattern Match Falling
    self.trig_in_ris           = 0x06;# External Input Trigger Rising
    self.trig_in_fal           = 0x07;# External Input Trigger Falling
    self.trig_watchdog         = 0x08;# Watchdog trigger
    self.cfg_dict = {};

    self.status_armed          = 0x01;# Engine is Armed, ready for trigger
    self.status_triggered      = 0x02;# Engine has been triggered
    self.status_ram_post       = 0x04;# Engine has filled post-trig RAM 
    self.status_ram_pre        = 0x08;# Engine has filled pre-trigger RAM
    self.status_rle_pre        = 0x10;# RLE Engine has filled pre-trig RAM
    self.status_rle_post       = 0x20;# RLE Engine has filled post-trig RAM
    self.status_rle_en         = 0x80;# RLE Engine is present

  def wr ( self, cmd, data ):
    self.bd.wr( self.addr_ctrl, [ cmd  ] );
    self.bd.wr( self.addr_data, [ data ] );

  def rd( self, addr, num_dwords = 1):
    # Note: addr of None means use existing ctrl address and just read data
    if ( addr != None ):
      self.bd.wr( self.addr_ctrl, [ addr ] );
    return self.bd.rd( self.addr_data, num_dwords, repeat = True);

  def rd_cfg( self ):
    hwid_data = self.rd( self.cmd_rd_hw_id_rev     )[0];
    ram_data  = self.rd( self.cmd_rd_ram_width_len )[0];
    freq_data = self.rd( self.cmd_rd_sample_freq   )[0];
    print("%08x" % hwid_data );
    print("%08x" % freq_data );
    print("%08x" % ram_data );

    self.cfg_dict['hw_id']       = ( hwid_data & 0xFFFF0000 ) >> 16;
    self.cfg_dict['hw_rev']      = ( hwid_data & 0x0000FF00 ) >> 8;
    self.cfg_dict['data_en']     = ( hwid_data & 0x00000040 ) >> 6;
    self.cfg_dict['trig_wd_en']  = ( hwid_data & 0x00000020 ) >> 5;
#   self.cfg_dict['data_en']     = 1;# This bit doesn't exist yet in HW
#   self.cfg_dict['trig_wd_en']  = 1;# This bit doesn't exist yet in HW
    self.cfg_dict['nonrle_dis']  = ( hwid_data & 0x00000010 ) >> 4;
    self.cfg_dict['rle_en']      = ( hwid_data & 0x00000008 ) >> 3;
    self.cfg_dict['pattern_en']  = ( hwid_data & 0x00000004 ) >> 2;
    self.cfg_dict['trig_nth_en'] = ( hwid_data & 0x00000002 ) >> 1;
    self.cfg_dict['trig_dly_en'] = ( hwid_data & 0x00000001 ) >> 0;
    self.cfg_dict['frequency']   =  float(freq_data) / 65536.0;
    self.cfg_dict['ram_len']     = ( ram_data  & 0x0000FFFF ) >> 0;
    self.cfg_dict['ram_dwords']  = ( ram_data  & 0x00FF0000 ) >> 14;# >>16,<<2
    self.cfg_dict['ram_event_bytes'] = ( ram_data  & 0x0F000000 ) >> 24;
    self.cfg_dict['ram_rle']         = ( ram_data  & 0xF0000000 ) >> 28;

  def close ( self ):
    return;


##############################################################################
# functions to convert text time samples into a VCD file. See cpy_txt2vcd.py
class TXT2VCD:
  def __init__ ( self ):
    self.char_code = self.build_char_code(); # ['AA','BA',etc]
    self.header    = self.build_header();
    self.footer    = self.build_footer();
    return;

  def close ( self ):
    return;

  def conv_txt2vcd ( self, main_self, txt_list ):
    """
    Take in a txt list and spit out a vcd
    """
    header_line = txt_list[0];     # 1st line   "#foo bar 10000"
    data_lines  = txt_list[1:];    # Data lines "1 1a"

    bus_widths = self.get_bus_widths( data_lines[:] ); # How many bits in each

    rts =  self.header;
    rts += self.build_name_map( header_line,bus_widths[:],self.char_code[:] );
    rts += self.footer;

    timescale = float( header_line.split()[-1] ); time = 0;
    next_perc = 0;# Display an update every 5%
    total_count = len( data_lines );
    prev_data_line = None;
    prev_bit_value = None;
    for ( i, data_line ) in enumerate( data_lines ):
      if ( data_line != prev_data_line ):
        rts += [ "#" + str(time) ]; 
        bit_list = self.get_bit_value( data_line, header_line, bus_widths[:] );
        new_bit_value = self.dump_bit_value( bit_list, self.char_code[:] );
        final_bit_value = [];# Used for VCD delta compression
        # Convert SUMP compression ( something changed, so here is everything ) to
        # VCD compression ( only this signal changed since last clock )
        # Go thru all the signals and only VCD store the ones that changed from prev
        for ( j, ( each_bit, each_char_code ) ) in enumerate( new_bit_value ):
          if ( prev_bit_value == None ):
            final_bit_value += [ each_bit + each_char_code ];
          else:	    
            ( old_bit, old_char_code ) = prev_bit_value[j];
            if ( old_bit != each_bit ):
              final_bit_value += [ each_bit + each_char_code ];
        rts += final_bit_value;	
        prev_bit_value = new_bit_value;
        prev_data_line = data_line;
      time += int( timescale );

      # TODO: Would be nice to have this call draw_header() instead.
      perc = ( 100 * i ) // total_count;
      if ( perc >= next_perc ):
        draw_header( main_self,"conv_txt2vcd() "+str( perc )+"%" );
        print( "conv_txt2vcd() "+str( perc )+"%" );
        next_perc += 5;# Next 5%, this counts 0,5,10,...95
    return rts;

  def get_bit_value( self,data_line,header_line,bus_widths_list_cp ):
    """
    Figure out each bit value (0,1) for the provided line. Return a list of 0,1s
    """
    rts = [];
    data_list = data_line.split();
    for bus_name in header_line.split()[0:-1]:# Remove the timescale at very end
      bus_width = bus_widths_list_cp.pop(0); # 1 or 16, etc
      data      = data_list.pop(0);         # "1" or "10ab", etc
      bit_val = 2**(bus_width-1);           # 8->128, 4->8, 1->1
      for i in range( bus_width ):          # Counts 0..7  for 8bit bus
        try:
          if ( ( int(data,16) & bit_val ) == 0 ): rts += ["0"];
          else:                                   rts += ["1"];
        except:
          rts += ["x"];

        bit_val //= 2;                      # Counts 128,64,..2,1 for 8bit bus
    return rts;


  def dump_bit_value( self, bit_list, char_code_list_cp ):
    """
    Convert [0,1,etc] to [0AA,1BA,etc]
    """
    rts = [];
    for bit in bit_list:
      char_code = char_code_list_cp.pop(0);
#     rts += [ bit + char_code ];
      rts += [ (bit , char_code) ];
    return rts;


  def build_name_map( self,header_line,bus_widths_list_cp,char_code_list_cp ):
    """
     $var wire 1 AA foo  [7] $end
    """
    rts = [];
    for bus_name in header_line.split()[0:-1]:# This removes timescale at end
      bus_width = bus_widths_list_cp.pop(0);
      if ( bus_width == 1 ):
        rts += [ "$var wire 1 " + char_code_list_cp.pop(0) + " " + \
               bus_name + " $end" ];
      else:
        for i in range( bus_width ): # Counts 0..7  for 8bit bus
          rts += [ "$var wire 1 " + char_code_list_cp.pop(0) + " " + \
                    bus_name + " [" + str(bus_width-1-i)+"] $end" ];
    return rts;


  def get_bus_widths( self, data_list_cp ):
    """
    Rip the vectors, if any vector never exceeds 1 then its a wire. Tag it
    otherwise, bus width is number of nibbles x4
    """
    bus_width = [None]*100;
    for data_line in data_list_cp:
      data_words = data_line.split(); i = 0;
      for data_word in data_words:
        bit_width = 4 * len( data_word ); # How many bits 4,8,12,etc
        if ( bus_width[i] == None ): bus_width[i] = 1;# Default to single wire
        if ( data_word == "XXXXXXXX" ):
          bus_width[i] == 32;
        else:
          try:
            if ( int( data_word, 16) > 1 ): bus_width[i] = bit_width;
          except:
            print("ERROR: Invalid non Hexadecimal Data " + str(data_word));
        i+=1;
    return bus_width;


  def build_char_code( self ):
    """
    VCDs map wires to alphabet names such as AA,BA. Build a 676 (26x26) list
    """
    char_code = []; # This will be ['AA','BA',..,'ZZ']
    for ch1 in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
      for ch2 in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        char_code += [ ch2+ch1 ];
    return char_code;

  def build_header( self ):
    rts  = [];
    rts += [ "$date      Wed May  4 10:12:46 2005 $end" ];
    rts += [ "$version   ModelSim Version 6.0c    $end" ];
    rts += [ "$timescale 1ps                      $end" ];
    rts += [ "$scope     module   module_name     $end" ];
    return rts;

  def build_footer( self ):
    rts  = [];
    rts += [ "$upscope $end"];
    rts += [ "$enddefinitions $end"];
    rts += [ "#0" ];
    rts += [ "$dumpvars"];
    rts += [ "$end"];
    return rts;


##############################################################################
# functions to send Backdoor commands to BD_SERVER.PY over TCP Sockets
class Backdoor:
  def __init__ ( self, ip, port  ):
    try:
      import socket;
    except:
      raise RuntimeError("ERROR: socket is required");
    try:
      self.sock=socket.socket(socket.AF_INET,socket.SOCK_STREAM);
      self.sock.connect( ( ip, port ) );# "localhost", 21567
#     self.sock.settimeout(1); # Dont wait forever
      self.sock.settimeout(5); # Dont wait forever
    except:
#     raise RuntimeError("ERROR: Unable to open Socket!! ")
      self.sock = None;
      return;
  def close ( self ):
    self.sock.close();

  def bs(self, addr, bitfield ):
    rts = self.rd( addr, 1 );
    data_new = rts[0] | bitfield[0];  # OR in some bits
    self.wr( addr, [data_new] );

  def bc(self, addr, bitfield ):
    rts = self.rd( addr, 1 );
    data_new = rts[0] & ~ bitfield[0];# CLR some bits
    self.wr( addr, [data_new] );

  def wr(self, addr, data, repeat = False ):
#   print("%08x" % addr );
#   print( data );
    if ( repeat == False ):
      cmd = "w";# Normal Write : Single or Burst with incrementing address
    else:
      cmd = "W";# Write Multiple DWORDs to same address
#   payload = "".join( [cmd + " %x" % addr] +
#                      [" %x" % int(d) for d in data] +
#                      ["\n"] );
    payload = "".join( [cmd + " %08x" % addr] +
                       [" %08x" % int(d) for d in data] +
                       ["\n"] );
    self.tx_tcp_packet( payload );
    self.rx_tcp_packet();

  def rd( self, addr, num_dwords=1, repeat = False ):
    if ( repeat == False ):
      cmd = "r";# Normal Read : Single or Burst with incrementing address
    else:
      cmd = "k";# Read Multiple DWORDs from single address
#   payload = cmd + " %x %x\n" % (addr, (num_dwords-1)); # 0=1DWORD,1=2DWORDs
    payload = cmd + " %08x %08x\n" % (addr,(num_dwords-1));# 0=1DWORD,1=2DWORDs
    self.tx_tcp_packet( payload );
    payload = self.rx_tcp_packet().rstrip();
    dwords = payload.split(' ');
    rts = [];
#   print( dwords );
    for dword in dwords:
      rts += [int( dword, 16 )];
    return rts;

  def tx_tcp_packet( self, payload ):
    # A Packet is a 8char hexadecimal header followed by the payload. 
    # The header is the number of bytes in the payload. 
    header = "%08x" % len(payload);
    bin_data = (header+payload).encode("utf-8");# String to ByteArray
    self.sock.send( bin_data );

  def rx_tcp_packet( self ):
    # Receive 1+ Packets of response. 1st Packet will start with header that 
    # indicates how big the entire Backdoor payload is. Sit in a loop 
    # receiving 1+ TCP packets until the entire payload is received.
    bin_data = self.sock.recv(1024);
    rts = bin_data.decode("utf-8");# ByteArray to String
    header      = rts[0:8];      # Remove the header, Example "00000004"
    payload_len = int(header,16);# The Payload Length in Bytes, Example 0x4
    payload     = rts[8:];       # Start of Payload is everything after header
    # 1st packet may not be entire payload so loop until we have it all
    while ( len(payload) < payload_len ):
      bin_data = self.sock.recv(1024);
      payload += bin_data.decode("utf-8");# ByteArray to String
    return payload;


###############################################################################
# Routines for Reading and Writing a remote 32bit LocalBus over MesaBus
# A local bus cycle is a pre-determined payload transported over the MesaBus
# LocalBus is mapped to SubSlot 0x0
#  0x0 : Write DWORD or Burst starting at Address
#  0x1 : Read  DWORD or Burst starting at Address
#  0x2 : Write Multiple DWORDs to same Address
#  0x3 : Read Multiple DWORDs from same Address
class local_bus:
  def __init__ ( self, mesa_bus, dbg_flag ):
    self.mesa_bus = mesa_bus;
    self.dbg_flag = dbg_flag;

  def wr( self, addr, data, repeat = False ):
    # LocalBus WR cycle is a Addr+Data 8byte payload
    # Mesabus has maximum payload of 255 bytes, or 63 DWORDs.
    # 1 DWORD is LB Addr, leaving 62 DWORDs available for data bursts
    # if data is more than 62 dwords, parse it into multiple bursts
    each_addr = addr;
    data_list = data;
    while ( len( data_list ) > 0 ):
#     if ( len( data_list ) > 62 ):
#       data_payload = data_list[0:62];
#       data_list    = data_list[62:];
#     else:
#       data_payload = data_list[0:];
#       data_list    = [];
      # Note: MesaBus on icoboard doesn't support bursts writes yet.
      if ( len( data_list ) > 1 ):
        data_payload = data_list[0:1];
        data_list    = data_list[1:];
      else:
        data_payload = data_list[0:];
        data_list    = [];

      payload = ( "%08x" % each_addr );
      for each_data in data_payload:
        payload += ( "%08x" % each_data );
        each_addr +=4;
#     print(payload);
      self.mesa_bus.wr( 0x00, 0x0, 0x0, payload );
    return;


  def rd( self, addr, num_dwords = 1, repeat = False ):
    dwords_remaining = num_dwords;
    each_addr = addr;
    rts = [];
    rts_dword = "00000000";
    while ( dwords_remaining > 0 ):
      # MesaBus has 255 byte limit on payload, so split up large reads
#     if ( dwords_remaining > 62 ):
#       n_dwords = 62;
#       dwords_remaining -= 62;
#     else:
#       n_dwords = dwords_remaining;
#       dwords_remaining = 0;

      # Note: MesaBus on icoboard doesn't support bursts or repeat reads yet.
      if ( dwords_remaining > 1 ):
        n_dwords = 1;
        dwords_remaining -= 1;
      else:
        n_dwords = dwords_remaining;
        dwords_remaining = 0;

      # LocalBus RD cycle is a Addr+Len 8byte payload to 0x00,0x0,0x1
      payload = ( "%08x" % each_addr ) + ( "%08x" % n_dwords );
      self.mesa_bus.wr( 0x00, 0x0, 0x1, payload );
      rts_mesa = self.mesa_bus.rd();
      # The Mesa Readback Ro packet resembles a Wi Write packet from slot 0xFE
      # This is to support a synchronous bus that clocks 0xFFs for idle
      # This only handles single DWORD reads and checks for:
      #   "F0FE0004"+"12345678" + "\n"
      #   "04" is num payload bytes and "12345678" is the read payload
      if ( len( rts_mesa ) > 8 ):
        rts_str = rts_mesa[8:];# Strip the FOFE0004 header
        while ( len( rts_str ) >= 8 ):
          rts_dword = rts_str[0:8];
          rts_str   = rts_str[8:];
          try:
            rts += [ int( rts_dword, 16 ) ];
          except:
#           print("ERROR:Invalid LocalBus Read "+rts_mesa+" "+rts_dword);
            if ( self.dbg_flag == "debug" ):
              sys.exit();
            rts += [ 0x00000000 ];
      else:
#       print("ERROR: Invalid LocalBus Read " + rts_mesa + " " + rts_dword);
        if ( self.dbg_flag == "debug" ):
          sys.exit();
        rts += [ 0x00000000 ];
      if ( repeat == False ):
        each_addr += ( 4 * n_dwords );# Note dword to byte addr translation
#   print( rts );
    return rts;


###############################################################################
# Routines for Reading and Writing Payloads over MesaBus
# A payload is a series of bytes in hexadecimal string format. A typical use
# for MesaBus is to transport a higher level Local Bus protocol for 32bit
# writes and reads. MesaBus is lower level and transports payloads to a
# specific device on a serial chained bus based on the Slot Number.
# More info at : https://blackmesalabs.wordpress.com/2016/03/04/mesa-bus/
class mesa_bus:
  def __init__ ( self, port, os_sys ):
    self.port = port;
    self.os_sys = os_sys;

  def wr( self, slot, subslot, cmd, payload ):
    preamble  = "FFF0";
    slot      = "%02x" % slot;
    subslot   = "%01x" % subslot;
    cmd       = "%01x" % cmd;
    num_bytes = "%02x" % ( len( payload ) / 2 );
    mesa_str  = preamble + slot + subslot + cmd + num_bytes + payload;
#   if ( type( self.port ) == spidev.SpiDev ):
    if ( self.os_sys == "Linux" ):
      mesa_hex_list = [];
      for i in range( 0, len(mesa_str)/2 ):
        mesa_hex = int(mesa_str[i*2:i*2+2],16);
        mesa_hex_list += [mesa_hex];
      rts = self.port.xfer2( mesa_hex_list );
    else:
      self.port.wr( mesa_str );
    return;

  def rd( self ):
#   if ( type( self.port ) == spidev.SpiDev ):
    if ( self.os_sys == "Linux" ):
      hex_str = "";
      rts = self.port.xfer2(8*[0xFF]);
      for each in rts:
        hex_str += ("%02x" % each );
      rts = hex_str;
    else:
      rts = self.port.rd();
    return rts;


###############################################################################
main = main();
