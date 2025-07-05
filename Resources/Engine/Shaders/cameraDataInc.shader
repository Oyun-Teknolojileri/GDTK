<shader>
	<type name = "includeShader" />
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

layout(std140) uniform CameraData
{
	Camera camera;
};

#endif // CAMERA_DATA
	-->
	</source>
</shader>