cmake_minimum_required(VERSION 3.4)
project(libcopynes)

set(LIBCOPYNES_SRC src/copynes.c)

add_library(copynes ${LIBCOPYNES_SRC})
