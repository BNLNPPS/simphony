set(name QUDARap)

set(DEV_SOURCES
    QSim_cerenkov_dev.cu
)

set(SOURCES
    QUDARAP_LOG.cc

    QCurandStateMonolithic.cc
    QCurandStateMonolithic.cu
    QCurandState.cu


    QSim.cc
    QSim.cu


    QPMT.cc
    QPMT.cu 

    QEvt.cc
    QEvt.cu

    QDebug.cc
    QState.cc

    QU.cc

    QRng.cc  
    QRng.cu

    QTex.cc
    QTexRotate.cc
    QTex.cu
    QTexMaker.cc
    QTexLookup.cc
    QTexLookup.cu

    QBase.cc
    QBnd.cc
    QBnd.cu


    QOptical.cc
    QOptical.cu 

    QScint.cc
    QScint.cu

    QWls.cc

    QCerenkovIntegral.cc
    QCerenkov.cc
    QCerenkov.cu 

    QCK.cc
    QCK.hh

    QPoly.cc
    QPoly.cu

    QProp.cc
    QProp.cu

    QMultiFilm.cc
    QMultiFilm.cu

    QBuf.cc
)

SET(HEADERS
    QUDARAP_LOG.hh
    QUDARAP_API_EXPORT.hh
    QUDA_CHECK.h


    QCurandStateMonolithic.hh
    qcurandwrap.h


    QSim.hh
    qsim.h

    QPMT.hh
    qpmt.h

    qgs.h
    qprop.h
 
    QEvt.hh

    QDebug.hh
    qdebug.h

    QU.hh
    QRng.hh
    qrng.h

    QTex.hh
    QTexRotate.hh
    QTexMaker.hh
    QTexLookup.hh

    QState.hh

    qmultifilm.h 

    QBase.hh
    qbase.h 

    QBnd.hh
    qbnd.h



    QOptical.hh

    QScint.hh
    qscint.h

    QWls.hh
    qwls.h

    QCerenkovIntegral.hh
    QCerenkov.hh
    qcerenkov.h

    QPoly.hh

    QProp.hh
    
    QMultiFilm.hh

    QBuf.hh
)

add_library( ${name} ${SOURCES} )

get_target_property(ILL ${name} INTERFACE_LINK_LIBRARIES)
message(STATUS "${name} INTERFACE_LINK_LIBRARIES:${ILL} " )

find_package(OpenSSL REQUIRED)

target_link_libraries( ${name} SysRap
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_definitions( ${name} PUBLIC OPTICKS_QUDARAP DEBUG_TAG)

#[=[

WITH_THRUST compile definition
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* WITH_THRUST is needed for the GPU complex math functions used by qpmt.h

* The CPU side does not require WITH_THRUST, so keep the definition PRIVATE
  to avoid leaking it into dependents

#]=]

target_compile_definitions( ${name} PRIVATE WITH_THRUST )

target_include_directories( ${name} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/sysrap/inc>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/qudarap>
)

install(FILES ${HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/qudarap)

install(TARGETS ${name}
        EXPORT ${PROJECT_NAME}Targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
