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
	float2 size = float2(max(gSize.x, 1u), max(gSize.y, 1u));
	float2 invSize = 1.0 / size;
	float2 uvPixel = uv * size;

	// The previous frame is sampled against the flow, the current frame along
	// it (the OFA forward flow goes from the previous to the current frame).
	float2 flow = LoadFlow(int2(uvPixel));
	float2 uvPrev = saturate((uvPixel - flow * gAlpha) * invSize);
	float2 uvCur = saturate((uvPixel + flow * (1.0 - gAlpha)) * invSize);

	float3 a = texPrev.SampleLevel(samp, uvPrev, 0).rgb;
	float3 b = texCur.SampleLevel(samp, uvCur, 0).rgb;

	return float4(lerp(a, b, gAlpha), 1.0);
}
