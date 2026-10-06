<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<uniform slot = "0" name = "CameraData" />
	<source>
	<!--
#ifndef CAMERA_DATA
#define CAMERA_DATA

// Camera Data
//////////////////////////////////////////

struct Camera
{
	vec3 position;
	float farPlane;

	vec3 direction;
	float pad0;

	mat4 projection;
	mat4 view;
	mat4 projectionView;
	mat4 projectionViewNoTranslate;

	// The same matrices the frame before this one was drawn with. A pass that reuses its own result
	// from the last frame reprojects with these to find where a pixel's surface was, which is exact
	// for geometry that did not move and needs no motion vectors. See temporalInc.shader.
	mat4 prevProjectionView;
};

TK_UBO_BINDING(0) uniform CameraData
{
	Camera camera;
};

#endif // CAMERA_DATA
	-->
	</source>
</shader>