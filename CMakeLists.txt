cmake_minimum_required( VERSION 2.8 )
project( hulk_script )

include_directories( include )
include_directories( $ENV{HULK_INSTALL_DIR}/hulk-core/include )
include_directories( $ENV{HULK_INSTALL_DIR}/hulk-fix/include )

link_directories( $ENV{HULK_INSTALL_DIR}/hulk-core/lib )
link_directories( $ENV{HULK_INSTALL_DIR}/hulk-fix/lib )

add_subdirectory( src )

set( CPACK_PACKAGE_VERSION "0.1" )
set( CPACK_GENERATOR "RPM" )
set( CPACK_PACKAGE_NAME "hulk-script" )
set( CPACK_PACKAGE_RELEASE 1 )
set( CPACK_PACKAGE_CONTACT "Mark McIlroy" )
set( CPACK_PACKAGE_VENDOR "Mark McIlroy" )
set( CPACK_PACKAGING_INSTALL_PREFIX "/usr/local/hulk-script" )
set( CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}.${CMAKE_SYSTEM_PROCESSOR}" )
include( CPack )

