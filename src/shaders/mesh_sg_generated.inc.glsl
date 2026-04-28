struct SGParams
{
	float p0;
	float p1;
	float p2;
	float p3;
};

float sg_noise_perlin3d(vec3 p)
{
	return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453) * 2.0 - 1.0;
}

vec3 sg_eval_base_color(vec2 uv, vec3 wpos, vec3 nrm, float timeSec, SGParams params)
{
	float timeSpeed = (abs(params.p0) > 1e-6) ? params.p0 : 0.6;
	float uvScale = (abs(params.p1) > 1e-6) ? params.p1 : 6.0;
	vec3 noisePos = vec3(uv.x * uvScale, uv.y * uvScale, params.p2 * timeSpeed);
	float n = sg_noise_perlin3d(noisePos);
	float remap = clamp((n + 1.0) * 0.5, 0.0, 1.0);
	return vec3(remap);
}
