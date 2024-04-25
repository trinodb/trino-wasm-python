#pragma once

#include <stdint.h>

// Trino types
static const int NUMERIC_VALUE_OUT_OF_RANGE = 19;
static const int EXCEEDED_FUNCTION_MEMORY_LIMIT = 37;
static const int FUNCTION_IMPLEMENTATION_ERROR = 65549;

typedef enum
{
    ROW = 0, // field count, field types
    ARRAY = 1, // element type
    MAP = 2, // key type, value type
    BOOLEAN = 3,
    BIGINT = 4,
    INTEGER = 5,
    SMALLINT = 6,
    TINYINT = 7,
    DOUBLE = 8,
    REAL = 9,
    DECIMAL = 10,
    VARCHAR = 11,
    VARBINARY = 12,
    DATE = 13,
    TIME = 14,
    TIME_WITH_TIME_ZONE = 15,
    TIMESTAMP = 16,
    TIMESTAMP_WITH_TIME_ZONE = 17,
    INTERVAL_YEAR_TO_MONTH = 18,
    INTERVAL_DAY_TO_SECOND = 19,
    JSON = 20,
    UUID = 21,
    IPADDRESS = 22,
} TrinoType;

// WebAssembly types
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;
typedef double f64;

// WebAssembly functions
__attribute__((export_name("allocate"))) u8* allocate(i32 size);
__attribute__((export_name("deallocate"))) void deallocate(u8* pointer);

__attribute__((export_name("setup"))) void setup(
    const u8* functionName, const u8* argType, const u8* returnType);

__attribute__((export_name("execute"))) u8* execute(const u8* data);

__attribute__((import_module("trino"), import_name("return_error"))) void trinoReturnError(
    i32 errorCode, const u8* message, i32 messageSize, const u8* traceback, i32 tracebackSize);
