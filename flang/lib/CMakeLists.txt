add_subdirectory(Evaluate)
add_subdirectory(Decimal)
add_subdirectory(Lower)
add_subdirectory(Parser)
add_subdirectory(Semantics)
add_subdirectory(Support)
add_subdirectory(Frontend)
add_subdirectory(FrontendTool)
add_subdirectory(Utils)

add_subdirectory(Optimizer)

# Fortran Interpreter (incremental execution backed by LLVM ORC)
add_subdirectory(Interpreter)

if (FLANG_INCLUDE_TESTS)
  add_subdirectory(Testing)
endif ()
