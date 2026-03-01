float3 Lin2Log(float3 LinearColor) {
    float3 LogColor = ( 300 * log10( LinearColor * (1 - .0108) + .0108 ) + 685 ) / 1023;    // Cineon
    LogColor = saturate( LogColor );

    return LogColor;
}

//cineon [0, 1] -> [0.0108, 13.51]
float3 Log2Lin(float3 LogColor) {
    float3 LinearColor = ( pow( 10, ( 1023 * LogColor - 685 ) / 300) - .0108 ) / (1 - .0108);    // Cineon

    return LinearColor;
}
