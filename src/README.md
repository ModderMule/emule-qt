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
./cmake-build-debug/tests/tst_KadSearch

Run a single test function within a suite:
./cmake-build-debug/tests/tst_KadSearch construct

List all available tests without running:
ctest --test-dir cmake-build-debug -N
