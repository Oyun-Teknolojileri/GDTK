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
};

TK_UBO_BINDING(0) uniform CameraData
{
	Camera camera;
};

#endif // CAMERA_DATA
	-->
	</source>
</shader>