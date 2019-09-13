//================================================================================
// POV-Ray spectral render materials V0.21
//
// Metals 
// ******
//
// Metals have no measured diffuse reflectance but a complex number where
// N (refraction) is the real part and K (extinction) the imaginary part.
// The actual (diffuse) reflectance is calculated "on the fly" from this complex
// number using this fresnel equation:
// R(w) = ( ( sqr(N(w)-1) + sqr(K(w)) ) / ( sqr(N(w)+1) + sqr(K(w)) ) )
//
// You'll find at the end of the file the section "high level interface" where
// the macros meant to be used are prefixed M_***
//
// All measurement was taken from very thin highly polished samples while
// *perfect* polishing would be impossible. 
// Actually the Reflection parameter should only control how polished the
// surface is (i.e. how blurred the reflection) and the definition of the fresnel
// reflection itself remain constant. But while keeping compatible to official
// POV this is currently out of the scope of this include file.
// As such the Reflection parameter is mostly intended for "artistic" usage,
// realistic values would be about 1.0 for chrome down to about 0.8 for iron.  
// As the absorption for all metals is around 10% when viewed directly down to 5% at
// shallow angels (in addition to the fresnel reflection falloff) I've adjusted
// the macro M_Spectral_Metal in that way.
// The small amount of diffuse is meant to simulate some photons that are scattered
// around the surface and do indeed bounce diffuse back, but again this should be 
// blurred reflection. 
// 
//
// - Ive, January 2014
//
//================================================================================
    
#ifndef (spectral_metals_inc) #declare spectral_metals_inc = 1;    


// fresnel reflectivity calculation for a given wavelength
#macro MetalIndex (N,K,I)
  ( ( pow(N[I]-1, 2) + pow(K[I], 2) ) / 
    ( pow(N[I]+1, 2) + pow(K[I], 2) ) )
#end               


#if (SpectralMode) 

//================================================================================
// SPECTRAL MODE
//================================================================================
// internal metal helper macros for the use with spectral data.


#macro D_Metal (N,K)
  
  #local Tmp = array[36];

  #for(I, 0, 35)
    #declare Tmp[I] = MetalIndex (N,K,I);
  #end
  
  Tmp

#end               


#macro C_Metal (N,K) 
  rgb MetalIndex (N,K,WavelengthIndex) 
#end               


//================================================================================

#else // !SpectralMode

//================================================================================
// PREVIEW MODE
//================================================================================
// internal metal helper macros for preview - corresponding to those with spectral data.

#macro D_Metal (N,K)
  /*
  creating a linear spline "on the fly" as this is what CIE.inc (and the function
  Reflective2RGB() expects. It finally returns a simple rgb<> color.
  */

  #local TS = spline { linear_spline
   #for (I, 0, 35)
    #local W = 380 + I*10;

     W, MetalIndex(N,K,I)

   #end        
  }        
      
  Reflective2RGB(TS)   
   
#end  


#macro C_Metal (N,K)
  rgb D_Metal (N,K)
#end  




//================================================================================

#end // !SpectralMode



//================================================================================
// helper macros
//================================================================================

// IOR
#macro IOR_Metal (N,K)
  ior pow(K[WavelengthIndex],2) / pow(N[WavelengthIndex],2)
#end


// extinction
#macro EXT_Metal (N,K)
  K[WavelengthIndex]
#end



#macro M_Spectral_Metal (N, K, Reflectance)
  material {
    texture {
      pigment { C_Metal(N, K) }
      finish {
        ambient 0  emission 0  diffuse (0.5 - Reflectance*0.45)
        reflection {
          #if (SpectralMode)
            Reflectance * MetalIndex(N,K,WavelengthIndex) * 0.9, 0.95
          #else
            Reflectance * 0.9, 0.95 metallic
          #end            
          fresnel on
        }
        conserve_energy
        brilliance EXT_Metal(N, K)
        metallic
      }
    }
    interior { IOR_Metal(N, K) }
  }
#end




#macro Spectral_Average (Data1, F1, Data2, F2)
  #local F = F1 + F2; 
  
  #if ((F <= 0.0) | (F1 < 0) | (F2 < 0))
    error("Spectral_Average: F1 + F2 has to be > 0.0");
  #end  

  #local FF1 = F1/F;
  #local FF2 = F2/F;

  #local Tmp = array[36];

  #for(I, 0, 35)
    #declare Tmp[I] = Data1[I]*FF1 + Data2[I]*FF2;
  #end
  
  Tmp
#end



