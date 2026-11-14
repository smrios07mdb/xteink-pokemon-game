add_executable(PokemonExtrasTest
  PokemonExtrasTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtras.cpp
)
target_include_directories(PokemonExtrasTest PRIVATE "${REPO_ROOT}/lib/Pokemon")
target_compile_definitions(PokemonExtrasTest PRIVATE CROSSINK_POKEMON_EXTRAS=1)
target_link_libraries(PokemonExtrasTest PRIVATE crosspoint_test_common)
add_test(NAME PokemonExtrasTest COMMAND PokemonExtrasTest)

add_executable(PokemonExtrasCodecTest
  PokemonExtrasCodecTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtras.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtrasCodec.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonCrc32.cpp
)
target_include_directories(PokemonExtrasCodecTest PRIVATE "${REPO_ROOT}/lib/Pokemon")
target_compile_definitions(PokemonExtrasCodecTest PRIVATE CROSSINK_POKEMON_EXTRAS=1)
target_link_libraries(PokemonExtrasCodecTest PRIVATE crosspoint_test_common)
add_test(NAME PokemonExtrasCodecTest COMMAND PokemonExtrasCodecTest)
