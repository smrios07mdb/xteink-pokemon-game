add_executable(PokemonExtrasStoreTest
  PokemonExtrasStoreTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtras.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtrasCodec.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonCrc32.cpp
  ${REPO_ROOT}/src/pokemon/PokemonExtrasStore.cpp
)

target_include_directories(PokemonExtrasStoreTest PRIVATE
  "${REPO_ROOT}/test/pokemon_store/stubs"
  "${REPO_ROOT}/lib/Pokemon"
  "${REPO_ROOT}/src"
)

target_compile_definitions(PokemonExtrasStoreTest PRIVATE
  CROSSINK_ENABLE_POKEMON=1
  CROSSINK_POKEMON_EXTRAS=1
  POKEMON_SERVICE_HOST_TEST=1
)

target_link_libraries(PokemonExtrasStoreTest PRIVATE
  crosspoint_test_common
)

add_test(NAME PokemonExtrasStoreTest COMMAND PokemonExtrasStoreTest)
