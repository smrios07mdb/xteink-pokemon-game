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

# M3 rules: eggs (01-eggs.md) and daily quests (08-daily-quests.md). Same
# species/item data and generated headers as PokemonExtrasRulesTest.
add_executable(PokemonExtrasEggsTest
  PokemonExtrasEggsTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtras.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtrasEggs.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonTypes.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonSpecies.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonItemData.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonGame.cpp
)
add_dependencies(PokemonExtrasEggsTest PokemonGameTest)
target_include_directories(PokemonExtrasEggsTest PRIVATE
  "${REPO_ROOT}/lib/Pokemon"
  "${POKEMON_EXTRAS_RULES_GENERATED_DIR}"
)
target_compile_definitions(PokemonExtrasEggsTest PRIVATE CROSSINK_POKEMON_EXTRAS=1)
target_link_libraries(PokemonExtrasEggsTest PRIVATE crosspoint_test_common)
add_test(NAME PokemonExtrasEggsTest COMMAND PokemonExtrasEggsTest)

add_executable(PokemonExtrasQuestsTest
  PokemonExtrasQuestsTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtras.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtrasQuests.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonTypes.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonSpecies.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonItemData.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonGame.cpp
)
add_dependencies(PokemonExtrasQuestsTest PokemonGameTest)
target_include_directories(PokemonExtrasQuestsTest PRIVATE
  "${REPO_ROOT}/lib/Pokemon"
  "${POKEMON_EXTRAS_RULES_GENERATED_DIR}"
)
target_compile_definitions(PokemonExtrasQuestsTest PRIVATE CROSSINK_POKEMON_EXTRAS=1)
target_link_libraries(PokemonExtrasQuestsTest PRIVATE crosspoint_test_common)
add_test(NAME PokemonExtrasQuestsTest COMMAND PokemonExtrasQuestsTest)

# M4 rules: Kindle credit (05-kindle-credit.md) and the book-finish boss
# (03-book-boss.md). Kindle credit needs no species data.
add_executable(PokemonExtrasKindleTest
  PokemonExtrasKindleTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtras.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtrasKindle.cpp
)
target_include_directories(PokemonExtrasKindleTest PRIVATE "${REPO_ROOT}/lib/Pokemon")
target_compile_definitions(PokemonExtrasKindleTest PRIVATE CROSSINK_POKEMON_EXTRAS=1)
target_link_libraries(PokemonExtrasKindleTest PRIVATE crosspoint_test_common)
add_test(NAME PokemonExtrasKindleTest COMMAND PokemonExtrasKindleTest)

add_executable(PokemonExtrasBossTest
  PokemonExtrasBossTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtras.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtrasBoss.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtrasKindle.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtrasQuests.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtrasRules.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonTypes.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonSpecies.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonItemData.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonGame.cpp
)
add_dependencies(PokemonExtrasBossTest PokemonGameTest)
target_include_directories(PokemonExtrasBossTest PRIVATE
  "${REPO_ROOT}/lib/Pokemon"
  "${POKEMON_EXTRAS_RULES_GENERATED_DIR}"
)
target_compile_definitions(PokemonExtrasBossTest PRIVATE CROSSINK_POKEMON_EXTRAS=1)
target_link_libraries(PokemonExtrasBossTest PRIVATE crosspoint_test_common)
add_test(NAME PokemonExtrasBossTest COMMAND PokemonExtrasBossTest)
