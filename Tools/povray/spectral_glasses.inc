//================================================================================
// POV-Ray spectral render materials V0.21
//
// Glasses
// *******
//
// Ive, April 2014
//
//================================================================================

#ifndef (spectral_glasses_inc) #declare spectral_glasses_inc = 1;  

//================================================================================


#declare Schott_TiK1     =  1;
#declare Schott_FK5      =  2;
#declare Schott_TiF1     =  3;
#declare Schott_BK7      =  4;
#declare Schott_PK2      =  5;
#declare Schott_K5       =  6;
#declare Schott_KF9      =  7;
#declare Schott_BaLK1    =  8;
#declare Schott_KzF6     =  9;
#declare Schott_ZK1      = 10;
#declare Schott_LLF1     = 11;
#declare Schott_Ultran30 = 12;
#declare Schott_PSK3     = 13;
#declare Schott_BaK1     = 14;
#declare Schott_BaLF4    = 15;
#declare Schott_LF5      = 16;
#declare Schott_LgSK2    = 17;
#declare Schott_FF5      = 18;
#declare Schott_SK4      = 19;
#declare Schott_KzFSN4   = 20;
#declare Schott_SSK4     = 21;
#declare Schott_F2       = 22;
#declare Schott_BaSF10   = 23;
#declare Schott_BaF10    = 24;
#declare Schott_LaK10    = 25;
#declare Schott_TaC2     = 26;
#declare Schott_NbF1     = 27;
#declare Schott_LaF2     = 28;
#declare Schott_SF6      = 29;
#declare Schott_TaFD5    = 30;
#declare Schott_HTF1     = 31;


#declare Schott_Param = array[31][6] {
  { 2.157397800000E+00, -8.400418900000E-03, 1.045758200000E-02,  2.182259300000E-04, -5.506364000000E-06,  5.446906000000E-07 },
  { 2.188762100000E+00, -9.557200700000E-03, 8.991523200000E-03,  1.456051600000E-04, -5.284306700000E-06,  3.458801000000E-07 },
  { 2.247312400000E+00, -8.904405800000E-03, 1.249352500000E-02,  4.265063800000E-04, -2.156480900000E-06,  2.636406500000E-06 },
  { 2.271892900000E+00, -1.010807700000E-02, 1.059250900000E-02,  2.081696500000E-04, -7.647253800000E-06,  4.924099100000E-07 },
  { 2.277053300000E+00, -1.053201000000E-02, 1.018835400000E-02,  2.900156400000E-04, -1.960285600000E-05,  1.096771800000E-06 },
  { 2.285029900000E+00, -8.601072500000E-03, 1.180678300000E-02,  2.076565700000E-04, -2.131491300000E-06,  3.213123400000E-07 },
  { 2.282439600000E+00, -8.596014400000E-03, 1.344264500000E-02,  2.780353500000E-04, -4.999896000000E-07,  7.710591100000E-07 },
  { 2.296692300000E+00, -8.297554900000E-03, 1.190723400000E-02,  1.990830500000E-04, -2.030683800000E-06,  3.142970300000E-07 },
  { 2.293404400000E+00, -1.034612200000E-02, 1.331986300000E-02,  3.483322600000E-04, -9.935409000000E-06,  1.122790500000E-06 },
  { 2.315795100000E+00, -8.749390500000E-03, 1.232964500000E-02,  2.631111200000E-04, -8.285420100000E-06,  7.373580100000E-07 },
  { 2.350516200000E+00, -8.530645100000E-03, 1.575085300000E-02,  4.281138800000E-04, -6.987571800000E-06,  1.717551700000E-06 },
  { 2.367794200000E+00, -6.081890600000E-03, 1.050956800000E-02,  1.310557500000E-04, -4.985438000000E-07,  1.047365200000E-07 },
  { 2.376819300000E+00, -1.014651400000E-02, 1.216714800000E-02,  1.191660600000E-04,  6.425062700000E-06, -1.747870600000E-07 },
  { 2.433300700000E+00, -8.493135300000E-03, 1.389351200000E-02,  2.679826800000E-04, -6.194610100000E-06,  6.220900500000E-07 },
  { 2.452836600000E+00, -9.204767800000E-03, 1.455279400000E-02,  4.304668800000E-04, -2.048983600000E-05,  1.592441500000E-06 },
  { 2.444176000000E+00, -8.305969500000E-03, 1.900069700000E-02,  5.412969700000E-04, -4.197315500000E-06,  2.374289700000E-06 },
  { 2.475076000000E+00, -5.430452800000E-03, 1.389321000000E-02,  2.299056000000E-04, -1.686847400000E-06,  4.395970300000E-07 },
  { 2.474332400000E+00, -1.095533800000E-02, 1.929380100000E-02,  1.449773200000E-03, -1.103874400000E-04,  1.113600800000E-05 },
  { 2.558522800000E+00, -9.882495100000E-03, 1.515182000000E-02,  2.113447800000E-04, -3.413013000000E-06,  1.267335500000E-07 },
  { 2.529344600000E+00, -1.323458600000E-02, 1.858616500000E-02,  5.475965500000E-04, -1.171798700000E-05,  2.004290500000E-06 },
  { 2.570784900000E+00, -9.257776400000E-03, 1.617075100000E-02,  8.692497200000E-04, -2.401170400000E-05,  4.536516900000E-06 },
  { 2.555406300000E+00, -8.874615000000E-03, 2.249478700000E-02,  8.692497200000E-04, -2.400117040000E-05,  4.536516900000E-06 },
  { 2.653125000000E+00, -8.138855300000E-03, 2.299564300000E-02,  7.353595700000E-04, -1.340739000000E-05,  3.696232500000E-06 },
  { 2.732246210000E+00, -1.249046000000E-02, 1.856233400000E-02,  9.999053600000E-04, -6.838855200000E-05,  4.925793100000E-06 },
  { 2.898461400000E+00, -1.485703900000E-02, 2.098503700000E-02,  5.450692100000E-04, -1.729731400000E-05,  1.799360100000E-06 },
  { 2.971713700000E+00, -1.495259300000E-02, 2.016286800000E-02,  9.407228300000E-04, -8.861410400000E-05,  5.319124200000E-06 },
  { 2.975349100000E+00, -1.461347000000E-02, 2.109638300000E-02,  1.198038000000E-03, -1.188738800000E-04,  7.344435000000E-06 },
  { 2.967378700000E+00, -1.097876700000E-02, 2.508860700000E-02,  6.317159600000E-04, -7.564541700000E-06,  2.320221300000E-06 },
  { 3.119500700000E+00, -1.090258000000E-02, 4.133065100000E-02,  3.180021400000E-03, -2.195318400000E-04,  2.667101400000E-05 },
  { 3.272909800000E+00, -1.288825700000E-02, 3.345136300000E-02,  6.822138100000E-05,  1.121542700000E-04, -4.048565900000E-06 },
  { 2.063303400000E+00, -3.290634500000E-03, 7.176516000000E-03, -1.911055900000E-04,  3.812344100000E-05, -2.066850100000E-06 }
}


