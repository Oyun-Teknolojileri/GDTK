<shader>
	<type name = "vertexShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "cameraDataInc.shader" />
	<include name = "perDrawDataInc.shader" />
	<include name = "instanceDataInc.shader" />
	<source>
	<!--

precision highp float;
precision highp int;

layout (location = 0) in vec3 vPosition;

TK_LOC(0) out vec2 v_ndc;

void main()
{
	// The screen position of the vertex with the model matrix the renderer handed this draw. The pass
	// draws the same geometry twice, once with the transform of this frame and once with the one it had
	// a frame ago, and the second buffer minus the first is the motion the object itself made.
	vec4 worldPos = INSTANCE_MODEL_MATRIX * vec4(vPosition, 1.0);
	vec4 clip     = camera.projectionView * worldPos;

	float w = abs(clip.w) > 1e-5 ? clip.w : 1e-5;
	v_ndc   = clip.xy / w;

	gl_Position = clip;
}

	-->
	</source>
</shader>
