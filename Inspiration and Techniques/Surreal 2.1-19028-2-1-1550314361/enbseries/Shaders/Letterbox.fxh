// Crop Assistant by Wolrajh 

#define Hratio 1
#define cropOffset 0

float4 PS_wolCropPreview(float4 position : SV_Position, VS_OUTPUT IN) : SV_Target
{
	float4 res;
	float fCropHeight = 0.0;	// defaulting
	float fCropOffsetH = 0.0;	// defaulting
	float fCropWidth = 0.0;		// defaulting	float fCropHeight = 0.0;	// defaulting
	float fCropOffsetW = 0.0;		// defaulting
	if (ENABLE_CROPPREVIEW==true)	// Is the Crop Assistant even enabled ?
	{
		if (Wratio > Hratio && (Wratio/Hratio) > ScreenSize.z)								// Basically, detects if portrait or landscape, or if input aspect ratio leads to something narrower than current output aspect ratio
		{
			fCropOffsetH = cropOffset;
			fCropHeight=(1-(ScreenSize.z/Wratio)*Hratio)*0.5;								// Landscape
		} else {
			 fCropOffsetW = cropOffset;
			fCropWidth=((ScreenSize.z-(Wratio/Hratio))*0.5)/ScreenSize.z;			// Portrait
		}
	
		if (IN.txcoord.y > 1.0f - fCropHeight + fCropOffsetH || IN.txcoord.y  < fCropHeight  + fCropOffsetH|| IN.txcoord.x > 1.0f - fCropWidth + fCropOffsetW || IN.txcoord.x  < fCropWidth  + fCropOffsetW ) 		// Detects if pixel in border
		{
			res = float4(0.0f, 0.0f, 0.0f, 0.0f);																																											// Turns pixel Black...
		}	else {
			res.xyz = TextureColor.Sample(LinearSampler, IN.txcoord.xy).rgb;																																// ... Or keeps it identical.
		}
		
	} else { 										// If Crop Assistant not enabled, keeps the pixel the way it was.
		res.xyz = TextureColor.Sample(LinearSampler, IN.txcoord.xy).rgb;
	}
	
	res.w = 1.0;
	return res;
}