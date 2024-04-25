from decimal import Decimal
from traceback import format_exception

INVALID_FUNCTION_ARGUMENT = 7
DIVISION_BY_ZERO = 8
INVALID_CAST_ARGUMENT = 9
NOT_SUPPORTED = 13
NUMERIC_VALUE_OUT_OF_RANGE = 19
EXCEEDED_FUNCTION_MEMORY_LIMIT = 37
FUNCTION_IMPLEMENTATION_ERROR = 65549


class TrinoError(Exception):
    def __init__(self, error_code, message):
        super().__init__(message)
        self.error_code = error_code


class InvalidFunctionArgumentError(TrinoError):
    def __init__(self, message):
        super().__init__(INVALID_FUNCTION_ARGUMENT, message)


class NumericValueOutOfRangeError(TrinoError):
    def __init__(self, message):
        super().__init__(NUMERIC_VALUE_OUT_OF_RANGE, message)


def _trino_error_result(e: BaseException):
    traceback = ''.join(format_exception(e))
    if isinstance(e, ZeroDivisionError):
        return DIVISION_BY_ZERO, str(e), traceback
    if isinstance(e, TrinoError):
        return e.error_code, str(e), traceback
    if isinstance(e, MemoryError):
        return EXCEEDED_FUNCTION_MEMORY_LIMIT, 'Python MemoryError', traceback
    message = type(e).__name__
    value = str(e)
    if value:
        message += ': ' + value
    return FUNCTION_IMPLEMENTATION_ERROR, message, traceback


def _decimal_to_string(value: Decimal):
    if not isinstance(value, Decimal):
        raise ValueError('Not a Decimal: ' + type(value).__name__)
    if not value.is_finite():
        raise ValueError('Decimal is not finite: ' + str(value))
    return "{:f}".format(value)
