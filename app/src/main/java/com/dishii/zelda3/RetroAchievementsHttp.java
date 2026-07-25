package com.dishii.zelda3;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.MalformedURLException;
import java.net.URL;
import javax.net.ssl.HttpsURLConnection;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

/** Bounded HTTPS transport for the native RetroAchievements callback. */
final class RetroAchievementsHttp {

    private static final int CLIENT_ERROR = -1;
    private static final int RETRYABLE_TRANSPORT_ERROR = -2;
    private static final int CONNECT_TIMEOUT_MS = 10_000;
    private static final int READ_TIMEOUT_MS = 15_000;
    private static final int MAX_REDIRECTS = 3;
    private static final int MAX_RESPONSE_BYTES = 1024 * 1024;

    private static final ThreadPoolExecutor EXECUTOR = new ThreadPoolExecutor(
            1, 2, 30, TimeUnit.SECONDS, new ArrayBlockingQueue<Runnable>(8),
            new ThreadPoolExecutor.AbortPolicy());

    private RetroAchievementsHttp() {}

    static boolean enqueue(final long requestId, final String url, final byte[] postData,
            final String contentType, final String userAgent) {
        if (!isHttpsUrl(url)) {
            nativeComplete(requestId, CLIENT_ERROR, null);
            return true;
        }
        try {
            EXECUTOR.execute(new Runnable() {
                @Override
                public void run() {
                    execute(requestId, url, postData, contentType, userAgent);
                }
            });
            return true;
        } catch (RejectedExecutionException e) {
            nativeComplete(requestId, RETRYABLE_TRANSPORT_ERROR, null);
            return true;
        }
    }

    static boolean isHttpsUrl(String value) {
        try {
            return value != null && "https".equalsIgnoreCase(new URL(value).getProtocol());
        } catch (MalformedURLException e) {
            return false;
        }
    }

    private static void execute(long requestId, String url, byte[] postData, String contentType,
            String userAgent) {
        String currentUrl = url;
        boolean usePost = postData != null;
        int status = RETRYABLE_TRANSPORT_ERROR;

        try {
            for (int redirects = 0; redirects <= MAX_REDIRECTS; redirects++) {
                HttpsURLConnection connection = (HttpsURLConnection) new URL(currentUrl)
                        .openConnection();
                try {
                    connection.setInstanceFollowRedirects(false);
                    connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
                    connection.setReadTimeout(READ_TIMEOUT_MS);
                    connection.setRequestProperty("User-Agent", userAgent);
                    if (usePost) {
                        connection.setRequestMethod("POST");
                        if (contentType != null) {
                            connection.setRequestProperty("Content-Type", contentType);
                        }
                        connection.setFixedLengthStreamingMode(postData.length);
                        try (OutputStream output = connection.getOutputStream()) {
                            output.write(postData);
                        }
                    }

                    status = connection.getResponseCode();
                    if (isRedirect(status)) {
                        String location = connection.getHeaderField("Location");
                        if (location == null) {
                            nativeComplete(requestId, status, null);
                            return;
                        }
                        currentUrl = new URL(new URL(currentUrl), location).toString();
                        if (!isHttpsUrl(currentUrl)) {
                            nativeComplete(requestId, status, null);
                            return;
                        }
                        if (status == HttpsURLConnection.HTTP_SEE_OTHER) {
                            usePost = false;
                        }
                        continue;
                    }

                    InputStream input = status >= 400 ? connection.getErrorStream()
                            : connection.getInputStream();
                    nativeComplete(requestId, status, readBody(input));
                    return;
                } finally {
                    connection.disconnect();
                }
            }
            nativeComplete(requestId, status, null);
        } catch (IOException e) {
            nativeComplete(requestId,
                    status > 0 ? status : RETRYABLE_TRANSPORT_ERROR, null);
        } catch (RuntimeException e) {
            nativeComplete(requestId, CLIENT_ERROR, null);
        }
    }

    private static boolean isRedirect(int status) {
        return status == HttpsURLConnection.HTTP_MOVED_PERM
                || status == HttpsURLConnection.HTTP_MOVED_TEMP
                || status == HttpsURLConnection.HTTP_SEE_OTHER
                || status == 307 || status == 308;
    }

    private static byte[] readBody(InputStream input) throws IOException {
        if (input == null) {
            return new byte[0];
        }
        try (InputStream source = input;
                ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int total = 0;
            int count;
            while ((count = source.read(buffer)) != -1) {
                if (count > MAX_RESPONSE_BYTES - total) {
                    throw new IOException("response body too large");
                }
                output.write(buffer, 0, count);
                total += count;
            }
            return output.toByteArray();
        }
    }

    private static native void nativeComplete(long requestId, int httpStatus, byte[] body);
}