#macro ParamIOR_Glass_Schott(K,W)
  sqrt( Schott_Param[K][0] + 
        Schott_Param[K][1] * pow(W,  2 ) +
        Schott_Param[K][2] * pow(W, -2 ) +
        Schott_Param[K][3] * pow(W, -4 ) +
        Schott_Param[K][4] * pow(W, -6 ) +
        Schott_Param[K][5] * pow(W, -8 )
      )
#end     


// optical glass N-BAF10 (Schott)
// IOR (550nm) = 1.67306   input: W^2
#declare ParamIOR_Glass_BAF10 = function(x) {
  sqrt( 1 + 1.5851495*x/(x-0.00926681282)  + 0.143559385*x/(x-0.0424489805) + 1.08521269*x/(x-105.613573) )
}

// optical glass N-BAK1 (Schott)
// IOR (550nm) = 1.57462   input: W^2
#declare ParamIOR_Glass_BAK1 = function(x) {
  sqrt( 1 + 1.12365662*x/(x-0.00644742752) + 0.309276848*x/(x-0.0222284402) + 0.881511957*x/(x-107.297751) )
}

// optical glass N-BK7 (Schott)
// IOR (550nm) = 1.51852   input: W^2
#declare ParamIOR_Glass_BK7 = function(x) {
  sqrt( 1 + 1.03961212*x/(x-0.00600069867) + 0.231792344*x/(x-0.0200179144) + 1.01046945*x/(x-103.560653) )
}       

// flint glass F2 (Schott)
// IOR (550nm) = 1.62366  input: W^2
#declare ParamIOR_FlintGlass_F2 = function(x) {
  sqrt( 1 + 1.34533359*x/(x-0.00997743871) + 0.209073176*x/(x-0.0470450767) + 0.937357162*x/(x-111.886764) )
} 

// flint glass F5 (Schott)
// IOR (550nm) = 1.60679  input: W^2
#declare ParamIOR_FlintGlass_F5 = function(x) {
  sqrt( 1 + 1.3104463 *x/(x-0.00958633048) + 0.19603426 *x/(x-0.0457627627) + 0.96612977*x/(x-115.011883) )
} 

// dense flint glass N-SF2 (Schott)
// IOR (550nm) = 1.65176  input: W^2
#declare ParamIOR_FlintGlass_SF2 = function(x) {
  sqrt( 1 + 1.47343127*x/(x-0.0109019098) + 0.163681849*x/(x-0.0585683687) + 1.36920899*x/(x-127.404933) )
} 

// crown glass K7 (Schott)
// IOR (550nm) = 1.51293   input: W^2
#declare ParamIOR_CrownGlass_K7 = function(x) {
  sqrt( 1 + 1.1273555*x/(x-0.00720341707) + 0.124412303*x/(x-0.0269835916) + 0.827100531*x/(x-100.384588) )
}

