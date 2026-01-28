# Testing Patterns

**Analysis Date:** 2026-01-28

## Test Framework

**Runner:**
- Planned: Catch2 v3.0.0 (referenced in `docs/REFACTORING_PLAN.md`)
- Current: No active test runner detected in `src/`

**Assertion Library:**
- Planned: Catch2 (will provide `REQUIRE`, `CHECK`, etc.)

**Run Commands:**
```bash
# Planned commands (from docs/REFACTORING_PLAN.md)
cd tests
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(sysctl -n hw.ncpu)
./videoTracker_tests
```

## Test File Organization

**Location:**
- Planned: `tests/` directory at the project root
- Subdirectories for specific categories: `tests/test_core/`, `tests/test_modules/`, `tests/test_threading/`

**Naming:**
- Planned: `*_test.cpp` (e.g., `Engine_test.cpp`, `Module_test.cpp`)

**Structure:**
```
videoTracker/
  tests/
    CMakeLists.txt
    test_main.cpp
    test_core/
      Engine_test.cpp
      LuaGlobals_test.cpp
    test_modules/
      Module_test.cpp
      ParameterRouter_test.cpp
    test_threading/
      ThreadSafety_test.cpp
```

## Test Structure

**Suite Organization:**
```typescript
// Planned pattern using Catch2
TEST_CASE("Module registration", "[core][registry]") {
    ModuleRegistry registry;
    auto module = std::make_shared<MockModule>();
    
    SECTION("Registering a valid module") {
        REQUIRE(registry.registerModule("uuid-1", module, "TestModule") == true);
    }
}
```

**Patterns:**
- `TEST_CASE` for major features
- `SECTION` for specific scenarios within a test case
- `REQUIRE` for critical assertions (stops test)
- `CHECK` for non-critical assertions (continues test)

## Mocking

**Framework:** Custom Mocks / Standard C++

**Patterns:**
- Interface-based mocking: `Module` is an abstract base class, allowing for `MockModule` in tests
- Dependency Injection: Controllers (like `SessionManager`) accept pointers to their dependencies, enabling the use of mocks/fakes in tests

**What to Mock:**
- Hardware-dependent components (Audio/Video drivers)
- External file systems (for IO tests)
- Time-dependent components (using manual `Clock` updates)

**What NOT to Mock:**
- Core data structures (`ParameterPath`, `Pattern`)
- Utility functions (`ExpressionParser`)

## Fixtures and Factories

**Test Data:**
- Sample session files (JSON format)
- Waveform and video asset samples for testing imports

**Location:**
- `bin/data/` for application data
- Planned `tests/fixtures/` for test-specific data

## Coverage

**Requirements:**
- Targeted coverage for core components (from `docs/REFACTORING_PLAN.md`):
  - Core atomic operations: 100%
  - Thread-safety patterns: 100%
  - Module parameter access: >90%
  - Parameter routing: >90%

**View Coverage:**
- Planned: Integration with CI/CD pipeline using standard coverage tools (e.g., `gcov`, `lcov`)

## Test Types

**Unit Tests:**
- Focused on individual classes: `ModuleRegistry`, `ParameterPath`, `ExpressionParser`
- No hardware dependencies

**Integration Tests:**
- Testing interaction between systems: `SessionManager` + `ModuleRegistry` + `ConnectionManager`
- Testing session save/load cycles

**E2E Tests:**
- Planned: `imgui_test_engine` for GUI interaction testing
- Full application startup and basic workflow verification

## Common Patterns

**Async Testing:**
- Manual clock stepping for timing-dependent modules
- Mutex/Condition Variable synchronization for threading tests

**Error Testing:**
- Verification of error logs using mock loggers
- Testing of `try-catch` boundaries in `ofApp`

---

*Testing analysis: 2026-01-28*
