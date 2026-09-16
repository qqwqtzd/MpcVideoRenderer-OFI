// RGB -> luma extraction, feeding the Optical Flow Accelerator.
// ps_4_0, full-screen quad. Output is R8 via the render target.

Texture2D    texSrc : register(t0);
SamplerState samp   : register(s0);

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
	float3 rgb = texSrc.SampleLevel(samp, uv, 0).rgb;
	// Rec.709 luma, using the non-linear RGB directly: the OFA only needs a
	// perceptually consistent grayscale signal, not a linear-light value.
	// saturate() matters for HDR/linear internal formats where the dot product
	// can exceed 1.0 and would otherwise clamp the whole plane to pure white.
	float y = saturate(dot(rgb, float3(0.2126, 0.7152, 0.0722)));
	return float4(y, y, y, 1.0);
}
