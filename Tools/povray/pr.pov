/////////////////////////////////////////////
//
//     ~~ [ Strange Crystal ] ~~
//        version 4 out of 4
//
//  by Michael Scharrer
//  https://mscharrer.net
//
// CC-BY-4.0 license
// https://creativecommons.org/licenses/by/4.0/
//
/////////////////////////////////////////////

#version 3.7;
#include "spectral.inc"

#declare s = seed(44);
#declare box_count = 40;

global_settings {
	assumed_gamma 1
	max_trace_level 16
	adc_bailout 0.006
	photons {
		count 10000000
		autostop 0
		jitter .4
		max_trace_level 7
		adc_bailout 0.02
	}
}

background {
	rgb 0.03
}

camera {
	right x*image_width/image_height
	location <3,5,-2>
	look_at <-2, 1, 0>
}

#macro sublight(col, pos)
	light_source {
		<-5.7,4.5,11.5> + pos
		<1,.9,.7> * col
		area_light <0.83, 0, 0>, <0, 0, 0.83>, 2, 2
		adaptive 0.01
		jitter
	}
#end

//reddish light
sublight(<.50, .25, .25>,  0.11*y)

//bluish light
sublight(<.25, .50, .25>,  0.00*y)

//greenish light
sublight(<.25, .25, .50>, -0.11*y)


//whole crystal
#declare crystal_base = merge {
	#declare i = 0;
	#while (i < box_count)
		box {
			-1
			1
			
			rotate 360*<rand(s), rand(s), rand(s)>
		}
		#declare i = i + 1;
	#end
}

//the cut through the crystal
#declare crystal_separator = blob {
	sphere { <-2,0,0> 4 2.9 }
	cylinder { <1,-4,0> <1,4,0> 1.5 (-2.2) }
	sphere { <.2,.7,-.8> 0.5 (-1) }
	scale 1.3
}

//the actual crystal
difference {
	union {

		  //left piece
		intersection {
			object { crystal_base }
			object { crystal_separator }
			
			rotate 10*z
			translate -.25*x
		}

		  sphere {<0.5,-0,-6.5>, 1
		  M_Spectral_Filter( D_Average(D_CC_A4, 1, Value_1, 1), IOR_CrownGlass_SK5,15)
		  }

		//right piece
		difference {

		union {
		      object { crystal_base }
		      box { <-0.7,-0.8,-1.5>,<-0.5,-0.2,-1.4>
		      		 rotate <17,-56,43>
				 texture { pigment { colour <255,0,0,0,0> }}
		          }
		}

               object { crystal_separator }
			
			rotate -10*z
			translate .25*x
		}
		
		//small shard
		intersection {
			object { crystal_base }
			sphere { 0 3 translate -4*x }
			
			rotate -90*z
			rotate 90*y
			translate -3.0*y
			scale 0.5
			translate <-3.5, 0, 1>
		}
		
		rotate -30*y
		translate <-3.5,1,5.5>

	}
	
	//cut off at the bottom
	plane {
		y
		0.01
	}
	
	pigment {
		rgbt <0.5, 1.0, 0.5, 0.9>
	}
	finish {
		ambient 0
		diffuse 0
		reflection <0.65, 0.55, 0.48>
	}
	interior {
		ior 1.8
	}
	photons{
		target
		reflection on
		refraction on
		collect off
	}
}

//floor
plane {
	y
	0
	
	hollow
	no_reflection
	
	#declare inner_pigm = pigment {
		bozo
		color_map {
			[0 rgb 0.6]
			[1 rgb 1.0]
		}
		scale 0.2
	}
	
	pigment {
		crackle
		pigment_map {
			[0.000 rgb .2]
			[0.015 inner_pigm]
		}
		turbulence 0.1
		scale 0.06
	}
	finish {
		ambient <0.032,0.032,0.038>
	}
	photons {
		collect on
	}
}

//fake floor reflector
plane {
	y
	(-1)
	
	hollow
	no_image
	no_shadow
	
	pigment {
		rgb 0.2
	}
	finish {
		diffuse 0
		ambient 1
		reflection 0.2
	}
	photons{
		collect off
	}
}

