
SET(signal_srcs
    "include/signal/delegate.hpp"
)

SET(filesystem_srcs
    "src/filesystem/filesystem.cpp"
)

SOURCE_GROUP("signal" FILES ${signal_srcs})
SOURCE_GROUP("filesystem" FILES ${filesystem_srcs})

ADD_LIBRARY(diamond_dogs_core STATIC ${signal_srcs} ${filesystem_srcs})
