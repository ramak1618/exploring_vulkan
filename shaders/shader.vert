#version 450

layout(location = 0) in vec3 inpos;
layout(location = 1) in vec3 incolor;
layout(location = 2) in vec3 area;

layout(location = 0) out vec3 outcolor;

layout(binding = 0) uniform readonly camera {
    mat4 view;
    mat4 proj;
    vec2 screen;
} cam;

void main() {
    vec4 view_coords = cam.view * vec4(inpos, 1.);
    gl_Position = cam.proj * view_coords;
    outcolor = incolor;
    vec4 view_area = cam.view * vec4(area, 0);
    float proj_area = abs(view_area.z * view_coords.z);
    gl_PointSize = (cam.screen.y * cam.proj[1].y * sqrt(proj_area)) / (view_coords.z * view_coords.z);
}
