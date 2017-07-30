#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(location = 0) in vec3 position;

layout(set = 0, binding = 0) uniform UBO {
	mat4 model;
	mat4 view;
	mat4 projection;
} ubo;

layout(set = 0, binding = 1) uniform atmoUBO {
	vec4 cameraPos;
	vec4 lightDir;
	vec4 invWavelength;
	vec4 lightPos;
	vec3 cHoRiR;
} atmo_ubo;

layout(constant_id = 0) const float KrESun = 0.0025f * 16.0f;
layout(constant_id = 1) const float KmESun = 0.0010f * 16.0f;
layout(constant_id = 2) const float scaleDepth = 0.25f;
layout(constant_id = 3) const float G = -0.99f;
layout(constant_id = 4) const float Kr4PI = 0.0025f * 4.0f * 3.14159f;
layout(constant_id = 5) const float Km4PI = 0.0010f * 4.0f * 3.14159f;
layout(constant_id = 6) const int samples = 4;

out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) out vec4 secondaryColor;
layout(location = 1) out vec4 primaryColor;
layout(location = 2) out vec3 vDir;

float scale_func(float _cos) {
	float x = 1.0f - _cos;
	return scaleDepth * exp(-0.00287 + x * (0.459 + x * (3.83 + x * (-6.80 + x * 5.25))));
}

void main(){
	float cameraHeight = atmo_ubo.cHoRiR.x;
	float outerRadius = atmo_ubo.cHoRiR.y;
	float innerRadius = atmo_ubo.cHoRiR.z;

	float scale = 1.0f / (outerRadius - innerRadius);
	// World-space position of current vertex.
    vec3 v_pos = mat3(ubo.model) * position;
	// Eye ray.
	vec3 v_ray = v_pos - atmo_ubo.cameraPos.xyz; 
	float far = length(v_ray);
	v_ray /= far;
	// ray is now a unit vector.

	// Get closest intersection of ray with atmosphere.
	float B = 2.0f * dot(atmo_ubo.cameraPos.xyz, v_ray);
	float C = (cameraHeight * cameraHeight) - (outerRadius * outerRadius);
	float det = max(0.0f, B * B - 4.0f - C);
	float near = 0.50f * (-B - sqrt(det));

	bool in_atmo;
	if(cameraHeight > outerRadius){
		in_atmo = true;
	 }
	 else {
		in_atmo = false;
	 } 

	vec3 start;
	if(in_atmo) {
		start = atmo_ubo.cameraPos.xyz + v_ray * near;
	} 
	else {
		start = atmo_ubo.cameraPos.xyz;
	}

	float height = length(start);
	float depth = exp(scale / scaleDepth * (innerRadius - cameraHeight));

	far -= near;

	float start_angle;
	if(in_atmo){
		start_angle = dot(v_ray, start) / outerRadius;
	}
	else {
		start_angle = dot(v_ray, start) / height;
	}

	float start_depth = exp(-1.0f / scaleDepth);

	float start_offset;
	if(in_atmo){
		start_offset = start_depth * scale_func(start_angle);
	}
	else {
		start_offset = depth * scale_func(start_angle);
	}

	float sample_length = far / samples;
	float scaled_length = sample_length * scale;
	vec3 sample_ray = v_ray * sample_length;
	vec3 sample_point = start + sample_ray * 0.50f;

	vec3 front_color = vec3(0.0f);

	for(int i = 0; i < samples; ++i){
		float sample_height = length(sample_point);
		float sample_depth = exp((scale / scaleDepth) * (innerRadius - height));
		float light_angle = dot(atmo_ubo.lightDir.xyz, sample_point) / height;
		float camera_angle = dot(v_ray, sample_point) / height;
		float scatter = (start_offset + depth * (scale_func(light_angle) - scale_func(camera_angle)));
		vec3 attenuate = exp(-scatter * (atmo_ubo.invWavelength.xyz * Kr4PI + Km4PI));
		front_color += attenuate * (depth * scaled_length);
		sample_point += sample_ray;
	}

	secondaryColor.rgb = front_color * KmESun;
	primaryColor.rgb = front_color * (atmo_ubo.invWavelength.xyz * KrESun);
	gl_Position = ubo.projection * ubo.view * ubo.model * vec4(position, 1.0f);

	vDir = atmo_ubo.cameraPos.xyz - position;
}