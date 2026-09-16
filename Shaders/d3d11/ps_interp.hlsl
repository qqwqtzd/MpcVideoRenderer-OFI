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
	int2 coord = int2(float2(px) * gInvGrid);
	coord = clamp(coord, int2(0, 0), int2(gSize) - 1);
	return float2(texFlow.Load(int3(coord, 0))) / 32.0; // S10.5
}

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
	// DIAGNOSTIC 2: sample the current frame with the raw interpolator UV - no
	// constant-buffer math, no flow. If this still comes out white, the pass is
	// not drawing or the texture binding is wrong. If it shows the video, the
	// bug is the constant-buffer math (e.g. gSize == 0 -> division by zero).
	return texCur.SampleLevel(samp, uv, 0);
}
