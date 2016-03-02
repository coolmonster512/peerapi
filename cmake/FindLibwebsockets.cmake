# The following variables will be defined:
#
#  LWS_LIBRARIES
#

set(LWS_WITH_STATIC ON CACHE INTERNAL "")
set(LWS_WITH_SHARED OFF CACHE INTERNAL "")
set(LWS_WITHOUT_CLIENT OFF CACHE INTERNAL "")
set(LWS_WITHOUT_SERVER ON CACHE INTERNAL "")
set(LWS_WITH_SSL ON CACHE INTERNAL "")
set(LWS_WITH_ZLIB ON CACHE INTERNAL "")
set(LWS_USE_BUNDLED_ZLIB OFF CACHE INTERNAL "")
set(LWS_WITHOUT_TESTAPPS ON CACHE INTERNAL "")
set(LWS_WITHOUT_TEST_CLIENT ON CACHE INTERNAL "")
set(LWS_WITHOUT_TEST_ECHO ON CACHE INTERNAL "")
set(LWS_WITHOUT_TEST_FRAGGLE ON CACHE INTERNAL "")
set(LWS_WITHOUT_TEST_PING ON CACHE INTERNAL "")
set(LWS_WITHOUT_TEST_SERVER ON CACHE INTERNAL "")
set(LWS_WITHOUT_TEST_SERVER_EXTPOLL ON CACHE INTERNAL "")
set(LWS_ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIR} CACHE PATH "Path to the zlib include directory")
set(LWS_ZLIB_LIBRARIES ${ZLIB_LIBRARIES} CACHE PATH "Path to the zlib library")
set(LWS_OPENSSL_LIBRARIES ${WEBRTC_BORING_SSL_LIBRARIES} CACHE PATH "Path to the OpenSSL library")
set(LWS_OPENSSL_INCLUDE_DIRS ${WEBRTC_BORING_SSL_INCLUDE} CACHE PATH "Path to the OpenSSL include directory")
set(LWS_WITHOUT_EXTENSIONS ON CACHE INTERNAL "")

add_subdirectory(${PROJECT_SOURCE_DIR}/src/libs/libwebsockets)

