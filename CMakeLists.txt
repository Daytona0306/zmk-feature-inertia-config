zephyr_include_directories(include)

if(CONFIG_ZMK_INERTIA_RUNTIME)
  zephyr_library()
  target_sources(app PRIVATE src/inertia_runtime.c)
endif()
