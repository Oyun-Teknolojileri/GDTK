<shader>
	<type name = "includeShader" />
	<source>
	<!--
#ifndef NORMAL_ENCODING
#define NORMAL_ENCODING

// Octahedron normal encoding/decoding
// Encodes a unit normal into 2 floats in [-1, 1] range

vec2 octWrap(vec2 v)
{
	return (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

vec2 encodeNormal(vec3 n)
{
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	n.xy = n.z >= 0.0 ? n.xy : octWrap(n.xy);
	n.xy = n.xy * 0.5 + 0.5;
	return n.xy;
}

vec3 decodeNormal(vec2 e)
{
	e = e * 2.0 - 1.0;
	vec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
	float t = max(-n.z, 0.0);
	n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
	return normalize(n);
}

#endif // NORMAL_ENCODING
	-->
	</source>
</shader>
