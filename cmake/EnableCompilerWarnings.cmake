## Strict warning level
if (MSVC)
    # Use the highest warning level for visual studio.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /w")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /w")

else()
    foreach(COMPILER C CXX)
        set(CMAKE_COMPILER_WARNINGS)
        list(APPEND CMAKE_COMPILER_WARNINGS 
            -Wall
            -Wextra
            -Wcomment
            -Wendif-labels
            -Wformat
            -Winit-self
            -Wreturn-type
            -Wsequence-point
            #-Wshadow
            -Wswitch
            -Wtrigraphs
            -Wundef
            -Wuninitialized
            -Wunreachable-code
            -Wunused
            -Wno-sign-compare
        )
        if (CMAKE_${COMPILER}_COMPILER_ID MATCHES "Clang")
            list(APPEND CMAKE_COMPILER_WARNINGS
                # -Werror
                -Weverything
                 # absolutely need these :
                -Wno-exit-time-destructors
                -Wno-c++98-compat
                -Wno-c++98-compat-pedantic
                -Wno-padded
                -Wno-weak-vtables
                # these require very clean coding with clean deps:
                -Wno-sign-conversion
                #  -Wno-shorten-64-to-32
                # I like having a default in switch statement even 
                # if all cases are covered. This is because if 
                # someone adds a new case (new enum) which is 
                # not covered in the switch statement, default 
                # catches it. 
                 -Wno-covered-switch-default
            )
            
        else()
            list(APPEND CMAKE_COMPILER_WARNINGS
                -Wno-missing-field-initializers
                -Wno-deprecated-declarations
            )
        endif()
        add_definitions(${CMAKE_COMPILER_WARNINGS})
    endforeach()
endif ()
