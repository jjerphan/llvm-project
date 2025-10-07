add_flang_library(flangInterpreter
  FortranInterpreter.cpp
  FortranIncrementalParser.cpp
  FortranIncrementalExecutor.cpp

  LINK_COMPONENTS
    Support
    OrcJIT

  LINK_LIBS
    PUBLIC
    
)

target_include_directories(flangInterpreter
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)



