float3 LinToLog( float3 LinearColor )
{
    float3 LogColor = ( 300 * log10( LinearColor * (1 - .0108) + .0108 ) + 685 ) / 1023;    // Cineon
    LogColor = saturate( LogColor );

    return LogColor;
}
