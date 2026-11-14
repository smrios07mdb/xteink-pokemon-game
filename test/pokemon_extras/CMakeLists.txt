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

# M2 rules (streaks, rare encounter, Day Care). Needs species/item data, so it
# links upstream's game sources and reuses PokemonGameTest's generated headers.
set(POKEMON_EXTRAS_RULES_GENERATED_DIR "${CMAKE_BINARY_DIR}/pokemon_game/generated")
add_executable(PokemonExtrasRulesTest
  PokemonExtrasRulesTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtras.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtrasRules.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonTypes.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonSpecies.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonItemData.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonGame.cpp
)
add_dependencies(PokemonExtrasRulesTest PokemonGameTest)
target_include_directories(PokemonExtrasRulesTest PRIVATE
  "${REPO_ROOT}/lib/Pokemon"
  "${POKEMON_EXTRAS_RULES_GENERATED_DIR}"
)
target_compile_definitions(PokemonExtrasRulesTest PRIVATE CROSSINK_POKEMON_EXTRAS=1)
target_link_libraries(PokemonExtrasRulesTest PRIVATE crosspoint_test_common)
add_test(NAME PokemonExtrasRulesTest COMMAND PokemonExtrasRulesTest)

# Feature 10: Party sleep screen geometry.
add_executable(PokemonExtrasLayoutTest
  PokemonExtrasLayoutTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtrasLayout.cpp
)
target_include_directories(PokemonExtrasLayoutTest PRIVATE "${REPO_ROOT}/lib/Pokemon")
target_compile_definitions(PokemonExtrasLayoutTest PRIVATE CROSSINK_POKEMON_EXTRAS=1)
target_link_libraries(PokemonExtrasLayoutTest PRIVATE crosspoint_test_common)
add_test(NAME PokemonExtrasLayoutTest COMMAND PokemonExtrasLayoutTest)
