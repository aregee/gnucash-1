# CMakeLists.txt for src/libqof

ADD_DEFINITIONS (-DG_LOG_DOMAIN=\"qof\")

INCLUDE_DIRECTORIES (${GLIB2_INCLUDE_DIRS})
INCLUDE_DIRECTORIES (${LIBINTL_INCLUDE_PATH})
INCLUDE_DIRECTORIES (${REGEX_INCLUDE_PATH})
INCLUDE_DIRECTORIES (${CMAKE_CURRENT_BINARY_DIR}/.. ) # for config.h
INCLUDE_DIRECTORIES (${CMAKE_SOURCE_DIR}/lib/libc) # for strptime.h
INCLUDE_DIRECTORIES (${CMAKE_SOURCE_DIR}/src) # for platform.h

SET (libgnc_qof_SOURCES
   qof/gnc-date.c
   qof/gnc-numeric.c
   qof/guid.c
   qof/kvp-util.c
   qof/kvp_frame.c
   qof/md5.c
   qof/qofbackend.c
   qof/qofbook.c
   qof/qofchoice.c
   qof/qofclass.c
   qof/qofevent.c
   qof/qofid.c
   qof/qofinstance.c
   qof/qoflog.c
   qof/qofobject.c
   qof/qofquery.c
   qof/qofquerycore.c
   qof/qofreference.c
   qof/qofsession.c
   qof/qofutil.c
)
IF (WIN32)
  ADD_DEFINITIONS (-DOS_WIN32)
  SET (libgnc_qof_SOURCES ${libgnc_qof_SOURCES}
    qof/qof-win32.c
	../../lib/libc/strptime.c
	../../lib/libc/localtime_r.c
	../../lib/libc/gmtime_r.c
  )
ENDIF (WIN32)

# Add dependency on config.h
SET_SOURCE_FILES_PROPERTIES (${libgnc_qof_SOURCES} PROPERTIES OBJECT_DEPENDS ${CONFIG_H})

SET (libgnc_qof_HEADERS
   qof/gnc-date-p.h
   qof/gnc-date.h
   qof/gnc-numeric.h
   qof/guid.h
   qof/kvp-util-p.h
   qof/kvp-util.h
   qof/kvp_frame.h
   qof/qof.h
   qof/qofbackend-p.h
   qof/qofbackend.h
   qof/qofbook.h
   qof/qofbookslots.h
   qof/qofchoice.h
   qof/qofclass.h
   qof/qofevent.h
   qof/qofid-p.h
   qof/qofid.h
   qof/qofinstance-p.h
   qof/qofinstance.h
   qof/qoflog.h
   qof/qofobject.h
   qof/qofquery.h
   qof/qofquerycore.h
   qof/qofreference.h
   qof/qofsession.h
   qof/qofutil.h
   qof/qof-gobject.h
)

ADD_LIBRARY	(qof
  ${libgnc_qof_SOURCES}
  ${libgnc_qof_HEADERS}
  )
