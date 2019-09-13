//================================================================================
// POV-Ray spectral render materials V0.22
//
// Minerals
// ********
//
// For all minerals the colors are calculated from absorbing instead
// of diffuse reflection.
//
// Ive, April 2014
//
//================================================================================
// Update November 2015
// - added missing macro M_Diamond_LemonYellow
//================================================================================

#ifndef (spectral_minerals_inc) #declare spectral_minerals_inc = 1;

//================================================================================


// Amethyst  pink  rgb <0.8981, 0.8441, 0.8972> 

#local S_IOR_Amethyst = spline { linear_spline
  380, 1.560613
  400, 1.559598
  420, 1.558582
  440, 1.557566
  460, 1.556551
  480, 1.555535
  500, 1.554520
  520, 1.553504
  540, 1.552488
  560, 1.551473
  580, 1.550457
  600, 1.549441
  620, 1.548426
  640, 1.547410
  660, 1.546395
  680, 1.545379
  700, 1.544363
  720, 1.543348
  740, 1.542332
}  


#local S_ABS_Amethyst = spline { linear_spline
  380, 0.248000
  400, 0.168000
  420, 0.113000
  440, 0.098000
  460, 0.101000
  480, 0.117000
  500, 0.138000
  520, 0.154000
  540, 0.160000
  560, 0.155000
  580, 0.142000
  600, 0.126000
  620, 0.109000
  640, 0.092000
  660, 0.079000
  680, 0.067000
  700, 0.057000
  720, 0.050000
  740, 0.045000
} 


// Emerald  green  rgb <0.7276, 0.9320, 0.8543>

#local S_IOR_Emerald = spline { linear_spline
  380, 1.602898
  400, 1.601483
  420, 1.600068
  440, 1.598654
  460, 1.597239
  480, 1.595824
  500, 1.594410
  520, 1.592995
  540, 1.591829
  560, 1.590797
  580, 1.589764
  600, 1.588890
  620, 1.588143
  640, 1.587397
  660, 1.586684
  680, 1.586103
  700, 1.585523
  720, 1.584942
  740, 1.584361
}  

#local S_ABS_Emerald = spline { linear_spline
  380, 0.184058
  400, 0.199834
  420, 0.220868
  440, 0.173539
  460, 0.141987
  480, 0.078881
  500, 0.036812
  520, 0.039441
  540, 0.068363
  560, 0.105175
  580, 0.147246
  600, 0.184058
  620, 0.231386
  640, 0.262939
  660, 0.241904
  680, 0.236644
  700, 0.131470
  720, 0.105175
  740, 0.089398
}  


// Ruby  red  rgb <0.8788, 0.3291, 0.6008>

#local S_IOR_Ruby = spline { linear_spline
  380, 1.774695
  400, 1.773289
  420, 1.771883
  440, 1.770477
  460, 1.769070
  480, 1.767664
  500, 1.766258
  520, 1.764852
  540, 1.763445
  560, 1.762039
  580, 1.760633
  600, 1.759227
  620, 1.757820
  640, 1.756414
  660, 1.755008
  680, 1.753602
  700, 1.752195
  720, 1.750789
  740, 1.749383
}

#local S_ABS_Ruby = spline { linear_spline
  380, 1.000000
  385, 1.000000
  390, 0.869714
  395, 0.747876
  400, 0.790648
  405, 0.721264
  410, 0.721296
  415, 0.711108
  420, 0.688742
  425, 0.689493
  430, 0.698973
  435, 0.693165
  440, 0.689509
  445, 0.608740
  450, 0.454494
  455, 0.338460
  460, 0.256066
  465, 0.202535
  470, 0.179838
  475, 0.201137
  480, 0.179536
  485, 0.209021
  490, 0.256152
  495, 0.312968
  500, 0.378655
  505, 0.456705
  510, 0.529905
  515, 0.598007
  520, 0.665409
  525, 0.718171
  530, 0.743652
  535, 0.741810
  540, 0.737309
  545, 0.763488
  550, 0.739814
  555, 0.732113
  560, 0.735976
  565, 0.719975
  570, 0.699403
  575, 0.653108
  580, 0.591774
  585, 0.507517
  590, 0.417545
  595, 0.341172
  600, 0.278328
  605, 0.224907
  610, 0.178696
  615, 0.139049
  620, 0.104245
  625, 0.075961
  630, 0.056322
  635, 0.044188
  640, 0.036911
  645, 0.030835
  650, 0.026564
  655, 0.024715
  660, 0.039440
  665, 0.029970
  670, 0.033350
  675, 0.025837
  680, 0.018808
  685, 0.014509
  690, 0.014071
  695, 0.045534
  700, 0.012844
  705, 0.011042
  710, 0.009917
  715, 0.009976
  720, 0.009405
  725, 0.009062
  730, 0.008901
}



