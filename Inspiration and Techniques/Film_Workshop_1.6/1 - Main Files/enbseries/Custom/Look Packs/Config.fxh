//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
// CUSTOM LOOK PACK CONFIGURATION FILE                                     //
//                                                                         //
// DO NOT MODIFY THIS FILE UNLESS YOU KNOW WHAT YOU'RE DOING               //
// AND IF YOU DO MODIFY IT AND BREAK SOMETHING, I WILL FIND YOU            //
// AND LAUGH IN YOUR FACE FOR IGNORING THIS WARNING                        //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
// UI SETTINGS AND PNG PATH (INPUT THESE VALUES BASED ON LOOK PACK VALUES) //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
#define PACK_SELECTION "      TRU3TH"            // UI String
#define LOOK_PACK_NAME "TRU3TH"                       // ID for 3DLUT shader
#define LOOK_PACK_LUT "Custom/Look Packs/tru3th.png"  // Path to LUT

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
// LUT SIZE INITIALIZATION (INPUT THESE VALUES BASED ON LOOK PACK VALUES)  //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
#define LP_COUNT 10.0      // Number of looks
#define LP_LUTVSIZE 32.0  // LUT block size

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
// AUTOMATED LUT SIZE CALCULATIONS (NO NEED TO MODIFY THIS)                //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
#define LP_LUTHSIZE (LP_LUTVSIZE * LP_LUTVSIZE)
#define LP_LUTHEIGHT (1.0 / LP_LUTVSIZE)
#define LP_LUTWIDTH (1.0 / LP_LUTHSIZE)
#define LP_VCOUNTPIXEL (1.0 / LP_COUNT)
