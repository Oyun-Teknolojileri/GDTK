<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<texture slot = "0" name = "s_glow" />
	<source>
	<!--

precision highp float;
precision highp sampler2D;

TK_SAMPLER_BINDING(0) uniform sampler2D s_glow;

TK_LOC(2) in vec2 v_texture;
out vec4 fragColor;

/*
 * Adds the half resolution glow sum over the frame.
 *
 * The sum itself ran at half resolution because the halo has no detail finer than that; the one
 * bilinear tap here is what puts it back on the frame grid. The alpha is zero on purpose: the pass
 * blends additively into a frame whose alpha channel the host owns, and adding one to it every
 * frame would leave the compositor with an opaque white the first time it asked.
 */
void main()
{
	fragColor = vec4(texture(s_glow, v_texture.xy).rgb, 0.0);
}
	-->
	</source>
</shader>