// Sapphire  blue  rgb <-0.0044, 0.3702, 0.7609>   
// Note: negative color components are perfectly legal, they just indicate a color
//       outside of the gamut of the sRGB color space 

#local S_IOR_Sapphire = spline { linear_spline
  380, 1.420000
  400, 1.773289
  420, 1.771883
  440, 1.770477
  460, 1.769070
  480, 1.767664
  500, 1.766258
  520, 1.764852
  540, 1.763445
  560, 1.762039
  580, 1.760633
  600, 1.759227
  620, 1.757820
  640, 1.756414
  660, 1.755008
  680, 1.753602
  700, 1.752195
  720, 1.750789
  740, 1.749383
}
  
#local S_ABS_Sapphire = spline { linear_spline
  380, 1.020000
  400, 0.280000
  420, 0.110000
  440, 0.270000
  460, 0.400000
  480, 0.220000
  500, 0.310000
  520, 0.490000
  540, 0.650000
  560, 0.770000
  580, 0.820000
  600, 0.840000
  620, 0.840000
  640, 0.860000
  660, 0.900000
  680, 0.970000
  700, 1.070000
  720, 1.190000
  740, 1.400000
}


//Topaz  rgb <0.9485, 0.9679, 0.9730>

#local S_IOR_Topaz = spline { linear_spline
  380, 1.641430
  400, 1.640336
  420, 1.639242
  440, 1.638148
  460, 1.637055
  480, 1.635961
  500, 1.634867
  520, 1.633773
  540, 1.632680
  560, 1.631586
  580, 1.630492
  600, 1.629398
  620, 1.628305
  640, 1.627211
  660, 1.626117
  680, 1.625023
  700, 1.623930
  720, 1.622836
  740, 1.621742
}  

#local S_ABS_Topaz = spline { linear_spline
  380, 0.057000
  400, 0.038000
  420, 0.032000
  440, 0.029000
  460, 0.027000
  480, 0.026000
  500, 0.026000
  520, 0.027000
  540, 0.031000
  560, 0.036000
  580, 0.042000
  600, 0.046000
  620, 0.047000
  640, 0.045000
  660, 0.039000
  680, 0.033000
  700, 0.028000
  720, 0.024000
  740, 0.021000
}



// Diamond

#local S_IOR_Diamond = spline { linear_spline
  380, 2.475000
  400, 2.465000
  420, 2.455000
  440, 2.448000
  460, 2.443000
  480, 2.438000
  500, 2.433000
  520, 2.428000
  540, 2.425000
  560, 2.422000
  580, 2.419000
  600, 2.416000
  620, 2.413000
  640, 2.410000
  660, 2.407000
  680, 2.404000
  700, 2.401000
  720, 2.398000
  740, 2.395000
}  


// Diamond Natural Yellow - rgb <0.9856, 0.9924, 0.8726>

#local S_ABS_Diamond_NaturalYellow = spline { linear_spline
  380, 0.907000
  400, 0.904200
  420, 0.321200
  440, 0.098000
  460, 0.078000
  480, 0.068200
  500, 0.024200
  520, 0.013200
  540, 0.013200
  560, 0.013200
  580, 0.013200
  600, 0.013200
  620, 0.013200
  640, 0.013200
  660, 0.002200
  680, 0.002200
  700, 0.002200
  720, 0.002200
  740, 0.002200
}  


