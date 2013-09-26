cmake_minimum_required( VERSION 2.8 )
project( hulk_script )

include_directories( include )
include_directories( ~/hulk-core/include )
include_directories( ~/hulk-fix/include )

link_directories( ~/hulk-build/core/src )
link_directories( ~/hulk-build/fix/src )

add_subdirectory( src )
