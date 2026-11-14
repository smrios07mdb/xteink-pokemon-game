# docs/extras/SAVE-SAFETY.md R10. Same sources as PokemonServiceTest plus the
# extras files; reuses that test's generated species/moves/... headers.
set(POKEMON_EXTRAS_COMPAT_GENERATED_DIR "${CMAKE_BINARY_DIR}/pokemon_service/generated")

add_executable(PokemonExtrasCompatTest
  PokemonExtrasCompatTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonTypes.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonSpecies.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonGame.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonStoreCodec.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonTracker.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonMoveData.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonBaseStats.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonLearnsets.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonItemData.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonGymData.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonTypeChart.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonBattle.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonTrainers.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonBattleStoreCodec.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonIvEvStoreCodec.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonMovesetStoreCodec.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonHallOfFameCodec.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonCrc32.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtras.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonExtrasCodec.cpp
  ${REPO_ROOT}/src/pokemon/PokemonStore.cpp
  ${REPO_ROOT}/src/pokemon/PokemonBattleStore.cpp
  ${REPO_ROOT}/src/pokemon/PokemonIvEvStore.cpp
  ${REPO_ROOT}/src/pokemon/PokemonMovesetStore.cpp
  ${REPO_ROOT}/src/pokemon/PokemonHallOfFameStore.cpp
  ${REPO_ROOT}/src/pokemon/PokemonService.cpp
  ${REPO_ROOT}/src/pokemon/PokemonExtrasStore.cpp
)
add_dependencies(PokemonExtrasCompatTest PokemonServiceTest)

target_include_directories(PokemonExtrasCompatTest PRIVATE
  "${REPO_ROOT}/test/pokemon_store/stubs"
  "${POKEMON_EXTRAS_COMPAT_GENERATED_DIR}"
  "${REPO_ROOT}/src"
)

target_compile_definitions(PokemonExtrasCompatTest PRIVATE
  CROSSINK_ENABLE_POKEMON=1
  CROSSINK_POKEMON_EXTRAS=1
  POKEMON_SERVICE_HOST_TEST=1
)

target_link_libraries(PokemonExtrasCompatTest PRIVATE
  crosspoint_test_common
)

add_test(NAME PokemonExtrasCompatTest COMMAND PokemonExtrasCompatTest)
