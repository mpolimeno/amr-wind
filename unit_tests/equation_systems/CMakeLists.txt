target_sources(${amr_wind_unit_test_exe_name}
  PRIVATE
  test_pde.cpp
  test_icns_cstdens.cpp
  test_icns_gravityforcing.cpp
  test_icns_init.cpp
  test_explicit_diffusion_rk2.cpp
  )
