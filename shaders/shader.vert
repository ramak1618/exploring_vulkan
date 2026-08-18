#version 450

layout(location = 0) in vec3 inpos;
layout(location = 1) in vec3 incolor;

layout(location = 0) out vec3 outcolor;

layout(binding = 0) uniform readonly camera {
    mat4 view;
    mat4 proj;
} cam;

void main() {
    gl_Position = cam.proj * cam.view * vec4(inpos, 1.0);
    outcolor = incolor;
    gl_PointSize = 30.f / (gl_Position.z);
}
