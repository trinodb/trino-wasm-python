#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <datetime.h>

#include "pyhost.h"

const static i64 MICROSECONDS = 1000 * 1000;

#ifdef NDEBUG
#define DEBUG(format, ...)
#else
#define DEBUG(format, ...) \
    fprintf(stdout, format "\n" __VA_OPT__(, ) __VA_ARGS__)
#endif

static PyObject* emptyTuple;

static PyObject* decimalModule;
static PyObject* decimalClass;

static PyObject* uuidModule;
static PyObject* uuidClass;

static PyObject* ipaddressModule;
static PyObject* ipaddressV4Class;
static PyObject* ipaddressV6Class;

static PyObject* trinoModule;
static PyObject* guestModule;

static PyObject* trinoErrorResultFunction;
static PyObject* decimalToStringFunction;
static PyObject* guestFunction;

static const u8* trinoArgType;
static const u8* trinoReturnType;

static PyObject* loadModule(const char* name);
static PyObject* findFunction(PyObject* module, const char* name);

__attribute__((noreturn)) static void FATAL(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

static void* xrealloc(void* ptr, const size_t size)
{
    void* result = realloc(ptr, size);
    if (result == NULL) {
        FATAL("Failed to allocate %lu bytes", size);
    }
    return result;
}

static PyObject* checked(PyObject* value)
{
    if (value == NULL) {
        PyErr_Print();
        FATAL("Failed to get Python object");
    }
    return value;
}

static bool checkType(PyObject* obj, PyTypeObject* expected)
{
    if (!PyObject_TypeCheck(obj, expected)) {
        PyErr_Format(PyExc_TypeError, "expected an instance of type '%s'", expected->tp_name);
        return false;
    }
    return true;
}

static i8 readI8(const u8** const data)
{
    const i8 value = *(const i8*)*data;
    *data += sizeof(i8);
    return value;
}

static i16 readI16(const u8** const data)
{
    const i16 value = *(const i16*)*data;
    *data += sizeof(i16);
    return value;
}

static i32 readI32(const u8** const data)
{
    const i32 value = *(const i32*)*data;
    *data += sizeof(i32);
    return value;
}

static i64 readI64(const u8** const data)
{
    const i64 value = *(const i64*)*data;
    *data += sizeof(i64);
    return value;
}

// skip type at the current position in the type descriptor
static void skipType(const u8** const type)
{
    const TrinoType trinoType = readI32(type);
    DEBUG("skipType: type=%d", trinoType);

    switch (trinoType) {
        case ROW: {
            const i32 count = readI32(type);
            for (i32 i = 0; i < count; i++) {
                skipType(type);
            }
            break;
        }
        case ARRAY:
            skipType(type);
            break;
        case MAP:
            skipType(type);
            skipType(type);
            break;
        case BOOLEAN:
        case BIGINT:
        case INTEGER:
        case SMALLINT:
        case TINYINT:
        case DOUBLE:
        case REAL:
        case DECIMAL:
        case VARCHAR:
        case VARBINARY:
        case DATE:
        case TIME:
        case TIME_WITH_TIME_ZONE:
        case TIMESTAMP:
        case TIMESTAMP_WITH_TIME_ZONE:
        case INTERVAL_YEAR_TO_MONTH:
        case INTERVAL_DAY_TO_SECOND:
        case JSON:
        case UUID:
        case IPADDRESS:
            break;
        default:
            FATAL("Unsupported Trino type %d", trinoType);
    }
}

static PyObject* doBuildArgs(const u8** const type, const u8** const data)
{
    const bool present = readI8(data);
    if (!present) {
        DEBUG("buildArgs: present=false");
        skipType(type);
        return Py_None;
    }

    const TrinoType trinoType = readI32(type);
    DEBUG("buildArgs: type=%d", trinoType);

    switch (trinoType) {
        case ROW: {
            const i32 count = readI32(type);
            DEBUG("buildArgs: fieldCount=%d", count);
            PyObject* tuple = checked(PyTuple_New(count));
            for (i32 i = 0; i < count; i++) {
                PyObject* value = doBuildArgs(type, data);
                PyTuple_SET_ITEM(tuple, i, value);
            }
            return tuple;
        }
        case ARRAY: {
            const u8* savedType = *type;
            const i32 count = readI32(data);
            DEBUG("buildArgs: elementCount=%d", count);
            PyObject* list = checked(PyList_New(count));
            for (i32 i = 0; i < count; i++) {
                *type = savedType;
                PyObject* value = doBuildArgs(type, data);
                PyList_SET_ITEM(list, i, value);
            }
            if (count == 0) {
                skipType(type);
            }
            return list;
        }
        case MAP: {
            const u8* savedType = *type;
            const i32 count = readI32(data);
            DEBUG("buildArgs: entryCount=%d", count);
            PyObject* dict = checked(PyDict_New());
            for (i32 i = 0; i < count; i++) {
                *type = savedType;
                PyObject* key = doBuildArgs(type, data);
                PyObject* value = doBuildArgs(type, data);
                if (PyDict_SetItem(dict, key, value) == -1) {
                    PyErr_Print();
                    FATAL("Failed to set dictionary item");
                }
                Py_DECREF(key);
                Py_DECREF(value);
            }
            return dict;
        }
        case BOOLEAN: {
            const bool value = readI8(data);
            return value ? Py_True : Py_False;
        }
        case BIGINT: {
            const i64 value = readI64(data);
            return checked(PyLong_FromLongLong(value));
        }
        case INTEGER: {
            const i32 value = readI32(data);
            return checked(PyLong_FromLong(value));
        }
        case SMALLINT: {
            const i16 value = readI16(data);
            return checked(PyLong_FromLong(value));
        }
        case TINYINT: {
            const i8 value = readI8(data);
            return checked(PyLong_FromLong(value));
        }
        case DOUBLE: {
            const f64 value = *(const f64*)*data;
            *data += sizeof(f64);
            return checked(PyFloat_FromDouble(value));
        }
        case REAL: {
            const f32 value = *(const f32*)*data;
            *data += sizeof(f32);
            return checked(PyFloat_FromDouble(value));
        }
        case DECIMAL: {
            const i32 size = readI32(data);
            PyObject* number = checked(PyUnicode_FromStringAndSize((const char*)*data, size));
            *data += size;
            PyObject* value = checked(PyObject_CallOneArg(decimalClass, number));
            Py_DECREF(number);
            return value;
        }
        case VARCHAR:
        case JSON: {
            const i32 size = readI32(data);
            PyObject* value = checked(PyUnicode_FromStringAndSize((const char*)*data, size));
            *data += size;
            return value;
        }
        case VARBINARY: {
            const i32 size = readI32(data);
            PyObject* value = checked(PyBytes_FromStringAndSize((const char*)*data, size));
            *data += size;
            return value;
        }
        case DATE: {
            const i32 days = readI32(data);
            const time_t time = days * (24 * 60 * 60);
            const struct tm* t = gmtime(&time);
            return checked(PyDate_FromDate(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday));
        }
        case TIME: {
            const i64 time = readI64(data);
            const int hour = time / (60 * 60 * MICROSECONDS);
            const int minute = time / (60 * MICROSECONDS) % 60;
            const int second = time / MICROSECONDS % 60;
            const int usecond = time % MICROSECONDS;
            return checked(PyTime_FromTime(hour, minute, second, usecond));
        }
        case TIME_WITH_TIME_ZONE: {
            const i64 time = readI64(data);
            const i16 offset = readI16(data);
            const int hour = time / (60 * 60 * MICROSECONDS);
            const int minute = time / (60 * MICROSECONDS) % 60;
            const int second = time / MICROSECONDS % 60;
            const int usecond = time % MICROSECONDS;
            PyObject* delta = checked(PyDelta_FromDSU(0, offset * 60, 0));
            PyObject* tz = checked(PyTimeZone_FromOffset(delta));
            return checked(PyDateTimeAPI->Time_FromTime(
                hour, minute, second, usecond, tz, PyDateTimeAPI->TimeType));
        }
        case TIMESTAMP: {
            const i64 ts = readI64(data);
            const time_t time = ts / MICROSECONDS;
            const struct tm* t = gmtime(&time);
            const int year = t->tm_year + 1900;
            const int month = t->tm_mon + 1;
            const int day = t->tm_mday;
            const int hour = t->tm_hour;
            const int minute = t->tm_min;
            const int second = t->tm_sec;
            const int usecond = ts % MICROSECONDS;
            return checked(PyDateTime_FromDateAndTime(year, month, day, hour, minute, second, usecond));
        }
        case TIMESTAMP_WITH_TIME_ZONE: {
            const i64 ts = readI64(data);
            const i16 offset = readI16(data);
            const time_t time = ts / MICROSECONDS + offset * 60;
            const struct tm* t = gmtime(&time);
            const int year = t->tm_year + 1900;
            const int month = t->tm_mon + 1;
            const int day = t->tm_mday;
            const int hour = t->tm_hour;
            const int minute = t->tm_min;
            const int second = t->tm_sec;
            const int usecond = ts % MICROSECONDS;
            PyObject* delta = checked(PyDelta_FromDSU(0, offset * 60, 0));
            PyObject* tz = checked(PyTimeZone_FromOffset(delta));
            return checked(PyDateTimeAPI->DateTime_FromDateAndTime(
                year, month, day, hour, minute, second, usecond, tz, PyDateTimeAPI->DateTimeType));
        }
        case INTERVAL_YEAR_TO_MONTH: {
            const i32 months = readI32(data);
            return checked(PyLong_FromLong(months));
        }
        case INTERVAL_DAY_TO_SECOND: {
            const i64 millis = readI64(data);
            const int days = millis / (24 * 60 * 60 * 1000);
            const int seconds = (millis / 1000) % (24 * 60 * 60);
            const int micros = (millis % 1000) * 1000;
            return checked(PyDelta_FromDSU(days, seconds, micros));
        }
        case UUID: {
            PyObject* bytes = checked(PyBytes_FromStringAndSize((const char*)*data, 16));
            *data += 16;
            PyObject* kwArgs = checked(PyDict_New());
            if (PyDict_SetItemString(kwArgs, "bytes", bytes) == -1) {
                PyErr_Print();
                FATAL("Failed to set dictionary item");
            }
            PyObject* value = checked(PyObject_Call(uuidClass, emptyTuple, kwArgs));
            Py_DECREF(kwArgs);
            Py_DECREF(bytes);
            return value;
        }
        case IPADDRESS: {
            const u32* raw = (u32*)*data;
            PyObject* bytes;
            PyObject* value;
            if (raw[0] == 0x00000000 && raw[1] == 0x00000000 && raw[2] == 0xFFFF0000) {
                bytes = checked(PyBytes_FromStringAndSize((const char*)(*data + 12), 4));
                value = checked(PyObject_CallOneArg(ipaddressV4Class, bytes));
            }
            else {
                bytes = checked(PyBytes_FromStringAndSize((const char*)*data, 16));
                value = checked(PyObject_CallOneArg(ipaddressV6Class, bytes));
            }
            *data += 16;
            Py_DECREF(bytes);
            return value;
        }
    }
    FATAL("Unsupported Trino type %d", trinoType);
}

static PyObject* buildArgs(const u8* data)
{
    const u8* type = trinoArgType;
    return doBuildArgs(&type, &data);
}

static void resultError(PyObject* resultValue, const char* trinoType)
{
    char* message;
    asprintf(&message, "Failed to convert Python result type '%s' to Trino type %s",
             Py_TYPE(resultValue)->tp_name, trinoType);
    if (message == NULL) {
        FATAL("Failed to allocate memory for error message");
    }

    PyObject* exception = PyErr_GetRaisedException();
    if (exception == NULL) {
        FATAL("Python exception not raised for value conversion failure: %s", message);
    }

    PyObject* exceptionStr = PyObject_Str(exception);
    if (exceptionStr == NULL) {
        FATAL("Failed to convert Python exception to string");
    }

    const char* string = PyUnicode_AsUTF8(exceptionStr);
    if (string == NULL) {
        FATAL("Failed to get Python exception string");
    }

    char* error;
    asprintf(&error, "%s: %s: %s", message, Py_TYPE(exception)->tp_name, string);
    if (error == NULL) {
        FATAL("Failed to allocate memory for error message");
    }
    free(message);

    Py_DECREF(exceptionStr);
    Py_DECREF(exception);

    trinoReturnError(FUNCTION_IMPLEMENTATION_ERROR, (u8*)error, strlen(error), NULL, 0);

    free(error);
}

typedef struct
{
    u8* data;
    i32 size;
    i32 used;
} Buffer;

static void bufferReserve(Buffer* buffer, const i32 required)
{
    if (buffer->size < required) {
        do {
            buffer->size *= 2;
        }
        while (buffer->size < required);
        buffer->data = xrealloc(buffer->data, buffer->size);
    }
}

static void bufferAppend(Buffer* buffer, const u8* data, const i32 size)
{
    bufferReserve(buffer, buffer->used + size);
    memcpy(buffer->data + buffer->used, data, size);
    buffer->used += size;
}

static void bufferAppendI8(Buffer *buffer, const i8 value)
{
    bufferAppend(buffer, (u8*)&value, sizeof(i8));
}

static void bufferAppendI16(Buffer *buffer, const i16 value)
{
    bufferAppend(buffer, (u8*)&value, sizeof(i16));
}

static void bufferAppendI32(Buffer *buffer, const i32 value)
{
    bufferAppend(buffer, (u8*)&value, sizeof(i32));
}

static void bufferAppendI64(Buffer *buffer, const i64 value)
{
    bufferAppend(buffer, (u8*)&value, sizeof(i64));
}

static void overflowError(const char* message)
{
    trinoReturnError(NUMERIC_VALUE_OUT_OF_RANGE, (u8*)message, strlen(message), NULL, 0);
}

static void memoryError()
{
    const char* message = "Python MemoryError (no traceback available)";
    trinoReturnError(EXCEEDED_FUNCTION_MEMORY_LIMIT, (u8*)message, strlen(message), NULL, 0);
}

static bool appendBytesAttr(PyObject* input, Buffer* buffer, const char* attr, const char* trinoType)
{
    PyObject* bytes = PyObject_GetAttrString(input, attr);
    if (bytes == NULL) {
        resultError(input, trinoType);
        return false;
    }
    Py_ssize_t size;
    char* data;
    if (PyBytes_AsStringAndSize(bytes, &data, &size) == -1) {
        Py_DECREF(bytes);
        resultError(input, trinoType);
        return false;
    }
    bufferAppend(buffer, (u8*)data, size);
    Py_DECREF(bytes);
    return true;
}

static bool buildResult(const u8** const type, PyObject* input, Buffer* buffer)
{
    bool present = input != Py_None;
    bufferAppendI8(buffer, present);
    if (!present) {
        DEBUG("buildResult: present=false");
        skipType(type);
        return true;
    }

    const TrinoType trinoType = readI32(type);
    DEBUG("buildResult: type=%d", trinoType);

    switch (trinoType) {
        case ROW: {
            if (!checkType(input, &PyTuple_Type)) {
                resultError(input, "ROW");
                return false;
            }
            const i32 count = readI32(type);
            if (PyTuple_Size(input) != count) {
                PyErr_Format(PyExc_ValueError, "tuple has %d fields, expected %d fields for row",
                             PyTuple_Size(input), count);
                resultError(input, "ROW");
                return false;
            }
            for (i32 i = 0; i < count; i++) {
                if (!buildResult(type, PyTuple_GetItem(input, i), buffer)) {
                    return false;
                }
            }
            return true;
        }
        case ARRAY: {
            if (!checkType(input, &PyList_Type)) {
                resultError(input, "ARRAY");
                return false;
            }
            const u8* savedType = *type;
            const i32 size = PyList_Size(input);
            bufferAppendI32(buffer, size);
            for (i32 i = 0; i < size; i++) {
                *type = savedType;
                if (!buildResult(type, PyList_GetItem(input, i), buffer)) {
                    return false;
                }
            }
            return true;
        }
        case MAP: {
            if (!checkType(input, &PyDict_Type)) {
                resultError(input, "MAP");
                return false;
            }
            const u8* savedType = *type;
            const i32 size = PyDict_Size(input);
            bufferAppendI32(buffer, size);
            PyObject* key;
            PyObject* value;
            Py_ssize_t pos = 0;
            while (PyDict_Next(input, &pos, &key, &value)) {
                *type = savedType;
                if (!buildResult(type, key, buffer)) {
                    return false;
                }
                if (!buildResult(type, value, buffer)) {
                    return false;
                }
            }
            return true;
        }
        case BOOLEAN: {
            int value = PyObject_IsTrue(input);
            if (value == -1) {
                resultError(input, "BOOLEAN");
                return false;
            }
            bufferAppendI8(buffer, value);
            return true;
        }
        case BIGINT: {
            int overflow;
            const i64 value = PyLong_AsLongLongAndOverflow(input, &overflow);
            if (value == -1 && PyErr_Occurred()) {
                resultError(input, "BIGINT");
                return false;
            }
            if (overflow) {
                overflowError("Value out of range for BIGINT");
                return false;
            }
            bufferAppendI64(buffer, value);
            return true;
        }
        case INTEGER: {
            int overflow;
            const i32 value = PyLong_AsLongAndOverflow(input, &overflow);
            if (value == -1 && PyErr_Occurred()) {
                resultError(input, "INTEGER");
                return false;
            }
            if (overflow) {
                overflowError("Value out of range for INTEGER");
                return false;
            }
            bufferAppendI32(buffer, value);
            return true;
        }
        case SMALLINT: {
            int overflow;
            const i32 value = PyLong_AsLongAndOverflow(input, &overflow);
            if (value == -1 && PyErr_Occurred()) {
                resultError(input, "SMALLINT");
                return false;
            }
            if (overflow || value < INT16_MIN || value > INT16_MAX) {
                overflowError("Value out of range for SMALLINT");
                return false;
            }
            bufferAppendI16(buffer, value);
            return true;
        }
        case TINYINT: {
            int overflow;
            const i32 value = PyLong_AsLongAndOverflow(input, &overflow);
            if (value == -1 && PyErr_Occurred()) {
                resultError(input, "TINYINT");
                return false;
            }
            if (overflow || value < INT8_MIN || value > INT8_MAX) {
                overflowError("Value out of range for TINYINT");
                return false;
            }
            bufferAppendI8(buffer, value);
            return true;
        }
        case DOUBLE: {
            const f64 value = PyFloat_AsDouble(input);
            if (value == -1.0 && PyErr_Occurred()) {
                resultError(input, "DOUBLE");
                return false;
            }
            bufferAppend(buffer, (u8*)&value, sizeof(f64));
            return true;
        }
        case REAL: {
            const f32 value = PyFloat_AsDouble(input);
            if (value == -1.0 && PyErr_Occurred()) {
                resultError(input, "REAL");
                return false;
            }
            bufferAppend(buffer, (u8*)&value, sizeof(f32));
            return true;
        }
        case DECIMAL: {
            PyObject* string = PyObject_CallOneArg(decimalToStringFunction, input);
            if (string == NULL) {
                resultError(input, "DECIMAL");
                return false;
            }
            Py_ssize_t size;
            const char* value = PyUnicode_AsUTF8AndSize(string, &size);
            if (value == NULL) {
                Py_DECREF(string);
                resultError(input, "DECIMAL");
                return false;
            }
            bufferAppendI32(buffer, size);
            bufferAppend(buffer, (u8*)value, size);
            Py_DECREF(string);
            return true;
        }
        case VARCHAR:
        case JSON: {
            const char* typeName = trinoType == VARCHAR ? "VARCHAR" : "JSON";
            if (!checkType(input, &PyUnicode_Type)) {
                resultError(input, typeName);
                return false;
            }
            Py_ssize_t size;
            const char* value = PyUnicode_AsUTF8AndSize(input, &size);
            if (value == NULL) {
                resultError(input, typeName);
                return false;
            }
            bufferAppendI32(buffer, size);
            bufferAppend(buffer, (u8*)value, size);
            return true;
        }
        case VARBINARY: {
            Py_buffer view;
            if (PyObject_GetBuffer(input, &view, PyBUF_SIMPLE) == -1) {
                resultError(input, "VARBINARY");
                return false;
            }
            bufferAppendI32(buffer, view.len);
            bufferAppend(buffer, view.buf, view.len);
            PyBuffer_Release(&view);
            return true;
        }
        case DATE: {
            if (!checkType(input, PyDateTimeAPI->DateType)) {
                resultError(input, "DATE");
                return false;
            }
            struct tm t = {
                .tm_year = PyDateTime_GET_YEAR(input) - 1900,
                .tm_mon = PyDateTime_GET_MONTH(input) - 1,
                .tm_mday = PyDateTime_GET_DAY(input),
            };
            const i32 days = timegm(&t) / (24 * 60 * 60);
            bufferAppendI32(buffer, days);
            return true;
        }
        case TIME: {
            if (!checkType(input, PyDateTimeAPI->TimeType)) {
                resultError(input, "TIME");
                return false;
            }
            const i64 micros =
                PyDateTime_TIME_GET_HOUR(input) * (60 * 60 * MICROSECONDS) +
                PyDateTime_TIME_GET_MINUTE(input) * (60 * MICROSECONDS) +
                PyDateTime_TIME_GET_SECOND(input) * MICROSECONDS +
                PyDateTime_TIME_GET_MICROSECOND(input);
            bufferAppendI64(buffer, micros);
            return true;
        }
        case TIME_WITH_TIME_ZONE: {
            if (!checkType(input, PyDateTimeAPI->TimeType)) {
                resultError(input, "TIME WITH TIME ZONE");
                return false;
            }
            const i64 micros =
                PyDateTime_TIME_GET_HOUR(input) * (60 * 60 * MICROSECONDS) +
                PyDateTime_TIME_GET_MINUTE(input) * (60 * MICROSECONDS) +
                PyDateTime_TIME_GET_SECOND(input) * MICROSECONDS +
                PyDateTime_TIME_GET_MICROSECOND(input);
            bufferAppendI64(buffer, micros);
            PyObject* delta = PyObject_CallMethod(input, "utcoffset", NULL);
            if (delta == NULL || delta == Py_None) {
                if (delta == Py_None) {
                    PyErr_Format(PyExc_ValueError, "time instance does not have tzinfo");
                }
                resultError(input, "TIME WITH TIME ZONE");
                return false;
            }
            const i16 offset =
                PyDateTime_DELTA_GET_DAYS(delta) * 24 * 60 +
                PyDateTime_DELTA_GET_SECONDS(delta) / 60;
            Py_DECREF(delta);
            bufferAppendI16(buffer, offset);
            return true;
        }
        case TIMESTAMP: {
            if (!checkType(input, PyDateTimeAPI->DateTimeType)) {
                resultError(input, "TIMESTAMP");
                return false;
            }
            struct tm t = {
                .tm_year = PyDateTime_GET_YEAR(input) - 1900,
                .tm_mon = PyDateTime_GET_MONTH(input) - 1,
                .tm_mday = PyDateTime_GET_DAY(input),
                .tm_hour = PyDateTime_DATE_GET_HOUR(input),
                .tm_min = PyDateTime_DATE_GET_MINUTE(input),
                .tm_sec = PyDateTime_DATE_GET_SECOND(input),
            };
            i64 micros = timegm(&t) * MICROSECONDS;
            micros += PyDateTime_DATE_GET_MICROSECOND(input);
            bufferAppendI64(buffer, micros);
            return true;
        }
        case TIMESTAMP_WITH_TIME_ZONE: {
            if (!checkType(input, PyDateTimeAPI->DateTimeType)) {
                resultError(input, "TIMESTAMP WITH TIME ZONE");
                return false;
            }
            struct tm t = {
                .tm_year = PyDateTime_GET_YEAR(input) - 1900,
                .tm_mon = PyDateTime_GET_MONTH(input) - 1,
                .tm_mday = PyDateTime_GET_DAY(input),
                .tm_hour = PyDateTime_DATE_GET_HOUR(input),
                .tm_min = PyDateTime_DATE_GET_MINUTE(input),
                .tm_sec = PyDateTime_DATE_GET_SECOND(input),
            };
            i64 micros = timegm(&t) * MICROSECONDS;
            micros += PyDateTime_DATE_GET_MICROSECOND(input);
            PyObject* delta = PyObject_CallMethod(input, "utcoffset", NULL);
            if (delta == NULL || delta == Py_None) {
                if (delta == Py_None) {
                    PyErr_Format(PyExc_ValueError, "datetime instance does not have tzinfo");
                }
                resultError(input, "TIMESTAMP WITH TIME ZONE");
                return false;
            }
            const i16 offset =
                PyDateTime_DELTA_GET_DAYS(delta) * 24 * 60 +
                PyDateTime_DELTA_GET_SECONDS(delta) / 60;
            micros -= offset * 60 * MICROSECONDS;
            bufferAppendI64(buffer, micros);
            bufferAppendI16(buffer, offset);
            Py_DECREF(delta);
            return true;
        }
        case INTERVAL_YEAR_TO_MONTH: {
            int overflow;
            const i32 value = PyLong_AsLongAndOverflow(input, &overflow);
            if (value == -1 && PyErr_Occurred()) {
                resultError(input, "INTERVAL YEAR TO MONTH");
                return false;
            }
            if (overflow) {
                overflowError("Value out of range for INTERVAL YEAR TO MONTH");
                return false;
            }
            bufferAppendI32(buffer, value);
            return true;
        }
        case INTERVAL_DAY_TO_SECOND: {
            if (!checkType(input, PyDateTimeAPI->DeltaType)) {
                resultError(input, "INTERVAL DAY TO SECOND");
                return false;
            }
            const i64 value =
                PyDateTime_DELTA_GET_DAYS(input) * (24 * 60 * 60 * 1000) +
                PyDateTime_DELTA_GET_SECONDS(input) * 1000 +
                (PyDateTime_DELTA_GET_MICROSECONDS(input) + 500) / 1000;
            bufferAppendI64(buffer, value);
            return true;
        }
        case UUID: {
            if (!checkType(input, _PyType_CAST(uuidClass))) {
                resultError(input, "UUID");
                return false;
            }
            return appendBytesAttr(input, buffer, "bytes", "UUID");
        }
        case IPADDRESS: {
            if (PyObject_IsInstance(input, ipaddressV4Class) == 1) {
                input = PyObject_GetAttrString(input, "ipv6_mapped");
                if (input == NULL) {
                    resultError(input, "IPADDRESS");
                    return false;
                }
            }
            if (PyObject_IsInstance(input, ipaddressV6Class) != 1) {
                PyErr_Format(PyExc_TypeError, "expected an instance of type '%N' or '%N'",
                             ipaddressV4Class, ipaddressV6Class);
                resultError(input, "IPADDRESS");
                return false;
            }
            return appendBytesAttr(input, buffer, "packed", "IPADDRESS");
        }
    }
    FATAL("Unsupported Trino type %d", trinoType);
}

static void handleTrinoError(PyObject* exception)
{
    if (exception == NULL) {
        FATAL("Python exception not raised for function failure");
    }

    PyObject* error = PyObject_CallOneArg(trinoErrorResultFunction, exception);
    if (error == NULL) {
        if (PyErr_ExceptionMatches(PyExc_MemoryError)) {
            memoryError();
            return;
        }
        PyErr_Print();
        FATAL("Failed to convert Python exception to Trino error");
    }

    PyObject* errorCodeObject = PyTuple_GetItem(error, 0);
    if (errorCodeObject == NULL) {
        PyErr_Print();
        FATAL("Failed to get error code from Trino error");
    }
    const i32 errorCode = PyLong_AsLong(errorCodeObject);
    if (errorCode == -1 && PyErr_Occurred()) {
        PyErr_Print();
        FATAL("Failed to convert error code to integer");
    }

    PyObject* messageObject = PyTuple_GetItem(error, 1);
    if (messageObject == NULL) {
        PyErr_Print();
        FATAL("Failed to get error message from Trino error");
    }
    Py_ssize_t messageSize;
    const char* message = PyUnicode_AsUTF8AndSize(messageObject, &messageSize);
    if (message == NULL) {
        PyErr_Print();
        FATAL("Failed to get error message string");
    }

    PyObject* tracebackObject = PyTuple_GetItem(error, 2);
    if (tracebackObject == NULL) {
        PyErr_Print();
        FATAL("Failed to get error traceback from Trino error");
    }
    Py_ssize_t tracebackSize;
    const char* traceback = PyUnicode_AsUTF8AndSize(tracebackObject, &tracebackSize);
    if (traceback == NULL) {
        PyErr_Print();
        FATAL("Failed to get error traceback string");
    }

    trinoReturnError(errorCode, (u8*)message, messageSize, (u8*)traceback, tracebackSize);
    Py_DECREF(error);
}

u8* allocate(const i32 size)
{
    return xrealloc(NULL, size);
}

void deallocate(u8* pointer)
{
    return free(pointer);
}

void setup(const u8* functionName, const u8* argType, const u8* returnType)
{
    const char* name = (const char*)functionName;
    DEBUG("setup('%s')", name);

    PyObject* path = PySys_GetObject("path");
    PyObject* entry = PyUnicode_FromString("/guest");
    PyList_Append(path, entry);
    Py_DECREF(entry);

    guestModule = loadModule("guest");
    guestFunction = findFunction(guestModule, name);

    trinoArgType = argType;
    trinoReturnType = returnType;

    DEBUG("Setup complete");
}

u8* execute(const u8* data)
{
    DEBUG("execute()");
    PyObject* args = buildArgs(data);

    PyObject* str = PyObject_Str(args);
    DEBUG("invoke(%s)", PyUnicode_AsUTF8(str));
    Py_DECREF(str);

    PyObject* value = PyObject_CallObject(guestFunction, args);
    if (value == NULL) {
        PyObject* exception = PyErr_GetRaisedException();
        handleTrinoError(exception);
        Py_DECREF(exception);
        Py_DECREF(args);
        return NULL;
    }
    Py_DECREF(args);

    u8* result = NULL;
    const u8* type = trinoReturnType;
    Buffer buffer = {
        .data = xrealloc(NULL, 1024),
        .size = 1024,
        .used = 4,
    };
    if (buildResult(&type, value, &buffer)) {
        result = buffer.data;
        *(i32*)result = buffer.used - 4;
    }
    else {
        free(buffer.data);
    }
    Py_DECREF(value);

    DEBUG("execute: completed");
    return result;
}

static PyObject* loadModule(const char* name)
{
    PyObject* pyName = PyUnicode_DecodeFSDefault(name);
    PyObject* module = PyImport_Import(pyName);
    Py_DECREF(pyName);

    if (module == NULL) {
        PyErr_Print();
        FATAL("Failed to load Python module '%s'", name);
    }

    DEBUG("Loaded Python module '%s'", name);
    return module;
}

static PyObject* findFunction(PyObject* module, const char* name)
{
    PyObject* function = PyObject_GetAttrString(module, name);
    if (function == NULL || !PyCallable_Check(function)) {
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        FATAL("Cannot find function '%s' in '%s'", name, PyModule_GetName(module));
    }
    return function;
}

int main(const int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    DEBUG("Initializing Python host");

    Py_Initialize();
    DEBUG("Python initialized");

    PyDateTime_IMPORT;

    emptyTuple = PyTuple_New(0);

    decimalModule = loadModule("decimal");
    decimalClass = findFunction(decimalModule, "Decimal");

    uuidModule = loadModule("uuid");
    uuidClass = findFunction(uuidModule, "UUID");

    ipaddressModule = loadModule("ipaddress");
    ipaddressV4Class = findFunction(ipaddressModule, "IPv4Address");
    ipaddressV6Class = findFunction(ipaddressModule, "IPv6Address");

    trinoModule = loadModule("trino");
    trinoErrorResultFunction = findFunction(trinoModule, "_trino_error_result");
    decimalToStringFunction = findFunction(trinoModule, "_decimal_to_string");

    DEBUG("Python host initialized");
    return 0;
}