#macro Spectral_Average3 (Data1, F1, Data2, F2, Data3, F3)
  #local F = F1 + F2 + F3; 
  
  #if ((F <= 0.0) | (F1 < 0) | (F2 < 0))
    error("Spectral_Average3: F1 + F2 + F3 has to be > 0.0");
  #end  

  #local FF1 = F1/F;
  #local FF2 = F2/F;
  #local FF3 = F3/F;

  #local Tmp = array[36];

  #for(I, 0, 35)
    #declare Tmp[I] = Data1[I]*FF1 + Data2[I]*FF2 + Data3[I]*FF3;
  #end
  
  Tmp
#end



#macro M_Spectral_Alloy (N1, K1, F1, N2, K2 F2, Reflectance) 
  #local N = Spectral_Average (N1, F1, N2, F2);
  #local K = Spectral_Average (K1, F1, K2, F2);
  M_Spectral_Metal (N, K, Reflectance)
#end



#macro M_Spectral_Alloy3 (N1, K1, F1, N2, K2 F2, N3, K3 F3, Reflectance) 
  #local N = Spectral_Average3 (N1, F1, N2, F2, N3, F3);
  #local K = Spectral_Average3 (K1, F1, K2, F2, K3, F3);
  M_Spectral_Metal (N, K, Reflectance)
#end


// wavelength nm                       380      390      400      410      420      430      440      450      460      470      480      490      500      510      520      530      540      550      560      570      580      590      600      610      620      630      640      650      660      670      680      690      700      710      720      730

#declare IOR_N_Aluminium = array[36] { 0.43745, 0.46265, 0.48784, 0.51304, 0.54101, 0.57035, 0.59969, 0.63324, 0.66843, 0.70362, 0.73927, 0.77592, 0.81257, 0.84921, 0.88783, 0.93029, 0.97274, 1.01519, 1.05764, 1.10625, 1.15827, 1.21030, 1.26232, 1.31434, 1.36645, 1.43031, 1.49417, 1.55803, 1.62703, 1.69751, 1.76800, 1.83981, 1.92139, 2.00298, 2.08456, 2.16738 }
#declare IOR_K_Aluminium = array[36] { 4.58661, 4.71107, 4.83552, 4.95998, 5.08430, 5.20855, 5.33281, 5.45435, 5.57484, 5.69533, 5.81463, 5.93134, 6.04806, 6.16477, 6.28104, 6.39645, 6.51187, 6.62728, 6.74270, 6.85478, 6.96502, 7.07526, 7.18550, 7.29573, 7.40592, 7.50808, 7.61025, 7.71241, 7.80423, 7.89304, 7.98186, 8.06881, 8.14197, 8.21514, 8.28831, 8.35851 }

#declare IOR_N_Chrome    = array[36] { 1.91761, 1.96178, 2.01050, 2.06210, 2.12128, 2.18583, 2.25115, 2.32444, 2.40701, 2.49579, 2.59167, 2.69222, 2.78191, 2.85932, 2.94188, 3.03334, 3.11515, 3.17240, 3.20804, 3.22244, 3.22139, 3.21044, 3.19542, 3.17844, 3.15981, 3.14055, 3.12199, 3.10440, 3.08994, 3.07652, 3.06769, 3.05966, 3.05680, 3.05478, 3.05748, 3.06059 }
#declare IOR_K_Chrome    = array[36] { 2.73668, 2.78563, 2.85060, 2.91028, 2.97307, 3.03354, 3.08587, 3.13556, 3.18391, 3.23125, 3.26771, 3.28974, 3.30703, 3.32214, 3.33077, 3.33198, 3.32972, 3.32657, 3.32179, 3.31481, 3.30712, 3.30027, 3.29792, 3.29795, 3.30008, 3.30794, 3.31690, 3.32738, 3.33847, 3.34977, 3.36054, 3.37091, 3.37853, 3.38608, 3.39329, 3.40061 }

#declare IOR_N_Copper    = array[36] { 1.20921, 1.18000, 1.18000, 1.18000, 1.17773, 1.17434, 1.17095, 1.16576, 1.15988, 1.15400, 1.14759, 1.14005, 1.13251, 1.12497, 1.10786, 1.07214, 1.03127, 0.94392, 0.85657, 0.73949, 0.60641, 0.47332, 0.40422, 0.33778, 0.27182, 0.25403, 0.23624, 0.21845, 0.21442, 0.21499, 0.21390, 0.21310, 0.21359, 0.21469, 0.21900, 0.22327 }
#declare IOR_K_Copper    = array[36] { 2.12561, 2.21000, 2.21000, 2.21000, 2.24407, 2.29492, 2.34576, 2.38965, 2.43082, 2.47200, 2.50804, 2.53317, 2.55829, 2.58342, 2.59848, 2.59402, 2.59041, 2.59449, 2.59857, 2.65074, 2.72881, 2.80688, 2.94993, 3.09569, 3.24132, 3.37322, 3.50512, 3.63702, 3.75051, 3.85785, 3.96461, 4.06863, 4.16176, 4.25455, 4.34545, 4.43598 }

