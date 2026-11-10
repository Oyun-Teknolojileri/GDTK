<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "ssrPassDataInc.shader" />
	<texture slot = "0" name = "s_accum" />
	<texture slot = "1" name = "s_normalDepth" />
	<source>
	<!--

precision highp float;
precision highp int;

TK_LOC(2) in vec2 v_texture;

layout (location = 0) out vec4 fragColor;

TK_SAMPLER_BINDING(0) uniform sampler2D s_accum;       // rgb: accumulated reflection, a: its weight.
TK_SAMPLER_BINDING(1) uniform sampler2D s_normalDepth; // b: linear depth.

float Median3(float a, float b, float c)
{
	return max(min(a, b), min(max(a, b), c));
}

vec3 Median3(vec3 a, vec3 b, vec3 c)
{
	return max(min(a, b), min(max(a, b), c));
}

void main()
{
	vec2 uv = v_texture;

	// Debug views are the raw trace, never filtered: they have to keep reading as data.
	if (ssrPass.flags.x > 0.5)
	{
		fragColor = texture(s_accum, uv);
		return;
	}

	float depth = texture(s_normalDepth, uv).b;
	if (depth <= 0.0)
	{
		fragColor = vec4(0.0);
		return;
	}

	// The accumulation is mostly converged; what survives is the occasional sample that caught a sharp
	// highlight, which the spatial average smears into a streak and the history then keeps for a second.
	// A median of the neighbourhood removes exactly those outliers without blurring the rest: the
	// surface keeps the edges its reflection has, a firefly loses against its eight neighbours.
	//
	// The nine taps are reduced to the median of three row medians. That is not the exact median of
	// nine, but it removes an isolated outlier just the same and costs nine compares instead of the
	// sorting network an exact median needs - the pass runs on a phone.
	vec2 texel = ssrPass.screenParams.xy;

	vec3 row[3];
	float rowWeight[3];

	for (int y = -1; y <= 1; ++y)
	{
		vec4 left   = texture(s_accum, uv + vec2(-texel.x, texel.y * float(y)));
		vec4 center = texture(s_accum, uv + vec2(0.0, texel.y * float(y)));
		vec4 right  = texture(s_accum, uv + vec2(texel.x, texel.y * float(y)));

		// A tap on another surface belongs to another reflection; it must not vote.
		float dl = texture(s_normalDepth, uv + vec2(-texel.x, texel.y * float(y))).b;
		float dc = texture(s_normalDepth, uv + vec2(0.0, texel.y * float(y))).b;
		float dr = texture(s_normalDepth, uv + vec2(texel.x, texel.y * float(y))).b;

		vec4 c = texture(s_accum, uv);
		left   = abs(dl - depth) < 0.5 ? left   : c;
		center = abs(dc - depth) < 0.5 ? center : c;
		right  = abs(dr - depth) < 0.5 ? right  : c;

		row[y + 1]       = Median3(left.rgb, center.rgb, right.rgb);
		rowWeight[y + 1] = Median3(left.a, center.a, right.a);
	}

	vec3 color   = Median3(row[0], row[1], row[2]);
	float weight = Median3(rowWeight[0], rowWeight[1], rowWeight[2]);

	fragColor = vec4(color, clamp(weight, 0.0, 1.0));
}

	-->
	</source>
</shader>
