INCLUDE(CheckCSourceCompiles)
FOREACH(_CMAKE_INLINE "inline" "__inline__" "__inline")
  IF (NOT _CMAKE_INLINE_RESULT)
    CHECK_C_SOURCE_COMPILES(
"#ifndef __cplusplus
typedef int foo_t;
static ${_CMAKE_INLINE} foo_t static_foo () { return 0; }
${_CMAKE_INLINE} foo_t foo () {return 0;}
#endif
int main() {}"
    _CMAKE_INLINE_RESULT)
    IF (NOT _CMAKE_INLINE STREQUAL "inline")
      IF (_CMAKE_INLINE_RESULT)
        SET(CMAKE_INLINE ${_CMAKE_INLINE})
      ENDIF (_CMAKE_INLINE_RESULT)
    ENDIF (NOT _CMAKE_INLINE STREQUAL "inline")
  ENDIF(NOT _CMAKE_INLINE_RESULT)
ENDFOREACH(_CMAKE_INLINE)
