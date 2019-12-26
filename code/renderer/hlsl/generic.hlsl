/*
===========================================================================
Copyright (C) 2019 Gian 'myT' Schellenbaum

This file is part of Challenge Quake 3 (CNQ3).

Challenge Quake 3 is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Challenge Quake 3 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Challenge Quake 3. If not, see <https://www.gnu.org/licenses/>.
===========================================================================
*/
// generic vertex and pixel shaders

cbuffer VertexShaderBuffer
{
    matrix modelViewMatrix;
    matrix projectionMatrix;
	float4 clipPlane;
};

struct VIn
{
	float4 position : POSITION;
	float4 color : COLOR0;
	float2 texCoords : TEXCOORD0;
	float2 texCoords2 : TEXCOORD1;
};

struct VOut
{
	float4 position : SV_Position;
	centroid float4 color : COLOR0;
	centroid float2 texCoords : TEXCOORD0;
	centroid float2 texCoords2 : TEXCOORD1;
	float clipDist : SV_ClipDistance0;
};

VOut vs_main(VIn input)
{
	float4 positionVS = mul(modelViewMatrix, float4(input.position.xyz, 1));

	VOut output;
	output.position = mul(projectionMatrix, positionVS);
	output.color = input.color;
	output.texCoords = input.texCoords;
	output.texCoords2 = input.texCoords2;
	output.clipDist = dot(positionVS, clipPlane);

	return output;
}

cbuffer PixelShaderBuffer
{
	uint alphaTest;
	uint texEnv;
	float2 seed;
	float invGamma;
	float invBrightness;
	float noiseScale;
	float dummy;
};

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

Texture2D texture1 : register(t1);
SamplerState sampler1 : register(s1);

#ifdef CNQ3_DITHER
float Hash(float2 input)
{
	// this is from Morgan McGuire's "Hashed Alpha Testing" paper
	return frac(1.0e4 * sin(17.0 * input.x + 0.1 * input.y) + (0.1 + abs(sin(13.0 * input.y + input.x))));
}

float Linearize(float color)
{
	return pow(abs(color * invBrightness), invGamma) * sign(color);
}

float4 Dither(float4 color, float3 position)
{
	float2 newSeed = position.xy + float2(0.6849, 0.6849) * seed + float2(position.z, position.z);
	float noise = (noiseScale / 255.0) * Linearize(Hash(newSeed) - 0.5);

	return color + float4(noise, noise, noise, 0.0);
}
#endif

#ifdef CNQ3_A2C
float CorrectAlpha(float threshold, float alpha, float2 tc)
{
	float w, h;
	texture0.GetDimensions(w, h);
	float dx = max(abs(ddx(tc.x * w)), 0.001);
	float dy = max(abs(ddy(tc.y * h)), 0.001);
	float dxy = max(dx, dy); // apply the smallest boost
	float scale = max(1.0 / dxy, 1.0);
	float ac = threshold + (alpha - threshold) * scale;

	return ac;
}
#endif

float4 ps_main(VOut input) : SV_Target0
{
	float4 s = texture1.Sample(sampler1, input.texCoords2);
	float4 p = texture0.Sample(sampler0, input.texCoords);
	float4 r;
	if(texEnv == 1)
		r = input.color * s * p;
	else if(texEnv == 2)
		r = s; // use input.color or not?
	else if(texEnv == 3)
		r = input.color * float4(p.rgb * (1 - s.a) + s.rgb * s.a, p.a);
	else if(texEnv == 4)
		r = input.color * float4(p.rgb + s.rgb, p.a * s.a);
	else // texEnv == 0
		r = input.color * p;

#ifdef CNQ3_DITHER
	r = Dither(r, input.position.xyz);
#endif

#ifdef CNQ3_A2C
	if(alphaTest == 1)
		r.a = CorrectAlpha(0.0, r.a, input.texCoords);
	else if(alphaTest == 2)
		r.a = CorrectAlpha(0.5, 1.0 - r.a, input.texCoords);
	else if(alphaTest == 3)
		r.a = CorrectAlpha(0.5, r.a, input.texCoords);
#else
	if((alphaTest == 1 && r.a == 0.0) ||
	   (alphaTest == 2 && r.a >= 0.5) ||
	   (alphaTest == 3 && r.a <  0.5))
	   discard;
#endif

	return r;
}
