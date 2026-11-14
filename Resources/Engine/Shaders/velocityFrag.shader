<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<source>
	<!--

precision highp float;
precision highp int;

TK_LOC(0) in vec2 v_ndc;

layout (location = 0) out vec4 fragColor;

void main()
{
	// Normalised device coordinates to the 0..1 range the resolve samples with.
	// The alpha marks that an object was drawn here at all: with both targets cleared to zero, that is
	// what tells the resolve the difference between the two positions is a real motion and not the
	// background.
	fragColor = vec4(v_ndc * 0.5 + 0.5, 1.0, 1.0);
}

	-->
	</source>
</shader>
