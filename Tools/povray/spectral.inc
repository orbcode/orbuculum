//================================================================================
// POV-Ray spectral rendering main include file  V0.22
//================================================================================
//
// Notes:
//
// If no animation is used (i.e. the value of "clock" is zero) the system will
// switch to rgb preview mode without spectral rendering.
// 
// But if in this case "SpectralWavelength" is declared before this file is
// included, the *one* file for the specified wavelength will be rendered.
//
// - Ive, April 2014
//
//================================================================================


#ifdef (SpectralWavelength)

  #if ((SpectralWavelength < 380) | (SpectralWavelength > 730))
     #error "\nSpectral rendering: Wavelength out of range [380-730]\n"
  #end
  #declare WavelengthIndex = (SpectralWavelength -380) / 10;

#else  

  #declare WavelengthIndex = clock - 1;

#end



#if (WavelengthIndex >= 0)

  #declare SpectralMode = on; 

  #debug "\nSpectral rendering: wavelength " 
  #debug concat(str(380 + WavelengthIndex*10,3,0), "nm\n\n")

#else    

  #declare SpectralMode = off; 
  #declare WavelengthIndex = 17;   // 550nm for the preview mode
        
  #debug "\nRGB preview rendering.\n\n"

#end  


//================================================================================


#include "CIE.inc"
#include "espd_lightsys.inc"

#include "spectral_materials.inc"
#include "spectral_lights.inc"
#include "spectral_glasses.inc"
#include "spectral_metals.inc"
#include "spectral_minerals.inc"


//================================================================================
