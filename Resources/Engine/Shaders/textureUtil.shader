<shader>
	<type name = "includeShader" />
	<source>
	<!--	

	#ifndef TEXTURE_UTIL_SHADER
	#define TEXTURE_UTIL_SHADER

// Returns uv coordinates and layer: vec3(u, v, layer)
// Branch-free cubemap face lookup for shadow atlas.
// Layer order must match ShadowPass.cpp cube map rotations:
//   0: +X   1: -X   2: +Y   3: -Y   4: +Z   5: -Z
vec3 UVWToUVLayer(vec3 v)
{
	vec3 a = abs(v);

	// Determine dominant axis: 0=X, 1=Y, 2=Z.
	// step(a.yx, a.xy) = (a.x>=a.y, a.y>=a.x)
	float isX = step(a.y, a.x) * step(a.z, a.x); // 1 if X dominant
	float isY = (1.0 - isX) * step(a.z, a.y);     // 1 if Y dominant (and not X)
	float isZ = 1.0 - isX - isY;                  // 1 if Z dominant

	// Dominant axis magnitude and sign.
	float major = isX * a.x + isY * a.y + isZ * a.z;
	float signVal = isX * v.x + isY * v.y + isZ * v.z;
	float s = step(0.0, signVal) * 2.0 - 1.0; // +1 or -1

	// Face index: base + (negative ? 1 : 0).
	float layer = isX * 0.0 + isY * 2.0 + isZ * 4.0 + (1.0 - step(0.0, signVal));

	// Per-face UV mapping (matches the original convention exactly):
	//   +X: (-z/x, -y/x)   -X: ( z/|x|, -y/|x|)
	//   +Y: ( x/y,  z/y)   -Y: ( x/|y|, -z/|y|)
	//   +Z: ( x/z, -y/z)   -Z: (-x/|z|, -y/|z|)
	float inv = 1.0 / major;

	vec2 uvX = vec2(-s * v.z, -v.y) * inv;
	vec2 uvY = vec2(v.x, s * v.z) * inv;
	vec2 uvZ = vec2(s * v.x, -v.y) * inv;

	vec2 coord = isX * uvX + isY * uvY + isZ * uvZ;

	coord = coord * 0.5 + 0.5;
	return vec3(coord, layer);
}

#endif

	-->
	</source>
</shader>