#declare IOR_N_Nickel    = array[36] { 1.61000, 1.61000, 1.61000, 1.61000, 1.61472, 1.62000, 1.62000, 1.62878, 1.64090, 1.65220, 1.66163, 1.66689, 1.67792, 1.69725, 1.71604, 1.73382, 1.75184, 1.77224, 1.79265, 1.81194, 1.83060, 1.84925, 1.87603, 1.90315, 1.93031, 1.96137, 1.98941, 2.01294, 2.04471, 2.08000, 2.11158, 2.14350, 2.17850, 2.21350, 2.24850, 2.28364 }
#declare IOR_K_Nickel    = array[36] { 2.25551, 2.31200, 2.36000, 2.42015, 2.47775, 2.53471, 2.59353, 2.65390, 2.71452, 2.77102, 2.82958, 2.89274, 2.95575, 3.01855, 3.07964, 3.13742, 3.19514, 3.25229, 3.30943, 3.36582, 3.42179, 3.47776, 3.53532, 3.59295, 3.65056, 3.70646, 3.75882, 3.80588, 3.85706, 3.91000, 3.95737, 4.00450, 4.04950, 4.09450, 4.13950, 4.18318 }

#declare IOR_N_Silver    = array[36] { 0.19708, 0.18820, 0.17300, 0.17300, 0.16687, 0.15951, 0.15755, 0.15129, 0.14346, 0.13668, 0.13167, 0.13062, 0.13000, 0.13000, 0.12985, 0.12940, 0.12867, 0.12500, 0.12132, 0.12024, 0.12061, 0.12099, 0.12425, 0.12764, 0.13103, 0.13378, 0.13653, 0.13928, 0.14000, 0.14000, 0.14000, 0.14024, 0.14221, 0.14419, 0.14616, 0.14792 }
#declare IOR_K_Silver    = array[36] { 1.72102, 1.83800, 1.95000, 2.07030, 2.18077, 2.28288, 2.37438, 2.47024, 2.56723, 2.65763, 2.74611, 2.83032, 2.91763, 3.00942, 3.09720, 3.17720, 3.25735, 3.33898, 3.42061, 3.50015, 3.57851, 3.65687, 3.73159, 3.80617, 3.88083, 3.96339, 4.04596, 4.12853, 4.20928, 4.28939, 4.36950, 4.44889, 4.52296, 4.59704, 4.67111, 4.74537 }

#declare IOR_N_Gold      = array[36] { 1.68798, 1.67080, 1.65800, 1.64146, 1.62656, 1.60718, 1.57188, 1.50229, 1.41768, 1.31373, 1.18881, 1.01723, 0.85500, 0.70620, 0.57687, 0.48532, 0.39847, 0.35929, 0.32011, 0.29593, 0.28020, 0.26447, 0.24874, 0.23301, 0.21728, 0.20155, 0.18582, 0.17009, 0.16477, 0.16312, 0.16146, 0.16012, 0.16111, 0.16209, 0.16308, 0.16415 }
#declare IOR_K_Gold      = array[36] { 1.91693, 1.94000, 1.95600, 1.95750, 1.94951, 1.93412, 1.91059, 1.87854, 1.84374, 1.81549, 1.80318, 1.82634, 1.89546, 2.03072, 2.18347, 2.37013, 2.55249, 2.69127, 2.83004, 2.89942, 2.92975, 2.96009, 2.99043, 3.02076, 3.05110, 3.08144, 3.11178, 3.14211, 3.28287, 3.46243, 3.64199, 3.81630, 3.95210, 4.08790, 4.22370, 4.35783 }

#declare IOR_N_Platinum  = array[36] { 1.66103, 1.68832, 1.72022, 1.74261, 1.76887, 1.79654, 1.82268, 1.84756, 1.87181, 1.89441, 1.91816, 1.94447, 1.97386, 2.00768, 2.04062, 2.07187, 2.10285, 2.13130, 2.15976, 2.18433, 2.20672, 2.22910, 2.25278, 2.27651, 2.30025, 2.32479, 2.34933, 2.37387, 2.40686, 2.44267, 2.47848, 2.51356, 2.54319, 2.57281, 2.60244, 2.63200 }
#declare IOR_K_Platinum  = array[36] { 2.71573, 2.77664, 2.84060, 2.90030, 2.96246, 3.02471, 3.08353, 3.14390, 3.20452, 3.26102, 3.31958, 3.38274, 3.44377, 3.50174, 3.55973, 3.61777, 3.67407, 3.71472, 3.75537, 3.80582, 3.86179, 3.91776, 3.96881, 4.01966, 4.07058, 4.12887, 4.18715, 4.24543, 4.29512, 4.34196, 4.38879, 4.43593, 4.48531, 4.53469, 4.58407, 4.63322 }

