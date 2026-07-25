package com.dishii.zelda3;

import android.content.Context;
import java.io.File;
import java.io.IOException;

/** Java ownership boundary for RA configuration and private credentials. */
final class RetroAchievementsBridge {

    private static Context applicationContext;

    private RetroAchievementsBridge() {}

    static void configure(Context context, File configFile, RetroAchievementsConfig config,
            RetroAchievementsStorage storage, boolean verified) {
        applicationContext = context.getApplicationContext();
        boolean enabled = verified && config.enabled
                && config.mode != RetroAchievementsConfig.Mode.DISABLED;
        RetroAchievementsConfig.Credentials credentials = enabled
                ? config.resolveCredentials(storage.getUsername(), storage.getReturnedToken())
                : null;
        nativeConfigure(enabled, config.mode == RetroAchievementsConfig.Mode.SPECTATOR,
                config.clientName, config.clientVersion,
                credentials == null ? null : credentials.username,
                credentials == null ? null : credentials.secret,
                credentials != null && credentials.token);
        if (credentials != null && credentials.externalPassword) {
            try {
                RetroAchievementsConfig.clearPasswordAfterNativeHandoff(configFile);
            } catch (IOException ignored) {
                // The native handoff already completed; external cleanup can retry next launch.
            }
        }
    }

    static void logout(Context context) {
        new RetroAchievementsStorage(context).clearCredentials();
        nativeLogout();
    }

    static String snapshot() {
        return nativeSnapshot();
    }

    static void persistReturnedToken(String username, String token) {
        Context context = applicationContext;
        if (context != null && username != null && token != null) {
            new RetroAchievementsStorage(context).saveCredentials(username, token);
        }
    }

    private static native void nativeConfigure(boolean enabled, boolean spectator,
            String clientName, String clientVersion, String username, String secret,
            boolean secretIsToken);
    private static native void nativeLogout();
    private static native String nativeSnapshot();
}
