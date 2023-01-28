/*
===========================================================================
Copyright (C) 2023 Gian 'myT' Schellenbaum

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
// helper functions used by multiple shader files


float4 MakeGreyscale(float4 input, float amount)
{
	float grey = dot(input.rgb, float3(0.299, 0.587, 0.114));
	float4 result = lerp(input, float4(grey, grey, grey, input.a), amount);

	return result;
}

/*
f   = far  clip plane distance
n   = near clip plane distance
exp = exponential depth value (as stored in the Z-buffer)

					 2 * f * n             B
linear(exp) = ----------------------- = -------
			  (f + n) - exp * (f - n)   exp - A

			f + n               -2 * f * n
with    A = -----    and    B = ----------
			f - n                  f - n
*/
float LinearDepth(float zwDepth, float proj22, float proj32)
{
	return proj32 / (zwDepth - proj22);
}
