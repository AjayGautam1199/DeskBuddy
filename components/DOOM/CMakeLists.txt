file(GLOB DoomSources *.c)
file(GLOB PlatformSources platform/*.c)

idf_component_register(SRCS ${DoomSources} ${PlatformSources}
                    INCLUDE_DIRS "include" "platform"
                    REQUIRES esp_lcd driver esp_timer)

# This is 1990s C (chocolate-doom derived) built against a project that
# defaults to -std=gnu23 -Wall -Werror. Force an older standard for this
# component only and relax the warnings that stock Doom source trips.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu99
    -Wno-error
    -Wno-char-subscripts
    -Wno-unused-value
    -Wno-parentheses
    -Wno-int-to-pointer-cast
    -Wno-pointer-sign
    -Wno-unused-but-set-parameter
    -Wno-unused-but-set-variable
    -Wno-maybe-uninitialized
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-const-variable
    -Wno-format-truncation
    -Wno-format
    -Wno-implicit-function-declaration
    -Wno-implicit-int
    -Wno-strict-prototypes
    -Wno-old-style-definition
    -Wno-incompatible-pointer-types
    -Wno-discarded-qualifiers
    -Wno-sign-compare
    -Wno-enum-conversion
)
