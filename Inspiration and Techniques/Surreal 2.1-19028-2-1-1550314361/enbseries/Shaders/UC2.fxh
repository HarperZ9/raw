/*
UI_FLOAT_DNI(A,                         "Shoulder Strength",        0.0, 2.0,  0.2)
UI_FLOAT_DNI(B,                         "Linear Strength",          0.0, 5.0,  0.3)
UI_FLOAT_FINE_DNI(C,                    "Linear Angle",             0.0, 1.0,  0.1, 0.001)
UI_FLOAT_DNI(D,                         "Toe Strength",             0.0, 2.0,  0.2)
UI_FLOAT_FINE_DNI(E,                    "Toe Numerator",            0.0, 0.5,  0.1, 0.001)
UI_FLOAT_FINE_DNI(F,                    "Toe Denominator",          0.0, 2.0,  0.3, 0.001)
UI_FLOAT_DNI(LinearWhite,               "Linear White",             0.0, 20.0, 10.0)
*/

// Function used by the Uncharte2D tone mapping curve
float3 U2Func(float3 x)
{
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F)) - E/F;
}

// Applies the Uncharted 2 filmic tone mapping curve
float3 ToneMapOperator(float3 color) 
{
    float3 numerator = U2Func(color);        
    float3 denominator = U2Func(LinearWhite);
    return numerator / denominator;
}