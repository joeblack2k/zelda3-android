package com.dishii.zelda3;

/** Runnable JVM check for the HTTPS-only request boundary. */
public final class RetroAchievementsHttpTest {

    public static void main(String[] args) {
        check(RetroAchievementsHttp.isHttpsUrl("https://retroachievements.org/API"));
        check(!RetroAchievementsHttp.isHttpsUrl("http://retroachievements.org/API"));
        check(!RetroAchievementsHttp.isHttpsUrl("ftp://retroachievements.org/API"));
        check(!RetroAchievementsHttp.isHttpsUrl("not a url"));
    }

    private static void check(boolean condition) {
        if (!condition) {
            throw new AssertionError("unexpected URL validation result");
        }
    }
}
