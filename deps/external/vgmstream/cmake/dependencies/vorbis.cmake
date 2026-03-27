if(NOT WIN32 AND USE_VORBIS)
	if(NOT VORBIS_PATH AND NOT ANDROID)
		find_package(VorbisFile QUIET)
		
		if(VORBISFILE_FOUND)
			set(VORBIS_SOURCE "(system)")
		endif()
	endif()
	if(VORBIS_PATH AND (OGG_PATH OR OGG_FOUND) OR ANDROID OR NOT VORBISFILE_FOUND)
		FetchDependency(VORBIS
			DIR vorbis
			GIT_REPOSITORY https://gitlab.xiph.org/xiph/vorbis
			GIT_TAG 2eac96b03ff67953354cb0a649c08aa3a23267ef
			GIT_UNSHALLOW ON
		)
		
		if(VORBIS_PATH)
			set(VORBIS_LINK_PATH ${VORBIS_BIN}/lib/libvorbis.a)
			set(VORBISFILE_LINK_PATH ${VORBIS_BIN}/lib/libvorbisfile.a)
			
			if(EXISTS ${VORBIS_LINK_PATH} AND EXISTS ${VORBISFILE_LINK_PATH})
				add_library(vorbis STATIC IMPORTED)
				set_target_properties(vorbis PROPERTIES
					IMPORTED_LOCATION ${VORBIS_LINK_PATH}
				)
				add_library(vorbisfile STATIC IMPORTED)
				set_target_properties(vorbisfile PROPERTIES
					IMPORTED_LOCATION ${VORBISFILE_LINK_PATH}
				)
			else()
				if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
					set(_VGMSTREAM_OLD_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
					set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
				endif()
				add_subdirectory(${VORBIS_PATH} ${VORBIS_BIN} EXCLUDE_FROM_ALL)
				if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
					set(BUILD_SHARED_LIBS "${_VGMSTREAM_OLD_BUILD_SHARED_LIBS}" CACHE BOOL "Build shared libraries" FORCE)
					unset(_VGMSTREAM_OLD_BUILD_SHARED_LIBS)
				endif()
			endif()
			
			set(OGG_VORBIS_INCLUDE_DIR ${VORBIS_PATH}/include)
			set(OGG_VORBIS_LIBRARY vorbis)
			
			set(VORBISFILE_INCLUDE_DIRS ${OGG_INCLUDE_DIR} ${OGG_VORBIS_INCLUDE_DIR})
		else()
			set_vorbis(OFF TRUE)
		endif()
	endif()
endif()
if(NOT USE_VORBIS)
	unset(VORBIS_SOURCE)
endif()
