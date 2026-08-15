package com.dishii.zelda3;

import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/** ROM shape and digest helpers used while building the port assets. */
final class RomVerification {

    static final int ROM_SIZE = 1048576;
    private static final int COPIER_HEADER = 512;

    private RomVerification() {}

    static byte[] stripCopierHeader(byte[] rom) {
        if (rom.length == ROM_SIZE + COPIER_HEADER && (rom.length % 1024) == COPIER_HEADER) {
            byte[] trimmed = new byte[ROM_SIZE];
            System.arraycopy(rom, COPIER_HEADER, trimmed, 0, ROM_SIZE);
            return trimmed;
        }
        return rom;
    }

    static String md5Hex(byte[] data) {
        try {
            byte[] digest = MessageDigest.getInstance("MD5").digest(data);
            StringBuilder hex = new StringBuilder(digest.length * 2);
            for (byte b : digest) {
                hex.append(Character.forDigit((b >>> 4) & 0xf, 16));
                hex.append(Character.forDigit(b & 0xf, 16));
            }
            return hex.toString();
        } catch (NoSuchAlgorithmException e) {
            throw new AssertionError("MD5 unavailable", e);
        }
    }
}
