find_package(DD4hep REQUIRED COMPONENTS DDCore)
  dd4hep_set_compiler_flags()
  dd4hep_add_plugin(RaindropGeo SOURCES geometry/Raindrop_geo.cpp USES DD4hep::DDCore)

  And include it from the existing examples/CMakeLists.txt with an optional guard:

  find_package(DD4hep QUIET COMPONENTS DDCore)
  if(DD4hep_FOUND)
      add_subdirectory(dd4hep)
  endif()
