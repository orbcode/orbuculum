//================================================================================
// POV-Ray spectral emission data V0.21
//
// format for all specular definitions: 
// 36 columns for the wavelengths of 380nm to 730nm in 10nm steps
// converted from lightsysIV "espd_lightsys.inc"
//
// All "#declares" and "#macros" prefixed "E_" can be used directly as spectral
// data for light sources and emitting objects.
//
// - Ive, April 2014
//
//================================================================================

#ifndef (spectral_lights_inc) #declare spectral_lights_inc = 1;


#if (SpectralMode)   

//================================================================================
// SPECTRAL MODE
//================================================================================
// ready made stuff: use any of these declares and macros 


// Lamps
#declare E_GE_SW_Incandescent_60w          = array[36]; // GE Soft White incandescent 60 watt
#declare E_GE_SW_Incandescent_100w         = array[36]; // GE Soft White incandescent 100 watt
#declare E_Nikon_SB16_XenonFlash           = array[36]; // Nikon SB-16 xenon flash
#declare E_GTE_341_Warm                    = array[36]; // warm white fluorescent tube
#declare E_GTE_341_Cool                    = array[36]; // cool white fluorescent tube

#declare E_Osram_CoolBeam_20w              = array[36]; // Osram Halogen 20 watt
#declare E_Osram_CoolFluor_36w             = array[36]; // Osram Fluor 36 watt    

#declare E_Phillips_PLS_11w                = array[36]; // Phillips PL-S compact white fluorescent 11 watt
#declare E_Phillips_Mastercolor_3K         = array[36]; // Phillips Mastercolor 3000
#declare E_Phillips_HPS                    = array[36]; // Phillips High Pressure Sodium

#declare E_Mitsubishi_Metal_Halide         = array[36]; // ca. 4274K
#declare E_Mitsubishi_Daylight_Fluorescent = array[36]; // ca. 6245K
#declare E_Mitsubishi_Moon_Fluorescent     = array[36]; // ca. 4939K
#declare E_Mitsubishi_Standard_Fluorescent = array[36]; // ca. 4091K   

#declare E_Solux_Halog4700K                = array[36]; // Solux Halogen 47 VOR
#declare E_Cornell_Box_Light               = array[36]; // tungsten flood light with UV filter and diffusing glass plate

#declare E_Sunlight                        = array[36]; // sunlight (terrestrial - filtered by the atmosphere)
#declare E_Extraterrestrial_Sun            = array[36]; // sunlight (outer space - non filtered)     


//================================================================================

// CIE D
#macro E_Daylight(K) // K = temperature in Kelvin (4000° - 25000°)
  #local M1 = 0; 
  #local M2 = 0;   
  DaylightM1M2(K, M1, M2)
    
  #local Tmp = array[36];
  #for (I, 0, 35)    
     #declare Tmp[I] = (DS012[I*2][0] + M1*DS012[I*2][1] + M2*DS012[I*2][2]);
  #end
  SpectralNormalize(Tmp)
  Tmp
#end   


// CIE B
#macro E_Blackbody(K) // K = temperature in Kelvin 

  #local Tmp = array[36];
  #for (I, 0, 35)    
    #local W = 380 + I*10;
    #declare Tmp[I] = PlanckBlackBody(W*1e-9, K);
  #end  
  SpectralNormalize(Tmp)
  Tmp
#end


// some CIE standard illuminants
#declare E_D50 = E_Daylight(5003); // CIE D50 overcast sky with reflected sunlight
#declare E_D65 = E_Daylight(6504); // CIE D65 overcast sky (neutral)
#declare E_D75 = E_Daylight(7500); // CIE D75 blue overcast sky
#declare E_D93 = E_Daylight(9300); // CIE D93 clear deep blue sky


#macro SpectralEmission(S)
  rgb S[WavelengthIndex]
#end


//================================================================================
// internal stuff: fill the arrays from the espd_lightsys splines

#for (I, 0, 35)

  #local W = 380 + I*10;

  #declare E_GE_SW_Incandescent_60w[I]   = ES_GE_SW_Incandescent_60w(W).y;
  #declare E_GE_SW_Incandescent_100w[I]  = ES_GE_SW_Incandescent_100w(W).y;
  #declare E_Nikon_SB16_XenonFlash[I]    = ES_Nikon_SB16_XenonFlash(W).y;
  #declare E_GTE_341_Warm[I]             = ES_GTE_341_Warm(W).y;
  #declare E_GTE_341_Cool[I]             = ES_GTE_341_Cool(W).y;
  
  #declare E_Osram_CoolBeam_20w[I]       = ES_Osram_CoolBeam_20w(W).y;  
  #declare E_Osram_CoolFluor_36w[I]      = ES_Osram_CoolFluor_36w(W).y;
  
