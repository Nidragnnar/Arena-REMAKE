cmake_minimum_required(VERSION 3.25)
project(ArenaRemake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
	add_compile_options(/W4 /utf-8 /permissive-)
endif()

# Leitura dos formatos originais do Arena e composição da UI (sem dependências externas).
add_library(arena_core STATIC
	src/assets/Pklite.cpp
	src/assets/Bsa.cpp
	src/assets/Compression.cpp
	src/assets/AssetManager.cpp
	src/assets/Image.cpp
	src/assets/Font.cpp
	src/render/Canvas.cpp
	src/render/ImageOps.cpp
	src/render/Ui.cpp
	src/i18n/Localization.cpp
	src/game/Game.cpp
	src/game/Screens.cpp
	src/game/ArenaData.cpp
	src/game/Dialog.cpp
	src/game/CharacterCreation.cpp
	src/world/MifFile.cpp
	src/world/InfFile.cpp
	src/world/Level.cpp
	src/world/TextureStyle.cpp
	src/world/SpriteOverrides.cpp
	src/render3d/Renderer3D.cpp
	src/render3d/Hud.cpp
	src/render3d/UiKit.cpp
	src/game/GameWorldScreen.cpp
	src/game/GameWorldSpells.cpp
	src/game/GameWorldSave.cpp
	src/game/Items.cpp
	src/game/Spells.cpp
	src/game/Leveling.cpp
	src/game/CityGen.cpp
	src/game/WildGen.cpp
	src/game/DungeonGen.cpp
	src/game/TravelMenu.cpp
	src/game/NewCharacter.cpp
	src/game/InventoryUi.cpp
	src/game/SpellbookUi.cpp
	src/game/GameMenus.cpp
	src/game/ServiceMenu.cpp
	src/game/DialogueMenu.cpp
	src/game/Creatures.cpp
	src/game/Citizens.cpp
	src/audio/Audio.cpp
)
target_include_directories(arena_core PUBLIC src)
find_path(STB_INCLUDE_DIRS "stb_image.h" REQUIRED) # vcpkg: stb (leitura dos PNG redesenhados)
target_include_directories(arena_core PRIVATE ${STB_INCLUDE_DIRS})
if(WIN32)
	target_link_libraries(arena_core PUBLIC winmm) # música via MIDI do Windows
endif()

add_executable(arena_tool tools/arena_tool.cpp)
target_link_libraries(arena_tool PRIVATE arena_core)

find_package(SDL2 CONFIG REQUIRED)

add_executable(ArenaRemake src/main.cpp src/gpu/GlDevice.cpp)
target_link_libraries(ArenaRemake PRIVATE arena_core SDL2::SDL2)
if(WIN32)
	target_link_libraries(ArenaRemake PRIVATE opengl32) # renderização pela placa de vídeo
else()
	find_package(OpenGL REQUIRED)
	target_link_libraries(ArenaRemake PRIVATE OpenGL::GL)
endif()

# Dados do remake (idiomas, mapas de texto) ao lado do executável.
add_custom_command(TARGET ArenaRemake POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:ArenaRemake>/data
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/data/settings.ini $<TARGET_FILE_DIR:ArenaRemake>/settings.ini
)