// Diamond Fancy Yellow   - rgb <1.0035, 0.9922, 0.8737>

#local S_ABS_Diamond_FancyYellow = spline { linear_spline
  380, 0.530415
  400, 0.346351
  420, 0.227044
  440, 0.138168
  460, 0.091619
  480, 0.059224
  500, 0.036794
  520, 0.019333
  540, 0.010107
  560, 0.008617
  580, 0.006091
  600, 0.001052
  620,-0.002963
  640, 0.001707
  660, 0.005392
  680, 0.005897
  700, 0.008932
  720, 0.013364
  740, 0.012528
}


// Diamond Lemon Yellow   - rgb <0.9909, 0.9697, 0.8961>

#local S_ABS_Diamond_LemonYellow = spline { linear_spline
  380, 0.157956
  400, 0.128985
  420, 0.083160
  440, 0.082684
  460, 0.104507
  480, 0.109775
  500, 0.080905
  520, 0.023000
  540, 0.024000
  560, 0.026000
  580, 0.024209
  600, 0.020125
  620, 0.018200
  640, 0.018200
  660, 0.018200
  680, 0.018133
  700, 0.018200
  720, 0.018200
  740, 0.018200
}       


//================================================================================

#if (SpectralMode)

//================================================================================
// SPECTRAL MODE
//================================================================================


#declare IOR_Amethyst = array[36];
#declare D_Amethyst   = array[36];

#declare IOR_Sapphire = array[36];
#declare D_Sapphire   = array[36];

#declare IOR_Emerald  = array[36];
#declare D_Emerald    = array[36];

#declare IOR_Ruby     = array[36];
#declare D_Ruby       = array[36];

#declare IOR_Topaz    = array[36];
#declare D_Topaz      = array[36];

#declare IOR_Diamond  = array[36];
#declare D_Diamond_NaturalYellow = array[36];
#declare D_Diamond_FancyYellow   = array[36];
#declare D_Diamond_LemonYellow   = array[36];



#for (I, 0, 35)                 
  #local W = 380 + I*10;

  #declare IOR_Amethyst[I] = S_IOR_Amethyst(W).y;
  #declare D_Amethyst[I]   = 1-S_ABS_Amethyst(W).y;

  #declare IOR_Emerald[I]  = S_IOR_Emerald(W).y;
  #declare D_Emerald[I]    = 1-S_ABS_Emerald(W).y;

  #declare IOR_Ruby[I]     = S_IOR_Ruby(W).y;
  #declare D_Ruby[I]       = 1-S_ABS_Ruby(W).y;

  #declare IOR_Sapphire[I] = S_IOR_Sapphire(W).y;
  #declare D_Sapphire[I]   = 1-S_ABS_Sapphire(W).y;

  #declare IOR_Topaz[I]    = S_IOR_Topaz(W).y;
  #declare D_Topaz[I]      = 1-S_ABS_Topaz(W).y;


  #declare IOR_Diamond[I]  = S_IOR_Diamond(W).y;
  #declare D_Diamond_NaturalYellow[I] = 1-S_ABS_Diamond_NaturalYellow(W).y;
  #declare D_Diamond_FancyYellow[I]   = 1-S_ABS_Diamond_FancyYellow(W).y;
  #declare D_Diamond_LemonYellow[I]   = 1-S_ABS_Diamond_LemonYellow(W).y;

#end



//================================================================================

#else // !SpectralMode

//================================================================================
// PREVIEW MODE
//================================================================================
         

#macro SpectralInverse(D)
  spline { linear_spline
 #for (I, 380, 760, 10)
    I, 1 - D(I).y
 #end 
  }
#end

#local W = WavelengthFromIndex(WavelengthIndex);
                        

#declare IOR_Amethyst = S_IOR_Amethyst(W).y;
#declare D_Amethyst   = Reflective2RGB(SpectralInverse(S_ABS_Amethyst));

