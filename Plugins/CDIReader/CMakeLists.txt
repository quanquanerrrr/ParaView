include_directories(
  ${PARAVIEW_INCLUDE_DIRS}
  ${PARAVIEW_GUI_INCLUDE_DIRS}
  ${PARAVIEW_KWSYS_INCLUDE_DIRS}
  ${VTK_INCLUDE_DIRS}
  )
add_definitions(-DHAVE_NETCDF2 -DHAVE_NETCDF4)
vtk_module_load(vtknetcdf)
add_paraview_plugin(CDIReader "1.0"
  SERVER_MANAGER_XML CDIReader.xml
  SERVER_MANAGER_SOURCES
    vtkCDIReader.cxx
    cdilib.c
  REQUIRED_ON_SERVER
)
set_source_files_properties(cdilib.c
  PROPERTIES COMPILE_FLAGS -std=c99)
target_link_libraries(CDIReader LINK_PRIVATE ${vtknetcdf_LIBRARIES})