#version 450

layout(location = 0) in vec3 incolor;

layout(location = 0) out vec3 outcolor;

void main() {
    outcolor = incolor;
}
