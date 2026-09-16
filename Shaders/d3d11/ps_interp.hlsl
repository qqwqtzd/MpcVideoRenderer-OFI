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
	float2 invSize = 1.0 / float2(gSize);
	float2 uvPixel = uv * float2(gSize);
	// DIAGNOSTIC BUILD: force zero flow to separate the warp from the sampling.
	// If the frame is still white with this, the bug is in the pass itself, not
	// in the optical flow.
	float2 flow = float2(0, 0);
	//float2 flow = LoadFlow(int2(uvPixel));

	// The OFA forward flow is the displacement from the input frame (previous)
	// to the reference frame (current). To reconstruct the in-between time we
	// sample the previous frame against the flow by alpha, and the current
	// frame along the flow by (1 - alpha).
	float2 uvPrev = (uvPixel - flow * gAlpha) * invSize;
	float2 uvCur = (uvPixel + flow * (1.0 - gAlpha)) * invSize;

	float3 a = texPrev.SampleLevel(samp, uvPrev, 0).rgb;
	float3 b = texCur.SampleLevel(samp, uvCur, 0).rgb;

	return float4(lerp(a, b, gAlpha), 1.0);
}
