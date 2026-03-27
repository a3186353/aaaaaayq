if(NOT WIN32 AND USE_MPEG)
	if(NOT MPEG_PATH AND NOT BUILD_STATIC)
		find_package(MPG123 QUIET)
		
		if(MPG123_FOUND)
			set(MPEG_SOURCE "(system)")
		endif()
	endif()
	if(MPEG_PATH OR BUILD_STATIC OR NOT MPG123_FOUND)
		FetchDependency(MPEG
			DIR mpg123
			FETCH_PRIORITY file svn git
			
			FILE_DOWNLOAD https://downloads.sourceforge.net/mpg123/mpg123-1.31.1.tar.bz2
			FILE_SUBDIR mpg123-1.31.1
			
			# unknown current version, to be fixed
			#SVN_REPOSITORY svn://scm.orgis.org/mpg123/trunk
			#SVN_REVISION -r4968 ?
			
			# "official" git repo: https://www.mpg123.de/trunk/.git/ but *very* slow (HTTP emulation)
			# "official" git mirror (default branch is not master), works too
			GIT_REPOSITORY https://github.com/madebr/mpg123
			GIT_TAG aec901b7a636b6eb61e03a87ff3547c787e8c693
			GIT_UNSHALLOW ON
		)
		
		if(MPEG_PATH)
			set(MPEG_LINK_PATH ${MPEG_BIN}/src/libmpg123/.libs/libmpg123.a)
			set(MPG123_INCLUDE_DIR ${MPEG_PATH}/src)
			
			if(NOT EXISTS ${MPEG_PATH}/configure)
				add_custom_target(MPEG_AUTORECONF
					COMMAND autoreconf -iv
					BYPRODUCTS ${MPEG_PATH}/configure
					WORKING_DIRECTORY ${MPEG_PATH}
				)
			endif()
			
			set(MPEG_CC "${CMAKE_C_COMPILER}")
			set(MPEG_CFLAGS "")
			if(ANDROID)
				set(ANDROID_API "")
				if(DEFINED ANDROID_PLATFORM AND NOT "${ANDROID_PLATFORM}" STREQUAL "")
					set(ANDROID_API "${ANDROID_PLATFORM}")
					string(REGEX REPLACE "^android-" "" ANDROID_API "${ANDROID_API}")
				elseif(DEFINED CMAKE_ANDROID_API AND NOT "${CMAKE_ANDROID_API}" STREQUAL "")
					set(ANDROID_API "${CMAKE_ANDROID_API}")
				elseif(DEFINED CMAKE_SYSTEM_VERSION AND NOT "${CMAKE_SYSTEM_VERSION}" STREQUAL "")
					set(ANDROID_API "${CMAKE_SYSTEM_VERSION}")
				endif()
				if(NOT ANDROID_API MATCHES "^[0-9]+$")
					set(ANDROID_API "21")
				endif()

				if(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
					set(MPEG_TARGET "armv7a-linux-androideabi")
				elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
					set(MPEG_TARGET "aarch64-linux-android")
				elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86")
					set(MPEG_TARGET "i686-linux-android")
				elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
					set(MPEG_TARGET "x86_64-linux-android")
				else()
					set(MPEG_TARGET "")
				endif()

				if(NOT "${MPEG_TARGET}" STREQUAL "")
					get_filename_component(_VGM_CC_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
					set(_VGM_CC_WRAPPER "${_VGM_CC_DIR}/${MPEG_TARGET}${ANDROID_API}-clang")
					if(EXISTS "${_VGM_CC_WRAPPER}")
						set(MPEG_CC "${_VGM_CC_WRAPPER}")
					else()
						set(MPEG_CC "${CMAKE_C_COMPILER}")
					endif()
					set(MPEG_CFLAGS "-fPIC")
				endif()
			endif()
			if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
				set(MPEG_CFLAGS "-arch ${CMAKE_OSX_ARCHITECTURES} -isysroot ${CMAKE_OSX_SYSROOT} -miphoneos-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET} -fPIC")
			endif()

			set(MPEG_ENV
				"${CMAKE_COMMAND}" -E env
				"CC=${MPEG_CC}"
				"AR=${CMAKE_AR}"
				"RANLIB=${CMAKE_RANLIB}"
			)
			if(MPEG_CFLAGS)
				list(APPEND MPEG_ENV "CFLAGS=${MPEG_CFLAGS}")
			endif()

			set(MPEG_CONFIGURE
				--enable-static
				--disable-shared
			)
			if(EMSCRIPTEN)
				list(APPEND MPEG_CONFIGURE
					--with-cpu=generic_fpu
				)
			endif()
			if(ANDROID)
				if(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
					set(MPEG_HOST "--host=arm-linux-androideabi")
				elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
					set(MPEG_HOST "--host=aarch64-linux-android")
				elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86")
					set(MPEG_HOST "--host=i686-linux-android")
				elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
					set(MPEG_HOST "--host=x86_64-linux-android")
				endif()
				
				list(APPEND MPEG_CONFIGURE
					${MPEG_HOST}
					--with-cpu=generic_fpu
					--with-sysroot=${CMAKE_SYSROOT}
				)
			endif()
			if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
				list(APPEND MPEG_CONFIGURE
					--host=aarch64-apple-darwin
					--with-cpu=generic_fpu
					--with-sysroot=${CMAKE_OSX_SYSROOT}
				)
			endif()
			
			file(MAKE_DIRECTORY ${MPEG_BIN})
			add_custom_target(MPEG_CONFIGURE
				COMMAND ${MPEG_ENV} "${MPEG_PATH}/configure" ${MPEG_CONFIGURE}
				DEPENDS ${MPEG_PATH}/configure
				BYPRODUCTS ${MPEG_BIN}/Makefile
				WORKING_DIRECTORY ${MPEG_BIN}
			)
			add_custom_target(MPEG_MAKE
				COMMAND make src/libmpg123/libmpg123.la
				DEPENDS ${MPEG_BIN}/Makefile
				BYPRODUCTS ${MPEG_LINK_PATH} ${MPEG_BIN}
				WORKING_DIRECTORY ${MPEG_BIN}
			)
			
			add_library(mpg123 STATIC IMPORTED)
			if(NOT EXISTS ${MPEG_LINK_PATH})
				add_dependencies(mpg123 MPEG_MAKE)
			endif()
			set_target_properties(mpg123 PROPERTIES
				IMPORTED_LOCATION ${MPEG_LINK_PATH}
			)
		endif()
	endif()
endif()
if(NOT USE_MPEG)
	unset(MPEG_SOURCE)
endif()