#declare IOR_Emerald  = S_IOR_Emerald(W).y;
#declare D_Emerald    = Reflective2RGB(SpectralInverse(S_ABS_Emerald));   

#declare IOR_Ruby     = S_IOR_Ruby(W).y;
#declare D_Ruby       = Reflective2RGB(SpectralInverse(S_ABS_Ruby));   

#declare IOR_Sapphire = S_IOR_Sapphire(W).y;
#declare D_Sapphire   = Reflective2RGB(SpectralInverse(S_ABS_Sapphire));

#declare IOR_Topaz    = S_IOR_Topaz(W).y;
#declare D_Topaz      = Reflective2RGB(SpectralInverse(S_ABS_Topaz));   


#declare IOR_Diamond  = S_IOR_Diamond(W).y;
#declare D_Diamond_NaturalYellow = Reflective2RGB(SpectralInverse(S_ABS_Diamond_NaturalYellow));   
#declare D_Diamond_FancyYellow   = Reflective2RGB(SpectralInverse(S_ABS_Diamond_FancyYellow));   
#declare D_Diamond_LemonYellow   = Reflective2RGB(SpectralInverse(S_ABS_Diamond_LemonYellow));   



//================================================================================ 

#end  // !SpectralMode

//================================================================================ 

#macro M_Gem (D, IOR, FD)
  material {                                                           
    texture {
      P_Spectral_Filter (D, Value_1)  
      finish {
        ambient 0  emission 0  diffuse 0
        reflection {0 1 fresnel on} conserve_energy
      }  
    }     
    interior {
      IOR_Spectral(IOR)
      fade_power 1001
      fade_distance FD
      FadeColor_Spectral (D) 
    }
  }
#end       


//================================================================================ 
//
// HIGH LEVEL INTERFACE
// ********************
//
//================================================================================ 

#macro M_Amethyst(FadeDist) 
  M_Gem (D_Amethyst, IOR_Amethyst, FadeDist)
#end

#macro M_Emerald(FadeDist) 
  M_Gem (D_Emerald, IOR_Emerald, FadeDist)
#end

#macro M_Ruby(FadeDist) 
  M_Gem (D_Ruby, IOR_Ruby, FadeDist)
#end

#macro M_Sapphire(FadeDist) 
  M_Gem (D_Sapphire, IOR_Sapphire, FadeDist)
#end

#macro M_Topaz(FadeDist) 
  M_Gem (D_Topaz, IOR_Topaz, FadeDist)
#end

#macro M_Diamond_NaturalYellow(FadeDist) 
  M_Gem (D_Diamond_NaturalYellow, IOR_Diamond, FadeDist)
#end

#macro M_Diamond_FancyYellow(FadeDist) 
  M_Gem (D_Diamond_FancyYellow, IOR_Diamond, FadeDist)
#end

#macro M_Diamond_LemonYellow(FadeDist) 
  M_Gem (D_Diamond_LemonYellow, IOR_Diamond, FadeDist)
#end


//================================================================================

#end  // #ifndef (spectral_minerals_inc)



/*

// some debugging, only used during development 

#macro DebugColor(What, C)
  #debug "\n" #debug What #debug " - rgb <"
  #debug str(C.red,  1,4) #debug ", "
  #debug str(C.green,1,4) #debug ", "
  #debug str(C.blue, 1,4) #debug ">\n"
#end


DebugColor("Amethyst", rgb D_Amethyst)
DebugColor("Emerald ", rgb D_Emerald)
DebugColor("Ruby    ", rgb D_Ruby)
DebugColor("Sapphire", rgb D_Sapphire)
DebugColor("Topaz   ", rgb D_Topaz)  
DebugColor("Diamond Natural Yellow", D_Diamond_NaturalYellow)
DebugColor("Diamond Fancy Yellow  ", D_Diamond_FancyYellow)
DebugColor("Diamond Lemon Yellow  ", D_Diamond_LemonYellow)

*/