// NOTE: I'm cheating a bit with the Phillips lamps as they have sometimes
// significant peaks in between the 10nm steps and as those peaks are the 
// most interesting part for specular rendering I won't miss them.
  #declare E_Phillips_PLS_11w[I]         = max(ES_Phillips_PLS_11w(W).y, ES_Phillips_PLS_11w(W+5).y);
  #declare E_Phillips_Mastercolor_3K[I]  = max(ES_Phillips_Mastercolor_3K(W).y, ES_Phillips_Mastercolor_3K(W+5).y);
  #declare E_Phillips_HPS[I]             = max(ES_Phillips_HPS(W).y, ES_Phillips_HPS(W+5).y);
  
  #declare E_Mitsubishi_Metal_Halide[I]         = ES_Mitsubishi_Metal_Halide(W).y;
  #declare E_Mitsubishi_Daylight_Fluorescent[I] = ES_Mitsubishi_Daylight_Fluorescent(W).y;
  #declare E_Mitsubishi_Moon_Fluorescent[I]     = ES_Mitsubishi_Moon_Fluorescent(W).y;
  #declare E_Mitsubishi_Standard_Fluorescent[I] = ES_Mitsubishi_Standard_Fluorescent(W).y;

  #declare E_Solux_Halog4700K[I]         = ES_Solux_Halog4700K(W).y;
  #declare E_Cornell_Box_Light[I]        = ES_Cornell_Box_Light(W).y;

  #declare E_Sunlight[I]                 = ES_Sunlight(W).y;
  #declare E_Extraterrestrial_Sun[I]     = ES_Extraterrestrial_Sun(W).y;

#end   

SpectralNormalize (E_Cornell_Box_Light)
SpectralNormalize (E_Sunlight)
SpectralNormalize (E_Extraterrestrial_Sun)


//================================================================================

#else // !SpectralMode - the preview functions simply call the CIE.inc counterparts

//================================================================================
// PREVIEW MODE
//================================================================================

#declare E_GE_SW_Incandescent_60w   = Emissive2RGB(ES_GE_SW_Incandescent_60w);
#declare E_GE_SW_Incandescent_100w  = Emissive2RGB(ES_GE_SW_Incandescent_100w);
#declare E_Nikon_SB16_XenonFlash    = Emissive2RGB(ES_Nikon_SB16_XenonFlash);
#declare E_GTE_341_Warm             = Emissive2RGB(ES_GTE_341_Warm);
#declare E_GTE_341_Cool             = Emissive2RGB(ES_GTE_341_Cool);
  
#declare E_Osram_CoolBeam_20w       = Emissive2RGB(ES_Osram_CoolBeam_20w);
#declare E_Osram_CoolFluor_36w      = Emissive2RGB(ES_Osram_CoolFluor_36w);
  
#declare E_Phillips_PLS_11w         = Emissive2RGB(ES_Phillips_PLS_11w);
#declare E_Phillips_Mastercolor_3K  = Emissive2RGB(ES_Phillips_Mastercolor_3K);
#declare E_Phillips_HPS             = Emissive2RGB(ES_Phillips_HPS);

#declare E_Mitsubishi_Metal_Halide         = Emissive2RGB(ES_Mitsubishi_Metal_Halide);
#declare E_Mitsubishi_Daylight_Fluorescent = Emissive2RGB(ES_Mitsubishi_Daylight_Fluorescent);
#declare E_Mitsubishi_Moon_Fluorescent     = Emissive2RGB(ES_Mitsubishi_Moon_Fluorescent);
#declare E_Mitsubishi_Standard_Fluorescent = Emissive2RGB(ES_Mitsubishi_Standard_Fluorescent);

#declare E_Solux_Halog4700K         = Emissive2RGB(ES_Solux_Halog4700K);
#declare E_Cornell_Box_Light        = Emissive2RGB(ES_Cornell_Box_Light);

#declare E_Sunlight                 = Emissive2RGB(ES_Sunlight);
#declare E_Extraterrestrial_Sun     = Emissive2RGB(ES_Extraterrestrial_Sun);


#declare E_D50  = Daylight(5003);
#declare E_D65  = Daylight(6504); 
#declare E_D75  = Daylight(7500); 
#declare E_D93  = Daylight(9300); 


#macro E_Daylight(K)
  Daylight(K)
#end


#macro E_Blackbody(K)
  Blackbody(K)
#end
  

#macro SpectralEmission(S)
  rgb S
#end


#end // !SpectralMode

//================================================================================

#end // ifndef spectral_lights_inc