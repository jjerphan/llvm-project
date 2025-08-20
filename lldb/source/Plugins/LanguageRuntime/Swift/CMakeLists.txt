add_lldb_library(lldbPluginSwiftLanguageRuntime PLUGIN
  LLDBMemoryReader.cpp
  ReflectionContext.cpp
  SwiftLanguageRuntime.cpp
  SwiftLanguageRuntimeDynamicTypeResolution.cpp
  SwiftLanguageRuntimeNames.cpp
  SwiftLanguageRuntimeRemoteAST.cpp
  SwiftMetadataCache.cpp
  SwiftTask.cpp

  LINK_LIBS
    lldbCore
    lldbExpression
    lldbInterpreter
    lldbTarget
    lldbUtility
    lldbPluginExpressionParserClang
    lldbPluginProcessUtility
    lldbPluginSwiftLanguage
    "${SWIFT_LIBRARY_DIR}/libswiftAST.a"
    "${SWIFT_LIBRARY_DIR}/libswiftBasic.a"
    "${SWIFT_LIBRARY_DIR}/libswiftRemoteInspection.a"
    "${SWIFT_LIBRARY_DIR}/libswiftRemoteAST.a"
)
