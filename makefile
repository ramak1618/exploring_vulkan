debug:
	gcc -fsanitize=address -g -Wall -Wextra -Wpedantic -Werror pcg/pcg_basic.c xdg-shell.c v.c -lm -lxkbcommon -lwayland-client -lvulkan -ov

debug2:
	gcc -g -Wall -Wextra -Wpedantic -Werror pcg/pcg_basic.c xdg-shell.c v.c -lm -lxkbcommon -lwayland-client -lvulkan -ov	

opt:
	gcc -O3 -flto -Wall -Wextra -Wpedantic -Werror pcg/pcg_basic.c xdg-shell.c v.c -lm -lxkbcommon -lwayland-client -lvulkan -ov

shaders:
	glslangValidator -V shader.vert -o vert.spv
	glslangValidator -V shader.frag -o frag.spv
