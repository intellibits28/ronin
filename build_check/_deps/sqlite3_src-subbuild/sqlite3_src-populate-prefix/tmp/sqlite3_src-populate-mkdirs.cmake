# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/data/data/com.termux/files/home/play-ground/ronin/build_check/_deps/sqlite3_src-src")
  file(MAKE_DIRECTORY "/data/data/com.termux/files/home/play-ground/ronin/build_check/_deps/sqlite3_src-src")
endif()
file(MAKE_DIRECTORY
  "/data/data/com.termux/files/home/play-ground/ronin/build_check/_deps/sqlite3_src-build"
  "/data/data/com.termux/files/home/play-ground/ronin/build_check/_deps/sqlite3_src-subbuild/sqlite3_src-populate-prefix"
  "/data/data/com.termux/files/home/play-ground/ronin/build_check/_deps/sqlite3_src-subbuild/sqlite3_src-populate-prefix/tmp"
  "/data/data/com.termux/files/home/play-ground/ronin/build_check/_deps/sqlite3_src-subbuild/sqlite3_src-populate-prefix/src/sqlite3_src-populate-stamp"
  "/data/data/com.termux/files/home/play-ground/ronin/build_check/_deps/sqlite3_src-subbuild/sqlite3_src-populate-prefix/src"
  "/data/data/com.termux/files/home/play-ground/ronin/build_check/_deps/sqlite3_src-subbuild/sqlite3_src-populate-prefix/src/sqlite3_src-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/data/data/com.termux/files/home/play-ground/ronin/build_check/_deps/sqlite3_src-subbuild/sqlite3_src-populate-prefix/src/sqlite3_src-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/data/data/com.termux/files/home/play-ground/ronin/build_check/_deps/sqlite3_src-subbuild/sqlite3_src-populate-prefix/src/sqlite3_src-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
