cmake_minimum_required(VERSION 3.15)

# set project name
project(Demos)

#find_package(CUDA REQUIRED)

# set the executable name
set(EXECUTABLE_SEND SendDemo)

# set compiler
set(CMAKE_C_COMPILER "gcc")
# set ibv_utils library path and pkt_gen library path
set(IBV_UTILS_PATH ibv_utils)
set(PKT_GEN_PATH pkt_gen)
set(UTILS_PATH utils)
# set source files
set(SEND_SRC SenderSim.c)

aux_source_directory(${UTILS_PATH} UTILS_SRC)
aux_source_directory(${PKT_GEN_PATH} PKT_GEN_SRC)
# include ibv_utils header files
include_directories(${IBV_UTILS_PATH}/include ${PKT_GEN_PATH})
include_directories(${UTILS_PATH})
# link ibv_utils library
#link_directories(${IBV_UTILS_PATH}/lib)
link_directories(/usr/local/lib)
link_libraries(ibv_utils)
link_libraries(ibverbs)

# add executable
add_executable(${EXECUTABLE_SEND} ${SEND_SRC} ${UTILS_SRC} ${PKT_GEN_SRC})

# install executable
install(TARGETS ${EXECUTABLE_RECV} DESTINATION /usr/local/bin)
install(TARGETS ${EXECUTABLE_SEND} DESTINATION /usr/local/bin)
install(TARGETS ${EXECUTABLE_RNICINFO} DESTINATION /usr/local/bin)