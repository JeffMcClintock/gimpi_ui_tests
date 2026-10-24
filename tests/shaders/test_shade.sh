// The shader brush the GpuShader tests use (gpu_shader_tests.cpp). Compiled
// into tests/shaders/compiled/shaders_test_shade.cpp by
//   cmake -DSHADERC=<bgfx shaderc> -DUSER_SHADER=tests/shaders/test_shade.sh \
//         -DUSER_NAME=test_shade -DUSER_OUT=tests/shaders/compiled/shaders_test_shade.cpp \
//         -P <gmpi_ui>/backends/gpu/shaders/compile_shaders.cmake
//
// colour = u_params[0] + p.x * u_params[1] + p.y * u_params[2] + u_time.x * u_params[3]
//
// so a test can ask for a constant (params 1..3 zero), a ramp along brush
// space, or something that moves, by choosing the parameters.
vec4 shade(vec2 p) {
  return u_params[0] + p.x * u_params[1] + p.y * u_params[2] + u_time.x * u_params[3];
}
