# ---------------------------------------------------------------------------
# emuleqt-mcp — MCP stdio bridge
#
# For MCP clients that only launch a local program: reads JSON-RPC lines on
# stdin, forwards each to the daemon's /mcp endpoint, writes the replies to
# stdout. Links eMule::Core for the config directory lookup only.
# ---------------------------------------------------------------------------

qt_add_executable(emuleqt-mcp
    main.cpp
    McpStdioBridge.cpp
    McpStdioBridge.h
)

target_link_libraries(emuleqt-mcp
    PRIVATE
        eMule::Core
        eMule::CommonSettings
        Qt6::Network
)

set_target_properties(emuleqt-mcp PROPERTIES
    FOLDER "Daemon"
)
