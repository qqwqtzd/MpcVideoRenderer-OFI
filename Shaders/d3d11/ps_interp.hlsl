// Warp + blend two neighbouring RGB frames along the optical flow into an
// in-between frame. ps_4_0, full-screen quad.
//
// t0 = previous RGB frame
// t1 = current  RGB frame
// t2 = optical flow (R16G16_SINT, S10.5 fixed point)

Texture2D    texPrev  : register(t0);
Texture2D    texCur   : register(t1);
Texture2D<int2> texFlow : register(t2);
SamplerState samp     : register(s0);

cbuffer InterpConstants : register(b0)
{
	float gAlpha;    // 0 = previous frame, 1 = current frame
	float gInvGrid;  // 1.0 / OFA output grid size
	uint2 gSize;     // frame width/height
};

float2 LoadFlow(int2 px)
{
	int2 lim = int2(max(gSize.x, 1u), max(gSize.y, 1u)) - 1;
	int2 coord = clamp(int2(float2(px) * gInvGrid), int2(0, 0), lim);
	return float2(texFlow.Load(int3(coord, 0))) / 32.0; // S10.5
}

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
	// DIAGNOSTIC 6: blend the two input frames with a hard-coded 0.5, using
	// nothing but the two SRVs - no flow, no gSize, no constant buffer.
	return float4(lerp(texPrev.SampleLevel(samp, uv, 0).rgb,
	                   texCur.SampleLevel(samp, uv, 0).rgb, 0.5), 1.0);
}
