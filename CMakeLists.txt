zephyr_include_directories(include)

if(CONFIG_ZMK_INERTIA_RUNTIME)
  zephyr_library()
  target_sources(app PRIVATE src/inertia_runtime.c)
  if(CONFIG_ZMK_INERTIA_RUNTIME_STUDIO_RPC)
    target_sources(app PRIVATE src/studio/inertia_rpc_stub.c)
  endif()
endif()
