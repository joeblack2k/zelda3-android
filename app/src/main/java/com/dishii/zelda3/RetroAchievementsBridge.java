package com.dishii.zelda3;

import android.content.Context;
import android.os.Build;
import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** Java ownership boundary for RA configuration and private credentials. */
final class RetroAchievementsBridge {

    private static Context applicationContext;
    private static File externalConfigFile;
    private static File passwordConfigFile;

    static final class Achievement {
        final String bucket;
        final int id;
        final String title;
        final String description;
        final int points;
        final boolean unlocked;
        final String progress;

        Achievement(String bucket, int id, String title, String description,
                int points, boolean unlocked, String progress) {
            this.bucket = bucket;
            this.id = id;
            this.title = title;
            this.description = description;
            this.points = points;
            this.unlocked = unlocked;
            this.progress = progress;
        }
    }

    static final class UiModel {
        String mode = "disabled";
        String status = "disabled";
        String username = "";
        String gameTitle = "";
        int gameId;
        int unlocked;
        int core;
        int points;
        int rp;
        String richPresence = "";
        String lastEvent = "";
        boolean disconnected;
        boolean spectator;
        final List<Achievement> achievements = new ArrayList<>();
    }

    private RetroAchievementsBridge() {}

    static void configure(Context context, File configFile, RetroAchievementsConfig config,
            RetroAchievementsStorage storage, boolean verified) {
        applicationContext = context.getApplicationContext();
        externalConfigFile = configFile;
        boolean enabled = verified && config.enabled
                && config.mode != RetroAchievementsConfig.Mode.DISABLED;
        RetroAchievementsConfig.Credentials credentials = enabled
                ? config.resolveCredentials(storage.getUsername(), storage.getReturnedToken())
                : null;
        nativeConfigure(enabled, config.mode == RetroAchievementsConfig.Mode.SPECTATOR, verified,
                config.clientName, config.clientVersion,
                credentials == null ? null : credentials.username,
                credentials == null ? null : credentials.secret,
                credentials != null && credentials.token,
                Build.VERSION.RELEASE, Build.MODEL);
        passwordConfigFile = credentials != null && credentials.externalPassword ? configFile : null;
    }

    static void logout(Context context) {
        new RetroAchievementsStorage(context).clearCredentials();
        File configFile = externalConfigFile;
        if (configFile != null) {
            try {
                RetroAchievementsConfig.clearExternalCredentials(configFile);
            } catch (IOException ignored) {
                // The private store is already durable; external cleanup retries on next launch.
            }
        }
        passwordConfigFile = null;
        nativeLogout();
    }

    static void setPaused(boolean paused) {
        nativeSetPaused(paused);
    }

    static String snapshot() {
        return nativeSnapshot();
    }

    static UiModel uiModel() {
        UiModel model = new UiModel();
        String raw = nativeUiModel();
        if (raw == null) return model;
        String[] lines = raw.split("\n");
        for (String line : lines) {
            String[] field = line.split("\t", -1);
            try {
                if (field.length >= 15 && "M".equals(field[0])) {
                    model.mode = field[1];
                    model.status = field[2];
                    model.username = field[3];
                    model.gameTitle = field[4];
                    model.gameId = Integer.parseInt(field[5]);
                    model.unlocked = Integer.parseInt(field[6]);
                    model.core = Integer.parseInt(field[7]);
                    model.points = Integer.parseInt(field[8]);
                    model.rp = Integer.parseInt(field[9]);
                    model.richPresence = field[10];
                    model.lastEvent = field[11];
                    model.disconnected = "1".equals(field[12]);
                    model.spectator = "1".equals(field[14]);
                } else if (field.length >= 8 && "A".equals(field[0])) {
                    model.achievements.add(new Achievement(field[1], Integer.parseInt(field[2]),
                            field[3], field[4], Integer.parseInt(field[5]),
                            "1".equals(field[6]), field[7]));
                }
            } catch (NumberFormatException ignored) {
                // Cached native UI data is best-effort; keep the usable records.
            }
        }
        return model;
    }

    static void persistReturnedToken(String username, String token) {
        Context context = applicationContext;
        if (context != null && username != null && token != null) {
            if (new RetroAchievementsStorage(context).saveCredentials(username, token)) {
                File configFile = passwordConfigFile;
                passwordConfigFile = null;
                if (configFile != null) {
                    try {
                        RetroAchievementsConfig.clearExternalPassword(configFile);
                    } catch (IOException ignored) {
                        // The returned token is durable; external cleanup retries on next launch.
                    }
                }
            }
        }
    }

    private static native void nativeConfigure(boolean enabled, boolean spectator, boolean verified,
            String clientName, String clientVersion, String username, String secret,
            boolean secretIsToken, String androidRelease, String androidModel);
    private static native void nativeLogout();
    private static native void nativeSetPaused(boolean paused);
    private static native String nativeSnapshot();
    private static native String nativeUiModel();
}