// crown glass K10 (Schott)
// IOR (550nm) = 1.50327   input: W^2
#declare ParamIOR_CrownGlass_K10 = function(x) {
  sqrt( 1 + 1.15687082*x/(x-0.00809424251) + 0.0642625444*x/(x-0.0386051284) + 0.872376139*x/(x-104.74773) )
}

// dense crown glass SK5 (Schott)
// IOR (550nm) = 1.59119  input: W^2     
#declare ParamIOR_CrownGlass_SK5 = function(x) {
  sqrt( 1 + 0.991463823*x/(x-0.00522730467) + 0.495982121*x/(x-0.0172733646) + 0.987393925*x/(x-98.3594579) )
}            


// PMMA acrylic glass
// IOR (550nm) = 1.49363  input: W
#declare ParamIOR_Acryl = function(x) {
  sqrt( 2.399964 - 8.308636e-2*pow(x,2) - 1.919569e-1*pow(x,-2) + 8.720608e-2*pow(x,-4) - 1.666411e-2*pow(x,-6) + 1.169519e-3*pow(x,-8) )
}



#if (SpectralMode)

//================================================================================
// SPECTRAL MODE
//================================================================================

// glasses - taken from Schott (a German glass manufacturer) data sheets
// optical glass
#declare IOR_Glass_BAF10     = array[36];
#declare IOR_Glass_BAK1      = array[36];
#declare IOR_Glass_BK7       = array[36];
// flint glass
#declare IOR_FlintGlass_F2   = array[36];
#declare IOR_FlintGlass_F5   = array[36];
#declare IOR_FlintGlass_SF2  = array[36];
// crown glass
#declare IOR_CrownGlass_K7   = array[36];
#declare IOR_CrownGlass_K10  = array[36];
#declare IOR_CrownGlass_SK5  = array[36];

#declare IOR_Acryl           = array[36]; 


// fill the arrays 
#for (I, 0, 35)
  #local W  = 0.38 + I*0.01;
  #local W2 = pow(W,2);      
  
  #declare IOR_Water[I]           = ParamIOR_Water(W2);

  #declare IOR_Glass_BAF10[I]     = ParamIOR_Glass_BAF10(W2);
  #declare IOR_Glass_BAK1[I]      = ParamIOR_Glass_BAK1(W2);
  #declare IOR_Glass_BK7[I]       = ParamIOR_Glass_BK7(W2);
  #declare IOR_FlintGlass_F2[I]   = ParamIOR_FlintGlass_F2(W2);
  #declare IOR_FlintGlass_F5[I]   = ParamIOR_FlintGlass_F5(W2);
  #declare IOR_FlintGlass_SF2[I]  = ParamIOR_FlintGlass_SF2(W2);
  #declare IOR_CrownGlass_K7[I]   = ParamIOR_CrownGlass_K7(W2);
  #declare IOR_CrownGlass_K10[I]  = ParamIOR_CrownGlass_K10(W2);
  #declare IOR_CrownGlass_SK5[I]  = ParamIOR_CrownGlass_SK5(W2);

  #declare IOR_Acryl[I]           = ParamIOR_Acryl(W);
#end 


//================================================================================

#else // !SpectralMode

//================================================================================
// PREVIEW MODE
//================================================================================

#local W  = WavelengthFromIndex(WavelengthIndex) / 1000;
#local W2 = pow(W,2);

#declare IOR_Glass_BAF10     = ParamIOR_Glass_BAF10(W2);
#declare IOR_Glass_BAK1      = ParamIOR_Glass_BAK1(W2);
#declare IOR_Glass_BK7       = ParamIOR_Glass_BK7(W2);
#declare IOR_FlintGlass_F2   = ParamIOR_FlintGlass_F2(W2);
#declare IOR_FlintGlass_F5   = ParamIOR_FlintGlass_F5(W2);
#declare IOR_FlintGlass_SF2  = ParamIOR_FlintGlass_SF2(W2);
#declare IOR_CrownGlass_K7   = ParamIOR_CrownGlass_K7(W2);
#declare IOR_CrownGlass_K10  = ParamIOR_CrownGlass_K10(W2);
#declare IOR_CrownGlass_SK5  = ParamIOR_CrownGlass_SK5(W2);

#declare IOR_Acryl           = ParamIOR_Acryl(W);


#end // !SpectralMode


//================================================================================


#macro IOR_SpectralParam(M)
  ior ParamIOR_Glass_Schott( M-1, WavelengthFromIndex(WavelengthIndex) / 1000)
#end


#macro M_Glass_Schott (M, D, FadeDist)
  material {
    texture {
      P_Spectral_Filter (D, Value_1)
      finish {
        ambient 0  emission 0  diffuse 0
        reflection {0 1 fresnel on} conserve_energy
      }
    }
    interior {
      IOR_SpectralParam(M)
      fade_power 1001
      fade_distance FadeDist
      FadeColor_Spectral (D)
    }
  }
#end


#end  // #ifndef (spectral_glasses_inc)