#declare IOR_N_Iron      = array[36] { 2.11234, 2.18836, 2.26116, 2.32917, 2.40153, 2.47354, 2.53351, 2.58522, 2.62938, 2.66658, 2.69534, 2.72039, 2.75680, 2.80722, 2.85589, 2.89567, 2.92992, 2.95137, 2.95699, 2.95195, 2.94205, 2.92776, 2.90614, 2.88682, 2.87964, 2.88729, 2.90214, 2.91576, 2.91974, 2.91096, 2.89484, 2.87703, 2.86317, 2.85730, 2.85572, 2.85717 }
#declare IOR_K_Iron      = array[36] { 2.49479, 2.54602, 2.59398, 2.63805, 2.67497, 2.70684, 2.73801, 2.76723, 2.79329, 2.81756, 2.84303, 2.86773, 2.88662, 2.89926, 2.90905, 2.91635, 2.92176, 2.93130, 2.94743, 2.96690, 2.98639, 3.00433, 3.02226, 3.03923, 3.05417, 3.06597, 3.07631, 3.08743, 3.10162, 3.11973, 3.14029, 3.16168, 3.18232, 3.20103, 3.21879, 3.23600 }




//================================================================================ 
//
// HIGH LEVEL INTERFACE
// ********************
//
//================================================================================ 

// pure metals

#macro M_Aluminium (Reflectance)
  M_Spectral_Metal(IOR_N_Aluminium, IOR_K_Aluminium, Reflectance)
#end


#macro M_Chrome (Reflectance)
  M_Spectral_Metal(IOR_N_Chrome, IOR_K_Chrome, Reflectance)
#end


#macro M_Copper (Reflectance)
  M_Spectral_Metal(IOR_N_Copper, IOR_K_Copper, Reflectance)
#end


#macro M_Nickel (Reflectance)
  M_Spectral_Metal(IOR_N_Nickel, IOR_K_Nickel, Reflectance)
#end


#macro M_Silver (Reflectance)
  M_Spectral_Metal(IOR_N_Silver, IOR_K_Silver, Reflectance)
#end


#macro M_Gold (Reflectance)
  M_Spectral_Metal(IOR_N_Gold, IOR_K_Gold, Reflectance)
#end


#macro M_Platinum (Reflectance)
  M_Spectral_Metal(IOR_N_Platinum, IOR_K_Platinum, Reflectance)
#end


#macro M_Iron (Reflectance)
  M_Spectral_Metal(IOR_N_Iron, IOR_K_Iron, Reflectance)
#end


// commonly used alloys for jewelry

#macro M_GoldCopper_Alloy (CopperPercentage, Reflectance)
  #local GoldPercentage = 100 - CopperPercentage;
  M_Spectral_Alloy (
    IOR_N_Gold,   IOR_K_Gold,   GoldPercentage,
    IOR_N_Copper, IOR_K_Copper, CopperPercentage,
    Reflectance 
  )
#end


#macro M_GoldSilver_Alloy (SilverPercentage, Reflectance)
  #local GoldPercentage = 100 - SilverPercentage;
  M_Spectral_Alloy (
    IOR_N_Gold,   IOR_K_Gold,   GoldPercentage,
    IOR_N_Silver, IOR_K_Silver, SilverPercentage,
    Reflectance 
  )
#end


#macro M_GoldNickel_Alloy (NickelPercentage, Reflectance)
  #local GoldPercentage = 100 - NickelPercentage;
  M_Spectral_Alloy (
    IOR_N_Gold,   IOR_K_Gold,   GoldPercentage,
    IOR_N_Nickel, IOR_K_Nickel, NickelPercentage,
    Reflectance 
  )
#end


#macro M_GoldPlatinum_Alloy (PlatinumPercentage, Reflectance)
  #local GoldPercentage = 100 - PlatinumPercentage;
  M_Spectral_Alloy (
    IOR_N_Gold,     IOR_K_Gold,     GoldPercentage,
    IOR_N_Platinum, IOR_K_Platinum, PlatinumPercentage,
    Reflectance 
  )
#end  


#macro M_GoldSilverCopper (Gold, Silver, Copper, Reflectance)
  M_Spectral_Alloy3 (
    IOR_N_Gold,   IOR_K_Gold,   Gold,
    IOR_N_Silver, IOR_K_Silver, Silver,
    IOR_N_Copper, IOR_K_Copper, Copper,
    Reflectance 
  )
#end



//================================================================================ 

#end //  #ifndef (spectral_metals_inc)