glslv = find_program('glslangValidator')
shader_compile_command = [glslv, '-V', '@INPUT@', '-o', '@OUTPUT@']

vertex_shader = custom_target('vert shader', input: 'shader.vert', output: 'vert.spv', command: shader_compile_command)
frag_shader = custom_target('frag shader', input: 'shader.frag', output: 'frag.spv', command: shader_compile_command)
