if(NOT WIN32 AND USE_VORBIS)
	if(NOT OGG_PATH AND NOT ANDROID)
		find_package(Ogg QUIET)
		
		if(OGG_FOUND)
			set(OGG_SOURCE "(system)")
		endif()
	endif()
	if(OGG_PATH OR ANDROID OR NOT OGG_FOUND)
		FetchDependency(OGG
			DIR ogg
			GIT_REPOSITORY https://gitlab.xiph.org/xiph/ogg
			GIT_TAG fa80aae9d50096160f2b56ada35527d7aee3f746
			GIT_UNSHALLOW ON
		)
		
		if(OGG_PATH)
			set(OGG_LINK_PATH ${OGG_BIN}/libogg.a)
			
			if(EXISTS ${OGG_LINK_PATH} AND EXISTS ${OGG_BIN}/include)
				add_library(ogg STATIC IMPORTED)
				set_target_properties(ogg PROPERTIES
					IMPORTED_LOCATION ${OGG_LINK_PATH}
				)
			else()
				if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
					set(_VGMSTREAM_OLD_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
					set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
				endif()
				add_subdirectory(${OGG_PATH} ${OGG_BIN} EXCLUDE_FROM_ALL)
				if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
					set(BUILD_SHARED_LIBS "${_VGMSTREAM_OLD_BUILD_SHARED_LIBS}" CACHE BOOL "Build shared libraries" FORCE)
					unset(_VGMSTREAM_OLD_BUILD_SHARED_LIBS)
				endif()
			endif()
			
			set(OGG_INCLUDE_DIR ${OGG_PATH}/include ${OGG_BIN}/include)
			set(OGG_LIBRARY ogg)
		endif()
	endif()
endif()
if(NOT OGG_PATH)
	unset(OGG_SOURCE)
endif()
