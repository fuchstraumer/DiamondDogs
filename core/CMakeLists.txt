
SET(filesystem_srcs
    "src/filesystem/filesystem.cpp"
)

SOURCE_GROUP("filesystem" FILES ${filesystem_srcs})

ADD_LIBRARY(diamond_dogs_core STATIC ${filesystem_srcs})
