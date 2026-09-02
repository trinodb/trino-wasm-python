/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package io.trino.wasm.python;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import run.endive.runtime.HostFunction;
import run.endive.runtime.ImportValues;
import run.endive.runtime.Instance;
import run.endive.runtime.Memory;
import run.endive.wasi.WasiOptions;
import run.endive.wasi.WasiPreview1;
import run.endive.wasm.types.FunctionType;
import run.endive.wasm.types.ValType;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

class TestPython
{
    // Trino type constants (from pyhost.h)
    private static final int ROW = 0;
    private static final int BIGINT = 4;
    private static final int INTEGER = 5;
    private static final int VARCHAR = 11;

    @Test
    void testAddIntegers(@TempDir Path guestDir)
            throws IOException
    {
        Files.writeString(guestDir.resolve("guest.py"),
                "def add(a, b):\n    return a + b\n");

        long result = callPythonFunction(
                guestDir,
                "add",
                rowType(INTEGER, INTEGER),
                intType(INTEGER),
                rowData(intValue(3), intValue(7)));

        assertEquals(10, result);
    }

    @Test
    void testStringLength(@TempDir Path guestDir)
            throws IOException
    {
        Files.writeString(guestDir.resolve("guest.py"),
                "def strlen(s):\n    return len(s)\n");

        long result = callPythonFunction(
                guestDir,
                "strlen",
                rowType(VARCHAR),
                intType(INTEGER),
                rowData(varcharValue("hello world")));

        assertEquals(11, result);
    }

    @Test
    void testReturnString(@TempDir Path guestDir)
            throws IOException
    {
        Files.writeString(guestDir.resolve("guest.py"),
                "def greet(name):\n    return 'hello ' + name\n");

        String result = callPythonFunctionString(
                guestDir,
                "greet",
                rowType(VARCHAR),
                varcharReturnType(),
                rowData(varcharValue("world")));

        assertEquals("hello world", result);
    }

    private long callPythonFunction(Path guestDir, String functionName, byte[] argType, byte[] returnType, byte[] data)
    {
        try (WasiPreview1 wasi = createWasi(guestDir)) {
            Instance instance = createInstance(wasi);
            Memory memory = instance.memory();

            int funcNamePtr = allocate(instance, functionName.length() + 1);
            memory.writeCString(funcNamePtr, functionName);

            int argTypePtr = allocate(instance, argType.length);
            memory.write(argTypePtr, argType);

            int returnTypePtr = allocate(instance, returnType.length);
            memory.write(returnTypePtr, returnType);

            instance.export("setup").apply(funcNamePtr, argTypePtr, returnTypePtr);

            int dataPtr = allocate(instance, data.length);
            memory.write(dataPtr, data);

            long[] result = instance.export("execute").apply(dataPtr);
            int resultPtr = (int) result[0];

            // int resultSize = memory.readInt(resultPtr);
            byte present = memory.read(resultPtr + 4);
            assertEquals(1, present, "result should be present");

            return memory.readInt(resultPtr + 5);
        }
    }

    private String callPythonFunctionString(Path guestDir, String functionName, byte[] argType, byte[] returnType, byte[] data)
    {
        try (WasiPreview1 wasi = createWasi(guestDir)) {
            Instance instance = createInstance(wasi);
            Memory memory = instance.memory();

            int funcNamePtr = allocate(instance, functionName.length() + 1);
            memory.writeCString(funcNamePtr, functionName);

            int argTypePtr = allocate(instance, argType.length);
            memory.write(argTypePtr, argType);

            int returnTypePtr = allocate(instance, returnType.length);
            memory.write(returnTypePtr, returnType);

            instance.export("setup").apply(funcNamePtr, argTypePtr, returnTypePtr);

            int dataPtr = allocate(instance, data.length);
            memory.write(dataPtr, data);

            long[] result = instance.export("execute").apply(dataPtr);
            int resultPtr = (int) result[0];

            byte present = memory.read(resultPtr + 4);
            assertEquals(1, present, "result should be present");

            int strLen = memory.readInt(resultPtr + 5);
            return memory.readString(resultPtr + 9, strLen);
        }
    }

    private static int allocate(Instance instance, int size)
    {
        long[] result = instance.export("allocate").apply(size);
        return (int) result[0];
    }

    private static WasiPreview1 createWasi(Path guestDir)
    {
        return WasiPreview1.builder()
                .withOptions(WasiOptions.builder()
                        .withStdout(System.out)
                        .withStderr(System.err)
                        .withDirectory("/guest", guestDir)
                        .withEnvironment("PYTHONDONTWRITEBYTECODE", "1")
                        .build())
                .build();
    }

    private static Instance createInstance(WasiPreview1 wasi)
    {
        HostFunction returnError = new HostFunction(
                "trino",
                "return_error",
                FunctionType.of(
                        List.of(ValType.I32, ValType.I32, ValType.I32, ValType.I32, ValType.I32),
                        List.of()),
                (_, args) -> {
                    throw new RuntimeException("Python error: errorCode=" + args[0]);
                });

        ImportValues imports = ImportValues.builder()
                .addFunction(wasi.toHostFunctions())
                .addFunction(returnError)
                .build();

        Python compiled = new Python();
        return Instance.builder(compiled.wasmModule())
                .withMachineFactory(compiled.machineFactory())
                .withImportValues(imports)
                .withStart(false)
                .build();
    }

    // Type descriptor builders

    private static byte[] rowType(int... fieldTypes)
    {
        ByteBuffer buf = ByteBuffer.allocate(4 + 4 + fieldTypes.length * 4).order(ByteOrder.LITTLE_ENDIAN);
        buf.putInt(ROW);
        buf.putInt(fieldTypes.length);
        for (int type : fieldTypes) {
            buf.putInt(type);
        }
        return buf.array();
    }

    private static byte[] intType(int type)
    {
        ByteBuffer buf = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN);
        buf.putInt(type);
        return buf.array();
    }

    private static byte[] varcharReturnType()
    {
        ByteBuffer buf = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN);
        buf.putInt(VARCHAR);
        return buf.array();
    }

    // Data builders

    private static byte[] rowData(byte[]... fields)
    {
        int totalSize = 1; // present byte for the row
        for (byte[] field : fields) {
            totalSize += field.length;
        }
        ByteBuffer buf = ByteBuffer.allocate(totalSize).order(ByteOrder.LITTLE_ENDIAN);
        buf.put((byte) 1); // present
        for (byte[] field : fields) {
            buf.put(field);
        }
        return buf.array();
    }

    private static byte[] intValue(int value)
    {
        ByteBuffer buf = ByteBuffer.allocate(5).order(ByteOrder.LITTLE_ENDIAN);
        buf.put((byte) 1); // present
        buf.putInt(value);
        return buf.array();
    }

    private static byte[] varcharValue(String value)
    {
        byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
        ByteBuffer buf = ByteBuffer.allocate(1 + 4 + bytes.length).order(ByteOrder.LITTLE_ENDIAN);
        buf.put((byte) 1); // present
        buf.putInt(bytes.length);
        buf.put(bytes);
        return buf.array();
    }
}
