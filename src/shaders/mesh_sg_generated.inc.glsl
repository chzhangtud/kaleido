struct SGParams { float p0; float p1; float p2; float p3; };
float sg_noise_perlin3d(vec3 p) {
    return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453) * 2.0 - 1.0;
}

vec3 sg_eval_base_color(vec2 uv, vec3 wpos, vec3 nrm, float timeSec, SGParams params)
{
    float sg_n4_p0 = sin(params.p2);
    float sg_n5_p0 = cos(params.p2);
    vec3 sg_n10_p0 = vec3(sg_n4_p0, sg_n5_p0, uv.x);
    return sg_n10_p0;
}
