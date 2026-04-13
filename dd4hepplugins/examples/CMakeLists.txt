find_package(DD4hep REQUIRED COMPONENTS DDCore)
dd4hep_set_compiler_flags()
dd4hep_add_plugin(RaindropGeo SOURCES geometry/Raindrop_geo.cpp USES DD4hep::DDCore)
