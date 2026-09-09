package com.papership.mobile;

import android.content.ContentResolver;
import android.net.Uri;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/**
 * Imports an N64 ROM selected with the system file picker.
 *
 * The image is normalised to big-endian (.z64) byte order, checked to be the
 * Paper Mario (USA) cartridge (game code NMQE, 40 MiB) and written into the
 * app's private data directory under the name the native side looks for.
 */
final class RomImporter {
    static final String ROM_FILE_NAME = "Paper Mario (USA).z64";
    static final int ROM_SIZE = 40 * 1024 * 1024;

    private static final int MAGIC_Z64 = 0x80371240; // big-endian
    private static final int MAGIC_V64 = 0x37804012; // byte-swapped
    private static final int MAGIC_N64 = 0x40123780; // little-endian words
    private static final String US_GAME_CODE = "NMQE";

    static final class Result {
        final boolean ok;
        final String error;

        private Result(boolean ok, String error) {
            this.ok = ok;
            this.error = error;
        }

        static Result success() {
            return new Result(true, null);
        }

        static Result failure(String error) {
            return new Result(false, error);
        }
    }

    private RomImporter() {
    }

    static Result importRom(ContentResolver resolver, Uri uri, File dest) {
        byte[] data;
        try (InputStream in = resolver.openInputStream(uri)) {
            if (in == null) {
                return Result.failure("could not open the selected file");
            }
            data = readUpTo(in, ROM_SIZE + 1);
        } catch (IOException | SecurityException e) {
            return Result.failure("could not read the selected file (" + e.getMessage() + ")");
        } catch (OutOfMemoryError e) {
            return Result.failure("not enough memory to read the file");
        }

        if (data.length < 0x1000) {
            return Result.failure("the file is too small to be an N64 ROM");
        }
        if (!normalizeByteOrder(data)) {
            return Result.failure("the file is not an N64 ROM image (.z64, .v64 or .n64)");
        }
        if (data.length != ROM_SIZE) {
            return Result.failure("unexpected size " + data.length + " bytes; Paper Mario (USA) is 40 MB");
        }
        String gameCode = new String(data, 0x3B, 4, StandardCharsets.US_ASCII);
        if (!US_GAME_CODE.equals(gameCode)) {
            String name = new String(data, 0x20, 20, StandardCharsets.US_ASCII).trim();
            return Result.failure("'" + name + "' (" + gameCode + ") is not Paper Mario (USA); only the US release is supported");
        }

        File temp = new File(dest.getPath() + ".tmp");
        try (OutputStream out = new FileOutputStream(temp)) {
            out.write(data);
        } catch (IOException e) {
            temp.delete();
            return Result.failure("could not write the ROM (" + e.getMessage() + ")");
        }
        if (dest.exists() && !dest.delete()) {
            temp.delete();
            return Result.failure("could not replace the existing ROM file");
        }
        if (!temp.renameTo(dest)) {
            temp.delete();
            return Result.failure("could not move the ROM into place");
        }
        return Result.success();
    }

    /** True if `file` is already a normalised Paper Mario (USA) image. */
    static boolean isUsRom(File file) {
        if (!file.isFile() || file.length() != ROM_SIZE) {
            return false;
        }
        try (RandomAccessFile raf = new RandomAccessFile(file, "r")) {
            byte[] header = new byte[0x40];
            raf.readFully(header);
            return readInt(header, 0) == MAGIC_Z64
                    && US_GAME_CODE.equals(new String(header, 0x3B, 4, StandardCharsets.US_ASCII));
        } catch (IOException e) {
            return false;
        }
    }

    private static byte[] readUpTo(InputStream in, int limit) throws IOException {
        byte[] buffer = new byte[limit];
        int total = 0;
        while (total < limit) {
            int read = in.read(buffer, total, limit - total);
            if (read < 0) {
                break;
            }
            total += read;
        }
        return total == limit ? buffer : Arrays.copyOf(buffer, total);
    }

    private static int readInt(byte[] data, int offset) {
        return ((data[offset] & 0xFF) << 24) | ((data[offset + 1] & 0xFF) << 16)
                | ((data[offset + 2] & 0xFF) << 8) | (data[offset + 3] & 0xFF);
    }

    /** Convert the image to .z64 byte order in place; false if it is not an N64 ROM. */
    private static boolean normalizeByteOrder(byte[] data) {
        switch (readInt(data, 0)) {
            case MAGIC_Z64:
                return true;
            case MAGIC_V64:
                for (int i = 0; i + 1 < data.length; i += 2) {
                    byte t = data[i];
                    data[i] = data[i + 1];
                    data[i + 1] = t;
                }
                return true;
            case MAGIC_N64:
                for (int i = 0; i + 3 < data.length; i += 4) {
                    byte b0 = data[i], b1 = data[i + 1];
                    data[i] = data[i + 3];
                    data[i + 1] = data[i + 2];
                    data[i + 2] = b1;
                    data[i + 3] = b0;
                }
                return true;
            default:
                return false;
        }
    }
}
