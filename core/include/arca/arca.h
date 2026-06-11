// arca.h — umbrella header for the ARCA core C ABI.
//
// The core is a native C++ library exposing this flat C ABI; UI shells
// (WinUI3 via P/Invoke, SwiftUI via direct C import) consume only the
// headers in this directory. See DECISIONS.md ADR-001.

#ifndef ARCA_H
#define ARCA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
  #if defined(ARCA_BUILD)
    #define ARCA_API __declspec(dllexport)
  #else
    #define ARCA_API __declspec(dllimport)
  #endif
#else
  #define ARCA_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define ARCA_VERSION_MAJOR 0
#define ARCA_VERSION_MINOR 1

typedef enum arca_status {
    ARCA_OK              = 0,
    ARCA_ERR_INVALID_ARG = -1,
    ARCA_ERR_ENGINE      = -2,  // the playback engine (libmpv) reported an error
    ARCA_ERR_GRAPHICS    = -3,  // D3D11 / DXGI failure
    ARCA_ERR_UNSUPPORTED = -4,
    ARCA_ERR_NOMEM       = -5,
} arca_status;

ARCA_API const char *arca_version_string(void);

// Frees any string returned by an arca_* function documented as caller-freed.
ARCA_API void arca_string_free(char *s);

#ifdef __cplusplus
}
#endif

#endif // ARCA_H
