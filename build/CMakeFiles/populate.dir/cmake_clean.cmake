file(REMOVE_RECURSE
  "spawn-binding-debug-test.wgt"
  "spawn-binding-debug.wgt"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/populate.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
