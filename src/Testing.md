# EmuleQt

## Testing
cmake --build cmake-build-debug && cd cmake-build-debug && ctest

Or build just the specific test target:

cmake --build cmake-build-debug --target tst_ArchiveRecovery




All tests:                                            
ctest --test-dir cmake-build-debug

With verbose output:                   
ctest --test-dir cmake-build-debug --output-on-failure

A specific test by name:      
ctest --test-dir cmake-build-debug -R tst_KadSearch

Multiple tests matching a pattern (e.g. all Kad tests):                                                                                                                                                                                                                                                                                                                          
ctest --test-dir cmake-build-debug -R Kad

Run a test executable directly (more verbose):
./cmake-build-debug/tests/tst_KadSearch -maxwarnings 50000
EMULE_TCP_PORT=5662 EMULE_UDP_PORT=5672 cmake --build build --target tst_KadLiveNetwork 2>&1 && build/tests/tst_KadLiveNetwork -maxwarnings 50000
bootstrap_connectsToNetwork
-v2 # Qt more verbose

Run a single test function within a suite:
./cmake-build-debug/tests/tst_KadSearch construct

List all available tests without running:
ctest --test-dir cmake-build-debug -N

cmake --build build --target emulecored 2>&1 && ./build/src/daemon/emulecored

# debug build
cmake --build build --target emulecored --config Debug 2>&1 | tail -3 && lldb -o run -o 'bt all' -o quit -- ./build/src/daemon/emulecored

EMULE_TCP_PORT=5662 EMULE_UDP_PORT=5672 ./build/tests/tst_KadLiveNetwork
