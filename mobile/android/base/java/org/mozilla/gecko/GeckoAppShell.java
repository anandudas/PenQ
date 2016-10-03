/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.PipedInputStream;
import java.io.PipedOutputStream;
import java.net.MalformedURLException;
import java.net.Proxy;
import java.net.URLConnection;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.StringTokenizer;
import java.util.TreeMap;
import java.util.concurrent.ConcurrentHashMap;

import android.annotation.SuppressLint;
import org.mozilla.gecko.annotation.JNITarget;
import org.mozilla.gecko.annotation.RobocopTarget;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.AppConstants.Versions;
import org.mozilla.gecko.gfx.BitmapUtils;
import org.mozilla.gecko.gfx.LayerView;
import org.mozilla.gecko.gfx.PanZoomController;
import org.mozilla.gecko.permissions.Permissions;
import org.mozilla.gecko.util.EventCallback;
import org.mozilla.gecko.util.GeckoRequest;
import org.mozilla.gecko.util.HardwareCodecCapabilityUtils;
import org.mozilla.gecko.util.HardwareUtils;
import org.mozilla.gecko.util.NativeEventListener;
import org.mozilla.gecko.util.NativeJSContainer;
import org.mozilla.gecko.util.NativeJSObject;
import org.mozilla.gecko.util.ProxySelector;
import org.mozilla.gecko.util.ThreadUtils;

import android.Manifest;
import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlarmManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.PackageManager.NameNotFoundException;
import android.content.pm.ResolveInfo;
import android.content.pm.ServiceInfo;
import android.content.pm.Signature;
import android.content.res.TypedArray;
import android.graphics.Bitmap;
import android.graphics.ImageFormat;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.SurfaceTexture;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.hardware.Sensor;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.location.Criteria;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Looper;
import android.os.SystemClock;
import android.os.Vibrator;
import android.provider.Settings;
import android.telephony.TelephonyManager;
import android.text.TextUtils;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.ContextThemeWrapper;
import android.view.Display;
import android.view.HapticFeedbackConstants;
import android.view.Surface;
import android.view.SurfaceView;
import android.view.TextureView;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.webkit.MimeTypeMap;
import android.widget.AbsoluteLayout;

public class GeckoAppShell
{
    private static final String LOGTAG = "GeckoAppShell";

    // We have static members only.
    private GeckoAppShell() { }

    private static final CrashHandler CRASH_HANDLER = new CrashHandler() {
        @Override
        protected String getAppPackageName() {
            return AppConstants.ANDROID_PACKAGE_NAME;
        }

        @Override
        protected Context getAppContext() {
            return sContextGetter != null ? getApplicationContext() : null;
        }

        @Override
        protected Bundle getCrashExtras(final Thread thread, final Throwable exc) {
            final Bundle extras = super.getCrashExtras(thread, exc);

            extras.putString("ProductName", AppConstants.MOZ_APP_BASENAME);
            extras.putString("ProductID", AppConstants.MOZ_APP_ID);
            extras.putString("Version", AppConstants.MOZ_APP_VERSION);
            extras.putString("BuildID", AppConstants.MOZ_APP_BUILDID);
            extras.putString("Vendor", AppConstants.MOZ_APP_VENDOR);
            extras.putString("ReleaseChannel", AppConstants.MOZ_UPDATE_CHANNEL);
            return extras;
        }

        @Override
        public void uncaughtException(final Thread thread, final Throwable exc) {
            if (GeckoThread.isStateAtLeast(GeckoThread.State.EXITING)) {
                // We've called System.exit. All exceptions after this point are Android
                // berating us for being nasty to it.
                return;
            }

            super.uncaughtException(thread, exc);
        }

        @Override
        public boolean reportException(final Thread thread, final Throwable exc) {
            try {
                if (exc instanceof OutOfMemoryError) {
                    SharedPreferences prefs = getSharedPreferences();
                    SharedPreferences.Editor editor = prefs.edit();
                    editor.putBoolean(GeckoApp.PREFS_OOM_EXCEPTION, true);

                    // Synchronously write to disk so we know it's done before we
                    // shutdown
                    editor.commit();
                }

                reportJavaCrash(getExceptionStackTrace(exc));

            } catch (final Throwable e) {
            }

            // reportJavaCrash should have caused us to hard crash. If we're still here,
            // it probably means Gecko is not loaded, and we should do something else.
            if (AppConstants.MOZ_CRASHREPORTER && AppConstants.MOZILLA_OFFICIAL) {
                // Only use Java crash reporter if enabled on official build.
                return super.reportException(thread, exc);
            }
            return false;
        }
    };

    public static CrashHandler ensureCrashHandling() {
        // Crash handling is automatically enabled when GeckoAppShell is loaded.
        return CRASH_HANDLER;
    }

    private static final Map<String, String> ALERT_COOKIES = new ConcurrentHashMap<String, String>();

    private static volatile boolean locationHighAccuracyEnabled;

    // Accessed by NotificationHelper. This should be encapsulated.
    /* package */ static NotificationClient notificationClient;

    // See also HardwareUtils.LOW_MEMORY_THRESHOLD_MB.
    private static final int HIGH_MEMORY_DEVICE_THRESHOLD_MB = 768;

    static private int sDensityDpi;
    static private int sScreenDepth;

    /* Is the value in sVibrationEndTime valid? */
    private static boolean sVibrationMaybePlaying;

    /* Time (in System.nanoTime() units) when the currently-playing vibration
     * is scheduled to end.  This value is valid only when
     * sVibrationMaybePlaying is true. */
    private static long sVibrationEndTime;

    private static Sensor gAccelerometerSensor;
    private static Sensor gLinearAccelerometerSensor;
    private static Sensor gGyroscopeSensor;
    private static Sensor gOrientationSensor;
    private static Sensor gProximitySensor;
    private static Sensor gLightSensor;
    private static Sensor gRotationVectorSensor;
    private static Sensor gGameRotationVectorSensor;

    private static final String GECKOREQUEST_RESPONSE_KEY = "response";
    private static final String GECKOREQUEST_ERROR_KEY = "error";

    /*
     * Keep in sync with constants found here:
     * http://mxr.mozilla.org/mozilla-central/source/uriloader/base/nsIWebProgressListener.idl
    */
    static public final int WPL_STATE_START = 0x00000001;
    static public final int WPL_STATE_STOP = 0x00000010;
    static public final int WPL_STATE_IS_DOCUMENT = 0x00020000;
    static public final int WPL_STATE_IS_NETWORK = 0x00040000;

    /* Keep in sync with constants found here:
      http://mxr.mozilla.org/mozilla-central/source/netwerk/base/nsINetworkLinkService.idl
    */
    static public final int LINK_TYPE_UNKNOWN = 0;
    static public final int LINK_TYPE_ETHERNET = 1;
    static public final int LINK_TYPE_USB = 2;
    static public final int LINK_TYPE_WIFI = 3;
    static public final int LINK_TYPE_WIMAX = 4;
    static public final int LINK_TYPE_2G = 5;
    static public final int LINK_TYPE_3G = 6;
    static public final int LINK_TYPE_4G = 7;

    /* The Android-side API: API methods that Android calls */

    // Initialization methods
    public static native void registerJavaUiThread();

    // helper methods
    public static void callObserver(String observerKey, String topic, String data) {
        sendEventToGecko(GeckoEvent.createCallObserverEvent(observerKey, topic, data));
    }
    public static void removeObserver(String observerKey) {
        sendEventToGecko(GeckoEvent.createRemoveObserverEvent(observerKey));
    }
    public static native void onSurfaceTextureFrameAvailable(Object surfaceTexture, int id);
    public static native void dispatchMemoryPressure();

    private static native void reportJavaCrash(String stackTrace);

    public static void notifyUriVisited(String uri) {
        sendEventToGecko(GeckoEvent.createVisitedEvent(uri));
    }

    public static native void notifyBatteryChange(double aLevel, boolean aCharging, double aRemainingTime);

    public static native void invalidateAndScheduleComposite();

    public static native float computeRenderIntegrity();

    public static native SurfaceBits getSurfaceBits(Surface surface);

    public static native void addPresentationSurface(Surface surface);
    public static native void removePresentationSurface(Surface surface);

    public static native void onFullScreenPluginHidden(View view);

    private static LayerView sLayerView;
    private static Rect sScreenSize;

    public static void setLayerView(LayerView lv) {
        if (sLayerView == lv) {
            return;
        }
        sLayerView = lv;
    }

    @RobocopTarget
    public static LayerView getLayerView() {
        return sLayerView;
    }

    /**
     * If the Gecko thread is running, immediately dispatches the event to
     * Gecko.
     *
     * If the Gecko thread is not running, queues the event. If the queue is
     * full, throws {@link IllegalStateException}.
     *
     * Queued events will be dispatched in order of arrival when the Gecko
     * thread becomes live.
     *
     * This method can be called from any thread.
     *
     * @param e
     *            the event to dispatch. Cannot be null.
     */
    @RobocopTarget
    public static void sendEventToGecko(GeckoEvent e) {
        if (e == null) {
            throw new IllegalArgumentException("e cannot be null.");
        }

        if (GeckoThread.isRunning()) {
            notifyGeckoOfEvent(e);
            // Gecko will copy the event data into a normal C++ object.
            // We can recycle the event now.
            e.recycle();
            return;
        }

        GeckoThread.addPendingEvent(e);
    }

    /**
     * Sends an asynchronous request to Gecko.
     *
     * The response data will be passed to {@link GeckoRequest#onResponse(NativeJSObject)} if the
     * request succeeds; otherwise, {@link GeckoRequest#onError()} will fire.
     *
     * This method follows the same queuing conditions as {@link #sendEventToGecko(GeckoEvent)}.
     * It can be called from any thread. The GeckoRequest callbacks will be executed on the Gecko thread.
     *
     * @param request The request to dispatch. Cannot be null.
     */
    @RobocopTarget
    public static void sendRequestToGecko(final GeckoRequest request) {
        final String responseMessage = "Gecko:Request" + request.getId();

        EventDispatcher.getInstance().registerGeckoThreadListener(new NativeEventListener() {
            @Override
            public void handleMessage(String event, NativeJSObject message, EventCallback callback) {
                EventDispatcher.getInstance().unregisterGeckoThreadListener(this, event);
                if (!message.has(GECKOREQUEST_RESPONSE_KEY)) {
                    request.onError(message.getObject(GECKOREQUEST_ERROR_KEY));
                    return;
                }
                request.onResponse(message.getObject(GECKOREQUEST_RESPONSE_KEY));
            }
        }, responseMessage);

        notifyObservers(request.getName(), request.getData());
    }

    // Tell the Gecko event loop that an event is available.
    public static native void notifyGeckoOfEvent(GeckoEvent event);

    // Synchronously notify a Gecko observer; must be called from Gecko thread.
    @WrapForJNI
    public static native void syncNotifyObservers(String topic, String data);

    @WrapForJNI(stubName = "NotifyObservers")
    private static native void nativeNotifyObservers(String topic, String data);

    @RobocopTarget
    public static void notifyObservers(final String topic, final String data) {
        notifyObservers(topic, data, GeckoThread.State.RUNNING);
    }

    public static void notifyObservers(final String topic, final String data, final GeckoThread.State state) {
        if (GeckoThread.isStateAtLeast(state)) {
            nativeNotifyObservers(topic, data);
        } else {
            GeckoThread.queueNativeCallUntil(
                    state, GeckoAppShell.class, "nativeNotifyObservers",
                    String.class, topic, String.class, data);
        }
    }

    /*
     *  The Gecko-side API: API methods that Gecko calls
     */

    @WrapForJNI(allowMultithread = true, noThrow = true)
    public static String handleUncaughtException(Throwable e) {
        if (AppConstants.MOZ_CRASHREPORTER) {
            final Throwable exc = CrashHandler.getRootException(e);
            final StackTraceElement[] stack = exc.getStackTrace();
            if (stack.length >= 1 && stack[0].isNativeMethod()) {
                // The exception occurred when running native code. Return an exception
                // string and trigger the crash reporter inside the caller so that we get
                // a better native stack in Socorro.
                CrashHandler.logException(Thread.currentThread(), exc);
                return CrashHandler.getExceptionStackTrace(exc);
            }
        }
        CRASH_HANDLER.uncaughtException(null, e);
        return null;
    }

    private static final Runnable sCallbackRunnable = new Runnable() {
        @Override
        public void run() {
            ThreadUtils.assertOnUiThread();
            long nextDelay = runUiThreadCallback();
            if (nextDelay >= 0) {
                ThreadUtils.getUiHandler().postDelayed(this, nextDelay);
            }
        }
    };

    private static native long runUiThreadCallback();

    @WrapForJNI(allowMultithread = true)
    private static void requestUiThreadCallback(long delay) {
        ThreadUtils.getUiHandler().postDelayed(sCallbackRunnable, delay);
    }

    private static float getLocationAccuracy(Location location) {
        float radius = location.getAccuracy();
        return (location.hasAccuracy() && radius > 0) ? radius : 1001;
    }

    @SuppressLint("MissingPermission") // Permissions are explicitly checked for in enableLocation()
    private static Location getLastKnownLocation(LocationManager lm) {
        Location lastKnownLocation = null;
        List<String> providers = lm.getAllProviders();

        for (String provider : providers) {
            Location location = lm.getLastKnownLocation(provider);
            if (location == null) {
                continue;
            }

            if (lastKnownLocation == null) {
                lastKnownLocation = location;
                continue;
            }

            long timeDiff = location.getTime() - lastKnownLocation.getTime();
            if (timeDiff > 0 ||
                (timeDiff == 0 &&
                 getLocationAccuracy(location) < getLocationAccuracy(lastKnownLocation))) {
                lastKnownLocation = location;
            }
        }

        return lastKnownLocation;
    }

    @WrapForJNI
    @SuppressLint("MissingPermission") // Permissions are explicitly checked for within this method
    public static void enableLocation(final boolean enable) {
        Permissions
                .from((Activity) getContext())
                .withPermissions(Manifest.permission.ACCESS_FINE_LOCATION)
                .onUIThread()
                .doNotPromptIf(!enable)
                .run(new Runnable() {
                    @Override
                    public void run() {
                        LocationManager lm = getLocationManager(getApplicationContext());
                        if (lm == null) {
                            return;
                        }

                        if (enable) {
                            Location lastKnownLocation = getLastKnownLocation(lm);
                            if (lastKnownLocation != null) {
                                getGeckoInterface().getLocationListener().onLocationChanged(lastKnownLocation);
                            }

                            Criteria criteria = new Criteria();
                            criteria.setSpeedRequired(false);
                            criteria.setBearingRequired(false);
                            criteria.setAltitudeRequired(false);
                            if (locationHighAccuracyEnabled) {
                                criteria.setAccuracy(Criteria.ACCURACY_FINE);
                                criteria.setCostAllowed(true);
                                criteria.setPowerRequirement(Criteria.POWER_HIGH);
                            } else {
                                criteria.setAccuracy(Criteria.ACCURACY_COARSE);
                                criteria.setCostAllowed(false);
                                criteria.setPowerRequirement(Criteria.POWER_LOW);
                            }

                            String provider = lm.getBestProvider(criteria, true);
                            if (provider == null)
                                return;

                            Looper l = Looper.getMainLooper();
                            lm.requestLocationUpdates(provider, 100, (float) .5, getGeckoInterface().getLocationListener(), l);
                        } else {
                            lm.removeUpdates(getGeckoInterface().getLocationListener());
                        }
                    }
                });
    }

    private static LocationManager getLocationManager(Context context) {
        try {
            return (LocationManager) context.getSystemService(Context.LOCATION_SERVICE);
        } catch (NoSuchFieldError e) {
            // Some Tegras throw exceptions about missing the CONTROL_LOCATION_UPDATES permission,
            // which allows enabling/disabling location update notifications from the cell radio.
            // CONTROL_LOCATION_UPDATES is not for use by normal applications, but we might be
            // hitting this problem if the Tegras are confused about missing cell radios.
            Log.e(LOGTAG, "LOCATION_SERVICE not found?!", e);
            return null;
        }
    }

    @WrapForJNI
    public static void enableLocationHighAccuracy(final boolean enable) {
        locationHighAccuracyEnabled = enable;
    }

    @WrapForJNI
    public static boolean setAlarm(int aSeconds, int aNanoSeconds) {
        AlarmManager am = (AlarmManager)
            getApplicationContext().getSystemService(Context.ALARM_SERVICE);

        Intent intent = new Intent(getApplicationContext(), AlarmReceiver.class);
        PendingIntent pi = PendingIntent.getBroadcast(
                getApplicationContext(), 0, intent, PendingIntent.FLAG_UPDATE_CURRENT);

        // AlarmManager only supports millisecond precision
        long time = ((long) aSeconds * 1000) + ((long) aNanoSeconds / 1_000_000L);
        am.setExact(AlarmManager.RTC_WAKEUP, time, pi);

        return true;
    }

    @WrapForJNI
    public static void disableAlarm() {
        AlarmManager am = (AlarmManager)
            getApplicationContext().getSystemService(Context.ALARM_SERVICE);

        Intent intent = new Intent(getApplicationContext(), AlarmReceiver.class);
        PendingIntent pi = PendingIntent.getBroadcast(
                getApplicationContext(), 0, intent,
                PendingIntent.FLAG_UPDATE_CURRENT);
        am.cancel(pi);
    }

    @WrapForJNI
    public static void enableSensor(int aSensortype) {
        GeckoInterface gi = getGeckoInterface();
        if (gi == null) {
            return;
        }
        SensorManager sm = (SensorManager)
            getApplicationContext().getSystemService(Context.SENSOR_SERVICE);

        switch (aSensortype) {
        case GeckoHalDefines.SENSOR_GAME_ROTATION_VECTOR:
            if (gGameRotationVectorSensor == null) {
                gGameRotationVectorSensor = sm.getDefaultSensor(15);
                    // sm.getDefaultSensor(
                    //     Sensor.TYPE_GAME_ROTATION_VECTOR); // API >= 18
            }
            if (gGameRotationVectorSensor != null) {
                sm.registerListener(gi.getSensorEventListener(),
                                    gGameRotationVectorSensor,
                                    SensorManager.SENSOR_DELAY_FASTEST);
            }
            if (gGameRotationVectorSensor != null) {
              break;
            }
            // Fallthrough

        case GeckoHalDefines.SENSOR_ROTATION_VECTOR:
            if (gRotationVectorSensor == null) {
                gRotationVectorSensor = sm.getDefaultSensor(
                    Sensor.TYPE_ROTATION_VECTOR);
            }
            if (gRotationVectorSensor != null) {
                sm.registerListener(gi.getSensorEventListener(),
                                    gRotationVectorSensor,
                                    SensorManager.SENSOR_DELAY_FASTEST);
            }
            if (gRotationVectorSensor != null) {
              break;
            }
            // Fallthrough

        case GeckoHalDefines.SENSOR_ORIENTATION:
            if (gOrientationSensor == null) {
                gOrientationSensor = sm.getDefaultSensor(
                    Sensor.TYPE_ORIENTATION);
            }
            if (gOrientationSensor != null) {
                sm.registerListener(gi.getSensorEventListener(),
                                    gOrientationSensor,
                                    SensorManager.SENSOR_DELAY_FASTEST);
            }
            break;

        case GeckoHalDefines.SENSOR_ACCELERATION:
            if (gAccelerometerSensor == null) {
                gAccelerometerSensor = sm.getDefaultSensor(
                    Sensor.TYPE_ACCELEROMETER);
            }
            if (gAccelerometerSensor != null) {
                sm.registerListener(gi.getSensorEventListener(),
                                    gAccelerometerSensor,
                                    SensorManager.SENSOR_DELAY_FASTEST);
            }
            break;

        case GeckoHalDefines.SENSOR_PROXIMITY:
            if (gProximitySensor == null) {
                gProximitySensor = sm.getDefaultSensor(Sensor.TYPE_PROXIMITY);
            }
            if (gProximitySensor != null) {
                sm.registerListener(gi.getSensorEventListener(),
                                    gProximitySensor,
                                    SensorManager.SENSOR_DELAY_NORMAL);
            }
            break;

        case GeckoHalDefines.SENSOR_LIGHT:
            if (gLightSensor == null) {
                gLightSensor = sm.getDefaultSensor(Sensor.TYPE_LIGHT);
            }
            if (gLightSensor != null) {
                sm.registerListener(gi.getSensorEventListener(),
                                    gLightSensor,
                                    SensorManager.SENSOR_DELAY_NORMAL);
            }
            break;

        case GeckoHalDefines.SENSOR_LINEAR_ACCELERATION:
            if (gLinearAccelerometerSensor == null) {
                gLinearAccelerometerSensor = sm.getDefaultSensor(
                    Sensor.TYPE_LINEAR_ACCELERATION);
            }
            if (gLinearAccelerometerSensor != null) {
                sm.registerListener(gi.getSensorEventListener(),
                                    gLinearAccelerometerSensor,
                                    SensorManager.SENSOR_DELAY_FASTEST);
            }
            break;

        case GeckoHalDefines.SENSOR_GYROSCOPE:
            if (gGyroscopeSensor == null) {
                gGyroscopeSensor = sm.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
            }
            if (gGyroscopeSensor != null) {
                sm.registerListener(gi.getSensorEventListener(),
                                    gGyroscopeSensor,
                                    SensorManager.SENSOR_DELAY_FASTEST);
            }
            break;

        default:
            Log.w(LOGTAG, "Error! Can't enable unknown SENSOR type " +
                  aSensortype);
        }
    }

    @WrapForJNI
    public static void disableSensor(int aSensortype) {
        GeckoInterface gi = getGeckoInterface();
        if (gi == null)
            return;

        SensorManager sm = (SensorManager)
            getApplicationContext().getSystemService(Context.SENSOR_SERVICE);

        switch (aSensortype) {
        case GeckoHalDefines.SENSOR_GAME_ROTATION_VECTOR:
            if (gGameRotationVectorSensor != null) {
                sm.unregisterListener(gi.getSensorEventListener(), gGameRotationVectorSensor);
              break;
            }
            // Fallthrough

        case GeckoHalDefines.SENSOR_ROTATION_VECTOR:
            if (gRotationVectorSensor != null) {
                sm.unregisterListener(gi.getSensorEventListener(), gRotationVectorSensor);
              break;
            }
            // Fallthrough

        case GeckoHalDefines.SENSOR_ORIENTATION:
            if (gOrientationSensor != null) {
                sm.unregisterListener(gi.getSensorEventListener(), gOrientationSensor);
            }
            break;

        case GeckoHalDefines.SENSOR_ACCELERATION:
            if (gAccelerometerSensor != null) {
                sm.unregisterListener(gi.getSensorEventListener(), gAccelerometerSensor);
            }
            break;

        case GeckoHalDefines.SENSOR_PROXIMITY:
            if (gProximitySensor != null) {
                sm.unregisterListener(gi.getSensorEventListener(), gProximitySensor);
            }
            break;

        case GeckoHalDefines.SENSOR_LIGHT:
            if (gLightSensor != null) {
                sm.unregisterListener(gi.getSensorEventListener(), gLightSensor);
            }
            break;

        case GeckoHalDefines.SENSOR_LINEAR_ACCELERATION:
            if (gLinearAccelerometerSensor != null) {
                sm.unregisterListener(gi.getSensorEventListener(), gLinearAccelerometerSensor);
            }
            break;

        case GeckoHalDefines.SENSOR_GYROSCOPE:
            if (gGyroscopeSensor != null) {
                sm.unregisterListener(gi.getSensorEventListener(), gGyroscopeSensor);
            }
            break;
        default:
            Log.w(LOGTAG, "Error! Can't disable unknown SENSOR type " + aSensortype);
        }
    }

    @WrapForJNI
    public static void startMonitoringGamepad() {
        ThreadUtils.postToUiThread(new Runnable() {
                @Override
                public void run() {
                    AndroidGamepadManager.startup();
                }
            });
    }

    @WrapForJNI
    public static void stopMonitoringGamepad() {
        ThreadUtils.postToUiThread(new Runnable() {
                @Override
                public void run() {
                    AndroidGamepadManager.shutdown();
                }
            });
    }

    @WrapForJNI
    public static void gamepadAdded(final int device_id, final int service_id) {
        ThreadUtils.postToUiThread(new Runnable() {
                @Override
                public void run() {
                    AndroidGamepadManager.gamepadAdded(device_id, service_id);
                }
            });
    }

    @WrapForJNI
    public static void moveTaskToBack() {
        if (getGeckoInterface() != null)
            getGeckoInterface().getActivity().moveTaskToBack(true);
    }

    @WrapForJNI
    static void scheduleRestart() {
        getGeckoInterface().doRestart();
    }

    // Creates a homescreen shortcut for a web page.
    // This is the entry point from nsIShellService.
    @WrapForJNI
    public static void createShortcut(final String aTitle, final String aURI) {
        final GeckoInterface geckoInterface = getGeckoInterface();
        if (geckoInterface == null) {
            return;
        }
        geckoInterface.createShortcut(aTitle, aURI);
    }

    @JNITarget
    static public int getPreferredIconSize() {
        if (Versions.feature11Plus) {
            ActivityManager am = (ActivityManager)
                    getApplicationContext().getSystemService(Context.ACTIVITY_SERVICE);
            return am.getLauncherLargeIconSize();
        } else {
            switch (getDpi()) {
                case DisplayMetrics.DENSITY_MEDIUM:
                    return 48;
                case DisplayMetrics.DENSITY_XHIGH:
                    return 96;
                case DisplayMetrics.DENSITY_HIGH:
                default:
                    return 72;
            }
        }
    }

    @WrapForJNI(stubName = "GetHandlersForMimeTypeWrapper")
    static String[] getHandlersForMimeType(String aMimeType, String aAction) {
        final GeckoInterface geckoInterface = getGeckoInterface();
        if (geckoInterface == null) {
            return new String[] {};
        }
        return geckoInterface.getHandlersForMimeType(aMimeType, aAction);
    }

    @WrapForJNI(stubName = "GetHandlersForURLWrapper")
    static String[] getHandlersForURL(String aURL, String aAction) {
        final GeckoInterface geckoInterface = getGeckoInterface();
        if (geckoInterface == null) {
            return new String[] {};
        }
        return geckoInterface.getHandlersForURL(aURL, aAction);
    }

    @WrapForJNI(stubName = "GetHWEncoderCapability")
    static boolean getHWEncoderCapability() {
      return HardwareCodecCapabilityUtils.getHWEncoderCapability();
    }

    @WrapForJNI(stubName = "GetHWDecoderCapability")
    static boolean getHWDecoderCapability() {
      return HardwareCodecCapabilityUtils.getHWDecoderCapability();
    }

    static List<ResolveInfo> queryIntentActivities(Intent intent) {
        final PackageManager pm = getApplicationContext().getPackageManager();

        // Exclude any non-exported activities: we can't open them even if we want to!
        // Bug 1031569 has some details.
        final ArrayList<ResolveInfo> list = new ArrayList<>();
        for (ResolveInfo ri: pm.queryIntentActivities(intent, 0)) {
            if (ri.activityInfo.exported) {
                list.add(ri);
            }
        }

        return list;
    }

    @WrapForJNI(stubName = "GetExtensionFromMimeTypeWrapper")
    static String getExtensionFromMimeType(String aMimeType) {
        return MimeTypeMap.getSingleton().getExtensionFromMimeType(aMimeType);
    }

    @WrapForJNI(stubName = "GetMimeTypeFromExtensionsWrapper")
    static String getMimeTypeFromExtensions(String aFileExt) {
        StringTokenizer st = new StringTokenizer(aFileExt, ".,; ");
        String type = null;
        String subType = null;
        while (st.hasMoreElements()) {
            String ext = st.nextToken();
            String mt = getMimeTypeFromExtension(ext);
            if (mt == null)
                continue;
            int slash = mt.indexOf('/');
            String tmpType = mt.substring(0, slash);
            if (!tmpType.equalsIgnoreCase(type))
                type = type == null ? tmpType : "*";
            String tmpSubType = mt.substring(slash + 1);
            if (!tmpSubType.equalsIgnoreCase(subType))
                subType = subType == null ? tmpSubType : "*";
        }
        if (type == null)
            type = "*";
        if (subType == null)
            subType = "*";
        return type + "/" + subType;
    }

    static boolean isUriSafeForScheme(Uri aUri) {
        // Bug 794034 - We don't want to pass MWI or USSD codes to the
        // dialer, and ensure the Uri class doesn't parse a URI
        // containing a fragment ('#')
        final String scheme = aUri.getScheme();
        if ("tel".equals(scheme) || "sms".equals(scheme)) {
            final String number = aUri.getSchemeSpecificPart();
            if (number.contains("#") || number.contains("*") || aUri.getFragment() != null) {
                return false;
            }
        }
        return true;
    }

    @WrapForJNI
    public static boolean openUriExternal(String targetURI,
                                          String mimeType,
                                          String packageName,
                                          String className,
                                          String action,
                                          String title) {
        final GeckoInterface geckoInterface = getGeckoInterface();
        if (geckoInterface == null) {
            return false;
        }
        return geckoInterface.openUriExternal(targetURI, mimeType, packageName, className, action, title);
    }

    /**
     * Only called from GeckoApp.
     */
    public static void setNotificationClient(NotificationClient client) {
        if (notificationClient == null) {
            notificationClient = client;
        } else {
            Log.d(LOGTAG, "Notification client already set");
        }
    }

    @WrapForJNI(stubName = "ShowAlertNotificationWrapper")
    public static void showAlertNotification(String aImageUrl, String aAlertTitle, String aAlertText, String aAlertCookie, String aAlertName, String aHost) {
        // The intent to launch when the user clicks the expanded notification
        Intent notificationIntent = new Intent(GeckoApp.ACTION_ALERT_CALLBACK);
        notificationIntent.setClassName(AppConstants.ANDROID_PACKAGE_NAME, AppConstants.MOZ_ANDROID_BROWSER_INTENT_CLASS);
        notificationIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);

        int notificationID = aAlertName.hashCode();

        // Put the strings into the intent as an URI "alert:?name=<alertName>&app=<appName>&cookie=<cookie>"
        Uri.Builder b = new Uri.Builder();
        Uri dataUri = b.scheme("alert").path(Integer.toString(notificationID))
                                       .appendQueryParameter("name", aAlertName)
                                       .appendQueryParameter("cookie", aAlertCookie)
                                       .build();
        notificationIntent.setData(dataUri);
        PendingIntent contentIntent = PendingIntent.getActivity(
                getApplicationContext(), 0, notificationIntent, PendingIntent.FLAG_UPDATE_CURRENT);

        ALERT_COOKIES.put(aAlertName, aAlertCookie);
        callObserver(aAlertName, "alertshow", aAlertCookie);

        notificationClient.add(notificationID, aImageUrl, aHost, aAlertTitle, aAlertText, contentIntent);
    }

    @WrapForJNI
    public static void alertsProgressListener_OnProgress(String aAlertName, long aProgress, long aProgressMax, String aAlertText) {
        int notificationID = aAlertName.hashCode();
        notificationClient.update(notificationID, aProgress, aProgressMax, aAlertText);
    }

    @WrapForJNI
    public static void closeNotification(String aAlertName) {
        String alertCookie = ALERT_COOKIES.get(aAlertName);
        if (alertCookie != null) {
            callObserver(aAlertName, "alertfinished", alertCookie);
            ALERT_COOKIES.remove(aAlertName);
        }

        removeObserver(aAlertName);

        int notificationID = aAlertName.hashCode();
        notificationClient.remove(notificationID);
    }

    public static void handleNotification(String aAction, String aAlertName, String aAlertCookie) {
        int notificationID = aAlertName.hashCode();

        if (GeckoApp.ACTION_ALERT_CALLBACK.equals(aAction)) {
            callObserver(aAlertName, "alertclickcallback", aAlertCookie);

            if (notificationClient.isOngoing(notificationID)) {
                // When clicked, keep the notification if it displays progress
                return;
            }
        }
        closeNotification(aAlertName);
    }

    @WrapForJNI(stubName = "GetDpiWrapper")
    public static int getDpi() {
        if (sDensityDpi == 0) {
            sDensityDpi = getApplicationContext().getResources().getDisplayMetrics().densityDpi;
        }

        return sDensityDpi;
    }

    @WrapForJNI
    public static float getDensity() {
        return getApplicationContext().getResources().getDisplayMetrics().density;
    }

    private static boolean isHighMemoryDevice() {
        return HardwareUtils.getMemSize() > HIGH_MEMORY_DEVICE_THRESHOLD_MB;
    }

    /**
     * Returns the colour depth of the default screen. This will either be
     * 24 or 16.
     */
    @WrapForJNI(stubName = "GetScreenDepthWrapper")
    public static synchronized int getScreenDepth() {
        if (sScreenDepth == 0) {
            sScreenDepth = 16;
            PixelFormat info = new PixelFormat();
            final WindowManager wm = (WindowManager)
                    getApplicationContext().getSystemService(Context.WINDOW_SERVICE);
            PixelFormat.getPixelFormatInfo(wm.getDefaultDisplay().getPixelFormat(), info);
            if (info.bitsPerPixel >= 24 && isHighMemoryDevice()) {
                sScreenDepth = 24;
            }
        }

        return sScreenDepth;
    }

    @WrapForJNI
    public static synchronized void setScreenDepthOverride(int aScreenDepth) {
        if (sScreenDepth != 0) {
            Log.e(LOGTAG, "Tried to override screen depth after it's already been set");
            return;
        }

        sScreenDepth = aScreenDepth;
    }

    @WrapForJNI
    public static void setFullScreen(boolean fullscreen) {
        if (getGeckoInterface() != null)
            getGeckoInterface().setFullScreen(fullscreen);
    }

    @WrapForJNI
    public static void performHapticFeedback(boolean aIsLongPress) {
        // Don't perform haptic feedback if a vibration is currently playing,
        // because the haptic feedback will nuke the vibration.
        if (!sVibrationMaybePlaying || System.nanoTime() >= sVibrationEndTime) {
            LayerView layerView = getLayerView();
            layerView.performHapticFeedback(aIsLongPress ?
                                            HapticFeedbackConstants.LONG_PRESS :
                                            HapticFeedbackConstants.VIRTUAL_KEY);
        }
    }

    private static Vibrator vibrator() {
        return (Vibrator) getApplicationContext().getSystemService(Context.VIBRATOR_SERVICE);
    }

    // Helper method to convert integer array to long array.
    private static long[] convertIntToLongArray(int[] input) {
        long[] output = new long[input.length];
        for (int i = 0; i < input.length; i++) {
            output[i] = input[i];
        }
        return output;
    }

    // Vibrate only if haptic feedback is enabled.
    public static void vibrateOnHapticFeedbackEnabled(int[] milliseconds) {
        if (Settings.System.getInt(getApplicationContext().getContentResolver(),
                                   Settings.System.HAPTIC_FEEDBACK_ENABLED, 0) > 0) {
            vibrate(convertIntToLongArray(milliseconds), -1);
        }
    }

    @WrapForJNI(stubName = "Vibrate1")
    public static void vibrate(long milliseconds) {
        sVibrationEndTime = System.nanoTime() + milliseconds * 1000000;
        sVibrationMaybePlaying = true;
        vibrator().vibrate(milliseconds);
    }

    @WrapForJNI(stubName = "VibrateA")
    public static void vibrate(long[] pattern, int repeat) {
        // If pattern.length is even, the last element in the pattern is a
        // meaningless delay, so don't include it in vibrationDuration.
        long vibrationDuration = 0;
        int iterLen = pattern.length - (pattern.length % 2 == 0 ? 1 : 0);
        for (int i = 0; i < iterLen; i++) {
          vibrationDuration += pattern[i];
        }

        sVibrationEndTime = System.nanoTime() + vibrationDuration * 1000000;
        sVibrationMaybePlaying = true;
        vibrator().vibrate(pattern, repeat);
    }

    @WrapForJNI
    public static void cancelVibrate() {
        sVibrationMaybePlaying = false;
        sVibrationEndTime = 0;
        vibrator().cancel();
    }

    @WrapForJNI
    public static void setKeepScreenOn(final boolean on) {
        ThreadUtils.postToUiThread(new Runnable() {
            @Override
            public void run() {
                // TODO
            }
        });
    }

    @WrapForJNI
    public static void notifyDefaultPrevented(final boolean defaultPrevented) {
        ThreadUtils.postToUiThread(new Runnable() {
            @Override
            public void run() {
                LayerView view = getLayerView();
                PanZoomController controller = (view == null ? null : view.getPanZoomController());
                if (controller != null) {
                    controller.notifyDefaultActionPrevented(defaultPrevented);
                }
            }
        });
    }

    @WrapForJNI
    public static boolean isNetworkLinkUp() {
        ConnectivityManager cm = (ConnectivityManager)
           getApplicationContext().getSystemService(Context.CONNECTIVITY_SERVICE);
        try {
            NetworkInfo info = cm.getActiveNetworkInfo();
            if (info == null || !info.isConnected())
                return false;
        } catch (SecurityException se) {
            return false;
        }
        return true;
    }

    @WrapForJNI
    public static boolean isNetworkLinkKnown() {
        ConnectivityManager cm = (ConnectivityManager)
            getApplicationContext().getSystemService(Context.CONNECTIVITY_SERVICE);
        try {
            if (cm.getActiveNetworkInfo() == null)
                return false;
        } catch (SecurityException se) {
            return false;
        }
        return true;
    }

    @WrapForJNI
    public static int networkLinkType() {
        ConnectivityManager cm = (ConnectivityManager)
            getApplicationContext().getSystemService(Context.CONNECTIVITY_SERVICE);
        NetworkInfo info = cm.getActiveNetworkInfo();
        if (info == null) {
            return LINK_TYPE_UNKNOWN;
        }

        switch (info.getType()) {
            case ConnectivityManager.TYPE_ETHERNET:
                return LINK_TYPE_ETHERNET;
            case ConnectivityManager.TYPE_WIFI:
                return LINK_TYPE_WIFI;
            case ConnectivityManager.TYPE_WIMAX:
                return LINK_TYPE_WIMAX;
            case ConnectivityManager.TYPE_MOBILE:
                break; // We will handle sub-types after the switch.
            default:
                Log.w(LOGTAG, "Ignoring the current network type.");
                return LINK_TYPE_UNKNOWN;
        }

        TelephonyManager tm = (TelephonyManager)
            getApplicationContext().getSystemService(Context.TELEPHONY_SERVICE);
        if (tm == null) {
            Log.e(LOGTAG, "Telephony service does not exist");
            return LINK_TYPE_UNKNOWN;
        }

        switch (tm.getNetworkType()) {
            case TelephonyManager.NETWORK_TYPE_IDEN:
            case TelephonyManager.NETWORK_TYPE_CDMA:
            case TelephonyManager.NETWORK_TYPE_GPRS:
                return LINK_TYPE_2G;
            case TelephonyManager.NETWORK_TYPE_1xRTT:
            case TelephonyManager.NETWORK_TYPE_EDGE:
                return LINK_TYPE_2G; // 2.5G
            case TelephonyManager.NETWORK_TYPE_UMTS:
            case TelephonyManager.NETWORK_TYPE_EVDO_0:
                return LINK_TYPE_3G;
            case TelephonyManager.NETWORK_TYPE_HSPA:
            case TelephonyManager.NETWORK_TYPE_HSDPA:
            case TelephonyManager.NETWORK_TYPE_HSUPA:
            case TelephonyManager.NETWORK_TYPE_EVDO_A:
            case TelephonyManager.NETWORK_TYPE_EVDO_B:
            case TelephonyManager.NETWORK_TYPE_EHRPD:
                return LINK_TYPE_3G; // 3.5G
            case TelephonyManager.NETWORK_TYPE_HSPAP:
                return LINK_TYPE_3G; // 3.75G
            case TelephonyManager.NETWORK_TYPE_LTE:
                return LINK_TYPE_4G; // 3.9G
            case TelephonyManager.NETWORK_TYPE_UNKNOWN:
            default:
                Log.w(LOGTAG, "Connected to an unknown mobile network!");
                return LINK_TYPE_UNKNOWN;
        }
    }

    @WrapForJNI(stubName = "GetSystemColoursWrapper")
    public static int[] getSystemColors() {
        // attrsAppearance[] must correspond to AndroidSystemColors structure in android/AndroidBridge.h
        final int[] attrsAppearance = {
            android.R.attr.textColor,
            android.R.attr.textColorPrimary,
            android.R.attr.textColorPrimaryInverse,
            android.R.attr.textColorSecondary,
            android.R.attr.textColorSecondaryInverse,
            android.R.attr.textColorTertiary,
            android.R.attr.textColorTertiaryInverse,
            android.R.attr.textColorHighlight,
            android.R.attr.colorForeground,
            android.R.attr.colorBackground,
            android.R.attr.panelColorForeground,
            android.R.attr.panelColorBackground
        };

        int[] result = new int[attrsAppearance.length];

        final ContextThemeWrapper contextThemeWrapper =
            new ContextThemeWrapper(getApplicationContext(), android.R.style.TextAppearance);

        final TypedArray appearance = contextThemeWrapper.getTheme().obtainStyledAttributes(attrsAppearance);

        if (appearance != null) {
            for (int i = 0; i < appearance.getIndexCount(); i++) {
                int idx = appearance.getIndex(i);
                int color = appearance.getColor(idx, 0);
                result[idx] = color;
            }
            appearance.recycle();
        }

        return result;
    }

    @WrapForJNI
    public static void killAnyZombies() {
        GeckoProcessesVisitor visitor = new GeckoProcessesVisitor() {
            @Override
            public boolean callback(int pid) {
                if (pid != android.os.Process.myPid())
                    android.os.Process.killProcess(pid);
                return true;
            }
        };

        EnumerateGeckoProcesses(visitor);
    }

    interface GeckoProcessesVisitor {
        boolean callback(int pid);
    }

    private static void EnumerateGeckoProcesses(GeckoProcessesVisitor visiter) {
        int pidColumn = -1;
        int userColumn = -1;

        try {
            // run ps and parse its output
            java.lang.Process ps = Runtime.getRuntime().exec("ps");
            BufferedReader in = new BufferedReader(new InputStreamReader(ps.getInputStream()),
                                                   2048);

            String headerOutput = in.readLine();

            // figure out the column offsets.  We only care about the pid and user fields
            StringTokenizer st = new StringTokenizer(headerOutput);

            int tokenSoFar = 0;
            while (st.hasMoreTokens()) {
                String next = st.nextToken();
                if (next.equalsIgnoreCase("PID"))
                    pidColumn = tokenSoFar;
                else if (next.equalsIgnoreCase("USER"))
                    userColumn = tokenSoFar;
                tokenSoFar++;
            }

            // alright, the rest are process entries.
            String psOutput = null;
            while ((psOutput = in.readLine()) != null) {
                String[] split = psOutput.split("\\s+");
                if (split.length <= pidColumn || split.length <= userColumn)
                    continue;
                int uid = android.os.Process.getUidForName(split[userColumn]);
                if (uid == android.os.Process.myUid() &&
                    !split[split.length - 1].equalsIgnoreCase("ps")) {
                    int pid = Integer.parseInt(split[pidColumn]);
                    boolean keepGoing = visiter.callback(pid);
                    if (keepGoing == false)
                        break;
                }
            }
            in.close();
        }
        catch (Exception e) {
            Log.w(LOGTAG, "Failed to enumerate Gecko processes.",  e);
        }
    }

    public static String getAppNameByPID(int pid) {
        BufferedReader cmdlineReader = null;
        String path = "/proc/" + pid + "/cmdline";
        try {
            File cmdlineFile = new File(path);
            if (!cmdlineFile.exists())
                return "";
            cmdlineReader = new BufferedReader(new FileReader(cmdlineFile));
            return cmdlineReader.readLine().trim();
        } catch (Exception ex) {
            return "";
        } finally {
            if (null != cmdlineReader) {
                try {
                    cmdlineReader.close();
                } catch (Exception e) { }
            }
        }
    }

    public static void listOfOpenFiles() {
        int pidColumn = -1;
        int nameColumn = -1;

        try {
            String filter = GeckoProfile.get(getApplicationContext()).getDir().toString();
            Log.d(LOGTAG, "[OPENFILE] Filter: " + filter);

            // run lsof and parse its output
            java.lang.Process lsof = Runtime.getRuntime().exec("lsof");
            BufferedReader in = new BufferedReader(new InputStreamReader(lsof.getInputStream()), 2048);

            String headerOutput = in.readLine();
            StringTokenizer st = new StringTokenizer(headerOutput);
            int token = 0;
            while (st.hasMoreTokens()) {
                String next = st.nextToken();
                if (next.equalsIgnoreCase("PID"))
                    pidColumn = token;
                else if (next.equalsIgnoreCase("NAME"))
                    nameColumn = token;
                token++;
            }

            // alright, the rest are open file entries.
            Map<Integer, String> pidNameMap = new TreeMap<Integer, String>();
            String output = null;
            while ((output = in.readLine()) != null) {
                String[] split = output.split("\\s+");
                if (split.length <= pidColumn || split.length <= nameColumn)
                    continue;
                final Integer pid = Integer.valueOf(split[pidColumn]);
                String name = pidNameMap.get(pid);
                if (name == null) {
                    name = getAppNameByPID(pid.intValue());
                    pidNameMap.put(pid, name);
                }
                String file = split[nameColumn];
                if (!TextUtils.isEmpty(name) && !TextUtils.isEmpty(file) && file.startsWith(filter))
                    Log.d(LOGTAG, "[OPENFILE] " + name + "(" + split[pidColumn] + ") : " + file);
            }
            in.close();
        } catch (Exception e) { }
    }

    @WrapForJNI(stubName = "GetIconForExtensionWrapper")
    public static byte[] getIconForExtension(String aExt, int iconSize) {
        try {
            if (iconSize <= 0)
                iconSize = 16;

            if (aExt != null && aExt.length() > 1 && aExt.charAt(0) == '.')
                aExt = aExt.substring(1);

            PackageManager pm = getApplicationContext().getPackageManager();
            Drawable icon = getDrawableForExtension(pm, aExt);
            if (icon == null) {
                // Use a generic icon
                icon = pm.getDefaultActivityIcon();
            }

            Bitmap bitmap = ((BitmapDrawable)icon).getBitmap();
            if (bitmap.getWidth() != iconSize || bitmap.getHeight() != iconSize)
                bitmap = Bitmap.createScaledBitmap(bitmap, iconSize, iconSize, true);

            ByteBuffer buf = ByteBuffer.allocate(iconSize * iconSize * 4);
            bitmap.copyPixelsToBuffer(buf);

            return buf.array();
        }
        catch (Exception e) {
            Log.w(LOGTAG, "getIconForExtension failed.",  e);
            return null;
        }
    }

    public static String getMimeTypeFromExtension(String ext) {
        final MimeTypeMap mtm = MimeTypeMap.getSingleton();
        return mtm.getMimeTypeFromExtension(ext);
    }

    private static Drawable getDrawableForExtension(PackageManager pm, String aExt) {
        Intent intent = new Intent(Intent.ACTION_VIEW);
        final String mimeType = getMimeTypeFromExtension(aExt);
        if (mimeType != null && mimeType.length() > 0)
            intent.setType(mimeType);
        else
            return null;

        List<ResolveInfo> list = pm.queryIntentActivities(intent, 0);
        if (list.size() == 0)
            return null;

        ResolveInfo resolveInfo = list.get(0);

        if (resolveInfo == null)
            return null;

        ActivityInfo activityInfo = resolveInfo.activityInfo;

        return activityInfo.loadIcon(pm);
    }

    @WrapForJNI
    public static boolean getShowPasswordSetting() {
        try {
            int showPassword =
                Settings.System.getInt(getApplicationContext().getContentResolver(),
                                       Settings.System.TEXT_SHOW_PASSWORD, 1);
            return (showPassword > 0);
        }
        catch (Exception e) {
            return true;
        }
    }

    @WrapForJNI(stubName = "AddPluginViewWrapper")
    public static void addPluginView(View view,
                                     float x, float y,
                                     float w, float h,
                                     boolean isFullScreen) {
        if (getGeckoInterface() != null)
             getGeckoInterface().addPluginView(view, new RectF(x, y, x + w, y + h), isFullScreen);
    }

    @WrapForJNI
    public static void removePluginView(View view, boolean isFullScreen) {
        if (getGeckoInterface() != null)
            getGeckoInterface().removePluginView(view, isFullScreen);
    }

    /**
     * A plugin that wish to be loaded in the WebView must provide this permission
     * in their AndroidManifest.xml.
     */
    public static final String PLUGIN_ACTION = "android.webkit.PLUGIN";
    public static final String PLUGIN_PERMISSION = "android.webkit.permission.PLUGIN";

    private static final String PLUGIN_SYSTEM_LIB = "/system/lib/plugins/";

    private static final String PLUGIN_TYPE = "type";
    private static final String TYPE_NATIVE = "native";
    public static final ArrayList<PackageInfo> mPackageInfoCache = new ArrayList<>();

    // Returns null if plugins are blocked on the device.
    static String[] getPluginDirectories() {

        // Block on Pixel C.
        if ((new File("/system/lib/hw/power.dragon.so")).exists()) {
            Log.w(LOGTAG, "Blocking plugins because of Pixel C device (bug 1255122)");
            return null;
        }
        // An awful hack to detect Tegra devices. Easiest way to do it without spinning up a EGL context.
        boolean isTegra = (new File("/system/lib/hw/gralloc.tegra.so")).exists() ||
                          (new File("/system/lib/hw/gralloc.tegra3.so")).exists() ||
                          (new File("/sys/class/nvidia-gpu")).exists();
        if (isTegra) {
            // disable on KitKat (bug 957694)
            if (Versions.feature19Plus) {
                Log.w(LOGTAG, "Blocking plugins because of Tegra (bug 957694)");
                return null;
            }

            // disable Flash on Tegra ICS with CM9 and other custom firmware (bug 736421)
            final File vfile = new File("/proc/version");
            try {
                if (vfile.canRead()) {
                    final BufferedReader reader = new BufferedReader(new FileReader(vfile));
                    try {
                        final String version = reader.readLine();
                        if (version.indexOf("CM9") != -1 ||
                            version.indexOf("cyanogen") != -1 ||
                            version.indexOf("Nova") != -1) {
                            Log.w(LOGTAG, "Blocking plugins because of Tegra 2 + unofficial ICS bug (bug 736421)");
                            return null;
                        }
                    } finally {
                      reader.close();
                    }
                }
            } catch (IOException ex) {
                // Do nothing.
            }
        }

        ArrayList<String> directories = new ArrayList<String>();
        PackageManager pm = getApplicationContext().getPackageManager();
        List<ResolveInfo> plugins = pm.queryIntentServices(new Intent(PLUGIN_ACTION),
                PackageManager.GET_SERVICES | PackageManager.GET_META_DATA);

        synchronized (mPackageInfoCache) {

            // clear the list of existing packageInfo objects
            mPackageInfoCache.clear();


            for (ResolveInfo info : plugins) {

                // retrieve the plugin's service information
                ServiceInfo serviceInfo = info.serviceInfo;
                if (serviceInfo == null) {
                    Log.w(LOGTAG, "Ignoring bad plugin.");
                    continue;
                }

                // Blacklist HTC's flash lite.
                // See bug #704516 - We're not quite sure what Flash Lite does,
                // but loading it causes Flash to give errors and fail to draw.
                if (serviceInfo.packageName.equals("com.htc.flashliteplugin")) {
                    Log.w(LOGTAG, "Skipping HTC's flash lite plugin");
                    continue;
                }


                // Retrieve information from the plugin's manifest.
                PackageInfo pkgInfo;
                try {
                    pkgInfo = pm.getPackageInfo(serviceInfo.packageName,
                                    PackageManager.GET_PERMISSIONS
                                    | PackageManager.GET_SIGNATURES);
                } catch (Exception e) {
                    Log.w(LOGTAG, "Can't find plugin: " + serviceInfo.packageName);
                    continue;
                }

                if (pkgInfo == null) {
                    Log.w(LOGTAG, "Not loading plugin: " + serviceInfo.packageName + ". Could not load package information.");
                    continue;
                }

                /*
                 * find the location of the plugin's shared library. The default
                 * is to assume the app is either a user installed app or an
                 * updated system app. In both of these cases the library is
                 * stored in the app's data directory.
                 */
                String directory = pkgInfo.applicationInfo.dataDir + "/lib";
                final int appFlags = pkgInfo.applicationInfo.flags;
                final int updatedSystemFlags = ApplicationInfo.FLAG_SYSTEM |
                                               ApplicationInfo.FLAG_UPDATED_SYSTEM_APP;

                // preloaded system app with no user updates
                if ((appFlags & updatedSystemFlags) == ApplicationInfo.FLAG_SYSTEM) {
                    directory = PLUGIN_SYSTEM_LIB + pkgInfo.packageName;
                }

                // check if the plugin has the required permissions
                String permissions[] = pkgInfo.requestedPermissions;
                if (permissions == null) {
                    Log.w(LOGTAG, "Not loading plugin: " + serviceInfo.packageName + ". Does not have required permission.");
                    continue;
                }
                boolean permissionOk = false;
                for (String permit : permissions) {
                    if (PLUGIN_PERMISSION.equals(permit)) {
                        permissionOk = true;
                        break;
                    }
                }
                if (!permissionOk) {
                    Log.w(LOGTAG, "Not loading plugin: " + serviceInfo.packageName + ". Does not have required permission (2).");
                    continue;
                }

                // check to ensure the plugin is properly signed
                Signature signatures[] = pkgInfo.signatures;
                if (signatures == null) {
                    Log.w(LOGTAG, "Not loading plugin: " + serviceInfo.packageName + ". Not signed.");
                    continue;
                }

                // determine the type of plugin from the manifest
                if (serviceInfo.metaData == null) {
                    Log.e(LOGTAG, "The plugin '" + serviceInfo.name + "' has no defined type.");
                    continue;
                }

                String pluginType = serviceInfo.metaData.getString(PLUGIN_TYPE);
                if (!TYPE_NATIVE.equals(pluginType)) {
                    Log.e(LOGTAG, "Unrecognized plugin type: " + pluginType);
                    continue;
                }

                try {
                    Class<?> cls = getPluginClass(serviceInfo.packageName, serviceInfo.name);

                    //TODO implement any requirements of the plugin class here!
                    boolean classFound = true;

                    if (!classFound) {
                        Log.e(LOGTAG, "The plugin's class' " + serviceInfo.name + "' does not extend the appropriate class.");
                        continue;
                    }

                } catch (NameNotFoundException e) {
                    Log.e(LOGTAG, "Can't find plugin: " + serviceInfo.packageName);
                    continue;
                } catch (ClassNotFoundException e) {
                    Log.e(LOGTAG, "Can't find plugin's class: " + serviceInfo.name);
                    continue;
                }

                // if all checks have passed then make the plugin available
                mPackageInfoCache.add(pkgInfo);
                directories.add(directory);
            }
        }

        return directories.toArray(new String[directories.size()]);
    }

    static String getPluginPackage(String pluginLib) {

        if (pluginLib == null || pluginLib.length() == 0) {
            return null;
        }

        synchronized (mPackageInfoCache) {
            for (PackageInfo pkgInfo : mPackageInfoCache) {
                if (pluginLib.contains(pkgInfo.packageName)) {
                    return pkgInfo.packageName;
                }
            }
        }

        return null;
    }

    static Class<?> getPluginClass(String packageName, String className)
            throws NameNotFoundException, ClassNotFoundException {
        Context pluginContext = getApplicationContext().createPackageContext(packageName,
                Context.CONTEXT_INCLUDE_CODE |
                Context.CONTEXT_IGNORE_SECURITY);
        ClassLoader pluginCL = pluginContext.getClassLoader();
        return pluginCL.loadClass(className);
    }

    @WrapForJNI(allowMultithread = true)
    public static Class<?> loadPluginClass(String className, String libName) {
        if (getGeckoInterface() == null)
            return null;
        try {
            final String packageName = getPluginPackage(libName);
            final int contextFlags = Context.CONTEXT_INCLUDE_CODE | Context.CONTEXT_IGNORE_SECURITY;
            final Context pluginContext = getApplicationContext().createPackageContext(
                    packageName, contextFlags);
            return pluginContext.getClassLoader().loadClass(className);
        } catch (java.lang.ClassNotFoundException cnfe) {
            Log.w(LOGTAG, "Couldn't find plugin class " + className, cnfe);
            return null;
        } catch (android.content.pm.PackageManager.NameNotFoundException nnfe) {
            Log.w(LOGTAG, "Couldn't find package.", nnfe);
            return null;
        }
    }

    private static Context sApplicationContext;
    private static ContextGetter sContextGetter;

    @Deprecated
    @WrapForJNI(allowMultithread = true)
    public static Context getContext() {
        return sContextGetter.getContext();
    }

    public static void setContextGetter(ContextGetter cg) {
        sContextGetter = cg;
    }

    @WrapForJNI(allowMultithread = true)
    public static Context getApplicationContext() {
        return sApplicationContext;
    }

    public static void setApplicationContext(final Context context) {
        sApplicationContext = context;
    }

    public static SharedPreferences getSharedPreferences() {
        if (sContextGetter == null) {
            throw new IllegalStateException("No ContextGetter; cannot fetch prefs.");
        }
        return sContextGetter.getSharedPreferences();
    }

    public interface AppStateListener {
        public void onPause();
        public void onResume();
        public void onOrientationChanged();
    }

    public interface GeckoInterface {
        public GeckoProfile getProfile();
        public Activity getActivity();
        public String getDefaultUAString();
        public LocationListener getLocationListener();
        public SensorEventListener getSensorEventListener();
        public void doRestart();
        public void setFullScreen(boolean fullscreen);
        public void addPluginView(View view, final RectF rect, final boolean isFullScreen);
        public void removePluginView(final View view, final boolean isFullScreen);
        public void enableCameraView();
        public void disableCameraView();
        public void addAppStateListener(AppStateListener listener);
        public void removeAppStateListener(AppStateListener listener);
        public View getCameraView();
        public void notifyWakeLockChanged(String topic, String state);
        public FormAssistPopup getFormAssistPopup();
        public boolean areTabsShown();
        public AbsoluteLayout getPluginContainer();
        public void notifyCheckUpdateResult(String result);
        public void invalidateOptionsMenu();

        /**
         * Create a shortcut -- generally a home-screen icon -- linking the given title to the given URI.
         * <p>
         * This method is always invoked on the Gecko thread.
         *
         * @param title of URI to link to.
         * @param URI to link to.
         */
        public void createShortcut(String title, String URI);

        /**
         * Check if the given URI is visited.
         * <p/>
         * If it has been visited, call {@link GeckoAppShell#notifyUriVisited(String)}.  (If it
         * has not been visited, do nothing.)
         * <p/>
         * This method is always invoked on the Gecko thread.
         *
         * @param uri to check.
         */
        public void checkUriVisited(String uri);

        /**
         * Mark the given URI as visited in Gecko.
         * <p/>
         * Implementors may maintain some local store of visited URIs in order to be able to
         * answer {@link #checkUriVisited(String)} requests affirmatively.
         * <p/>
         * This method is always invoked on the Gecko thread.
         *
         * @param uri to mark.
         */
        public void markUriVisited(final String uri);

        /**
         * Set the title of the given URI, as determined by Gecko.
         * <p/>
         * This method is always invoked on the Gecko thread.
         *
         * @param uri given.
         * @param title to associate with the given URI.
         */
        public void setUriTitle(final String uri, final String title);

        public void setAccessibilityEnabled(boolean enabled);

        public boolean openUriExternal(String targetURI, String mimeType, String packageName, String className, String action, String title);

        public String[] getHandlersForMimeType(String mimeType, String action);
        public String[] getHandlersForURL(String url, String action);
    };

    private static GeckoInterface sGeckoInterface;

    public static GeckoInterface getGeckoInterface() {
        return sGeckoInterface;
    }

    public static void setGeckoInterface(GeckoInterface aGeckoInterface) {
        sGeckoInterface = aGeckoInterface;
    }

    public static android.hardware.Camera sCamera;

    static native void cameraCallbackBridge(byte[] data);

    static final int kPreferredFPS = 25;
    static byte[] sCameraBuffer;


    @WrapForJNI(stubName = "InitCameraWrapper")
    static int[] initCamera(String aContentType, int aCamera, int aWidth, int aHeight) {
        ThreadUtils.postToUiThread(new Runnable() {
                @Override
                public void run() {
                    try {
                        if (getGeckoInterface() != null)
                            getGeckoInterface().enableCameraView();
                    } catch (Exception e) { }
                }
            });

        // [0] = 0|1 (failure/success)
        // [1] = width
        // [2] = height
        // [3] = fps
        int[] result = new int[4];
        result[0] = 0;

        if (android.hardware.Camera.getNumberOfCameras() == 0) {
            return result;
        }

        try {
            sCamera = android.hardware.Camera.open(aCamera);

            android.hardware.Camera.Parameters params = sCamera.getParameters();
            params.setPreviewFormat(ImageFormat.NV21);

            // use the preview fps closest to 25 fps.
            int fpsDelta = 1000;
            try {
                Iterator<Integer> it = params.getSupportedPreviewFrameRates().iterator();
                while (it.hasNext()) {
                    int nFps = it.next();
                    if (Math.abs(nFps - kPreferredFPS) < fpsDelta) {
                        fpsDelta = Math.abs(nFps - kPreferredFPS);
                        params.setPreviewFrameRate(nFps);
                    }
                }
            } catch (Exception e) {
                params.setPreviewFrameRate(kPreferredFPS);
            }

            // set up the closest preview size available
            Iterator<android.hardware.Camera.Size> sit = params.getSupportedPreviewSizes().iterator();
            int sizeDelta = 10000000;
            int bufferSize = 0;
            while (sit.hasNext()) {
                android.hardware.Camera.Size size = sit.next();
                if (Math.abs(size.width * size.height - aWidth * aHeight) < sizeDelta) {
                    sizeDelta = Math.abs(size.width * size.height - aWidth * aHeight);
                    params.setPreviewSize(size.width, size.height);
                    bufferSize = size.width * size.height;
                }
            }

            try {
                if (getGeckoInterface() != null) {
                    View cameraView = getGeckoInterface().getCameraView();
                    if (cameraView instanceof SurfaceView) {
                        sCamera.setPreviewDisplay(((SurfaceView)cameraView).getHolder());
                    } else if (cameraView instanceof TextureView) {
                        sCamera.setPreviewTexture(((TextureView)cameraView).getSurfaceTexture());
                    }
                }
            } catch (IOException | RuntimeException e) {
                Log.w(LOGTAG, "Error setPreviewXXX:", e);
            }

            sCamera.setParameters(params);
            sCameraBuffer = new byte[(bufferSize * 12) / 8];
            sCamera.addCallbackBuffer(sCameraBuffer);
            sCamera.setPreviewCallbackWithBuffer(new android.hardware.Camera.PreviewCallback() {
                @Override
                public void onPreviewFrame(byte[] data, android.hardware.Camera camera) {
                    cameraCallbackBridge(data);
                    if (sCamera != null)
                        sCamera.addCallbackBuffer(sCameraBuffer);
                }
            });
            sCamera.startPreview();
            params = sCamera.getParameters();
            result[0] = 1;
            result[1] = params.getPreviewSize().width;
            result[2] = params.getPreviewSize().height;
            result[3] = params.getPreviewFrameRate();
        } catch (RuntimeException e) {
            Log.w(LOGTAG, "initCamera RuntimeException.", e);
            result[0] = result[1] = result[2] = result[3] = 0;
        }
        return result;
    }

    @WrapForJNI
    static synchronized void closeCamera() {
        ThreadUtils.postToUiThread(new Runnable() {
                @Override
                public void run() {
                    try {
                        if (getGeckoInterface() != null)
                            getGeckoInterface().disableCameraView();
                    } catch (Exception e) { }
                }
            });
        if (sCamera != null) {
            sCamera.stopPreview();
            sCamera.release();
            sCamera = null;
            sCameraBuffer = null;
        }
    }

    /*
     * Battery API related methods.
     */
    @WrapForJNI
    public static void enableBatteryNotifications() {
        GeckoBatteryManager.enableNotifications();
    }

    @WrapForJNI(stubName = "HandleGeckoMessageWrapper")
    public static void handleGeckoMessage(final NativeJSContainer message) {
        EventDispatcher.getInstance().dispatchEvent(message);
        message.disposeNative();
    }

    @WrapForJNI
    public static void disableBatteryNotifications() {
        GeckoBatteryManager.disableNotifications();
    }

    @WrapForJNI(stubName = "GetCurrentBatteryInformationWrapper")
    public static double[] getCurrentBatteryInformation() {
        return GeckoBatteryManager.getCurrentInformation();
    }

    @WrapForJNI(stubName = "CheckURIVisited")
    static void checkUriVisited(String uri) {
        final GeckoInterface geckoInterface = getGeckoInterface();
        if (geckoInterface == null) {
            return;
        }
        geckoInterface.checkUriVisited(uri);
    }

    @WrapForJNI(stubName = "MarkURIVisited")
    static void markUriVisited(final String uri) {
        final GeckoInterface geckoInterface = getGeckoInterface();
        if (geckoInterface == null) {
            return;
        }
        geckoInterface.markUriVisited(uri);
    }

    @WrapForJNI(stubName = "SetURITitle")
    static void setUriTitle(final String uri, final String title) {
        final GeckoInterface geckoInterface = getGeckoInterface();
        if (geckoInterface == null) {
            return;
        }
        geckoInterface.setUriTitle(uri, title);
    }

    @WrapForJNI
    static void hideProgressDialog() {
        // unused stub
    }

    /*
     * WebSMS related methods.
     */
    public static void sendMessage(String aNumber, String aMessage, int aRequestId, boolean aShouldNotify) {
        if (!SmsManager.isEnabled()) {
            return;
        }

        SmsManager.getInstance().send(aNumber, aMessage, aRequestId, aShouldNotify);
    }

    @WrapForJNI(stubName = "SendMessageWrapper")
    public static void sendMessage(String aNumber, String aMessage, int aRequestId) {
        sendMessage(aNumber, aMessage, aRequestId, /* shouldNotify */ true);
    }

    @WrapForJNI(stubName = "GetMessageWrapper")
    public static void getMessage(int aMessageId, int aRequestId) {
        if (!SmsManager.isEnabled()) {
            return;
        }

        SmsManager.getInstance().getMessage(aMessageId, aRequestId);
    }

    @WrapForJNI(stubName = "DeleteMessageWrapper")
    public static void deleteMessage(int aMessageId, int aRequestId) {
        if (!SmsManager.isEnabled()) {
            return;
        }

        SmsManager.getInstance().deleteMessage(aMessageId, aRequestId);
    }

    @WrapForJNI
    public static void markMessageRead(int aMessageId, boolean aValue, boolean aSendReadReport, int aRequestId) {
        if (!SmsManager.isEnabled()) {
            return;
        }

        SmsManager.getInstance().markMessageRead(aMessageId, aValue, aSendReadReport, aRequestId);
    }

    @WrapForJNI(stubName = "CreateMessageCursorWrapper")
    public static void createMessageCursor(long aStartDate, long aEndDate, String[] aNumbers, int aNumbersCount, String aDelivery, boolean aHasRead, boolean aRead, boolean aHasThreadId, long aThreadId, boolean aReverse, int aRequestId) {
        if (!SmsManager.isEnabled()) {
            return;
        }

        SmsManager.getInstance().createMessageCursor(aStartDate, aEndDate, aNumbers, aNumbersCount, aDelivery, aHasRead, aRead, aHasThreadId, aThreadId, aReverse, aRequestId);
    }

    @WrapForJNI(stubName = "GetNextMessageWrapper")
    public static void getNextMessage(int aRequestId) {
        if (!SmsManager.isEnabled()) {
            return;
        }

        SmsManager.getInstance().getNextMessage(aRequestId);
    }

    @WrapForJNI(stubName = "CreateThreadCursorWrapper")
    public static void createThreadCursor(int aRequestId) {
        Log.i("GeckoAppShell", "CreateThreadCursorWrapper!");

        if (!SmsManager.isEnabled()) {
            return;
        }

        SmsManager.getInstance().createThreadCursor(aRequestId);
    }

    @WrapForJNI(stubName = "GetNextThreadWrapper")
    public static void getNextThread(int aRequestId) {
        if (!SmsManager.isEnabled()) {
            return;
        }

        SmsManager.getInstance().getNextThread(aRequestId);
    }

    /* Called by JNI from AndroidBridge, and by reflection from tests/BaseTest.java.in */
    @WrapForJNI
    @RobocopTarget
    public static boolean isTablet() {
        return HardwareUtils.isTablet();
    }

    private static boolean sImeWasEnabledOnLastResize = false;
    public static void viewSizeChanged() {
        GeckoView v = (GeckoView) getLayerView();
        if (v == null) {
            return;
        }
        boolean imeIsEnabled = v.isIMEEnabled();
        if (imeIsEnabled && !sImeWasEnabledOnLastResize) {
            // The IME just came up after not being up, so let's scroll
            // to the focused input.
            notifyObservers("ScrollTo:FocusedInput", "");
        }
        sImeWasEnabledOnLastResize = imeIsEnabled;
    }

    @WrapForJNI(stubName = "GetCurrentNetworkInformationWrapper")
    public static double[] getCurrentNetworkInformation() {
        return GeckoNetworkManager.getInstance().getCurrentInformation();
    }

    @WrapForJNI
    public static void enableNetworkNotifications() {
        ThreadUtils.postToUiThread(new Runnable() {
            @Override
            public void run() {
                GeckoNetworkManager.getInstance().enableNotifications();
            }
        });
    }

    @WrapForJNI
    public static void disableNetworkNotifications() {
        ThreadUtils.postToUiThread(new Runnable() {
            @Override
            public void run() {
                GeckoNetworkManager.getInstance().disableNotifications();
            }
        });
    }

    @WrapForJNI(stubName = "GetScreenOrientationWrapper")
    public static short getScreenOrientation() {
        return GeckoScreenOrientation.getInstance().getScreenOrientation().value;
    }

    @WrapForJNI
    public static int getScreenAngle() {
        return GeckoScreenOrientation.getInstance().getAngle();
    }

    @WrapForJNI
    public static void enableScreenOrientationNotifications() {
        GeckoScreenOrientation.getInstance().enableNotifications();
    }

    @WrapForJNI
    public static void disableScreenOrientationNotifications() {
        GeckoScreenOrientation.getInstance().disableNotifications();
    }

    @WrapForJNI
    public static void lockScreenOrientation(int aOrientation) {
        GeckoScreenOrientation.getInstance().lock(aOrientation);
    }

    @WrapForJNI
    public static void unlockScreenOrientation() {
        GeckoScreenOrientation.getInstance().unlock();
    }

    @WrapForJNI
    public static void notifyWakeLockChanged(String topic, String state) {
        if (getGeckoInterface() != null)
            getGeckoInterface().notifyWakeLockChanged(topic, state);
    }

    @WrapForJNI(allowMultithread = true)
    public static void registerSurfaceTextureFrameListener(Object surfaceTexture, final int id) {
        ((SurfaceTexture)surfaceTexture).setOnFrameAvailableListener(new SurfaceTexture.OnFrameAvailableListener() {
            @Override
            public void onFrameAvailable(SurfaceTexture surfaceTexture) {
                GeckoAppShell.onSurfaceTextureFrameAvailable(surfaceTexture, id);
            }
        });
    }

    @WrapForJNI(allowMultithread = true)
    public static void unregisterSurfaceTextureFrameListener(Object surfaceTexture) {
        ((SurfaceTexture)surfaceTexture).setOnFrameAvailableListener(null);
    }

    @WrapForJNI
    public static boolean unlockProfile() {
        // Try to kill any zombie Fennec's that might be running
        GeckoAppShell.killAnyZombies();

        // Then force unlock this profile
        if (getGeckoInterface() != null) {
            GeckoProfile profile = getGeckoInterface().getProfile();
            File lock = profile.getFile(".parentlock");
            return lock.exists() && lock.delete();
        }
        return false;
    }

    @WrapForJNI(stubName = "GetProxyForURIWrapper")
    public static String getProxyForURI(String spec, String scheme, String host, int port) {
        final ProxySelector ps = new ProxySelector();

        Proxy proxy = ps.select(scheme, host);
        if (Proxy.NO_PROXY.equals(proxy)) {
            return "DIRECT";
        }

        switch (proxy.type()) {
            case HTTP:
                return "PROXY " + proxy.address().toString();
            case SOCKS:
                return "SOCKS " + proxy.address().toString();
        }

        return "DIRECT";
    }

    @WrapForJNI(allowMultithread = true)
    static InputStream createInputStream(URLConnection connection) throws IOException {
        return connection.getInputStream();
    }

    private static class BitmapConnection extends URLConnection {
        private Bitmap bitmap;

        BitmapConnection(Bitmap b) throws MalformedURLException, IOException {
            super(null);
            bitmap = b;
        }

        @Override
        public void connect() {}

        @Override
        public InputStream getInputStream() throws IOException {
            return new BitmapInputStream();
        }

        @Override
        public String getContentType() {
            return "image/png";
        }

        private final class BitmapInputStream extends PipedInputStream {
            private boolean mHaveConnected = false;

            @Override
            public synchronized int read(byte[] buffer, int byteOffset, int byteCount)
                                    throws IOException {
                if (mHaveConnected) {
                    return super.read(buffer, byteOffset, byteCount);
                }

                final PipedOutputStream output = new PipedOutputStream();
                connect(output);
                ThreadUtils.postToBackgroundThread(
                    new Runnable() {
                        @Override
                        public void run() {
                            try {
                                bitmap.compress(Bitmap.CompressFormat.PNG, 100, output);
                                output.close();
                            } catch (IOException ioe) { }
                        }
                    });
                mHaveConnected = true;
                return super.read(buffer, byteOffset, byteCount);
            }
        }
    }

    @WrapForJNI(allowMultithread = true, narrowChars = true)
    static URLConnection getConnection(String url) {
        try {
            String spec;
            if (url.startsWith("android://")) {
                spec = url.substring(10);
            } else {
                spec = url.substring(8);
            }

            // Check if we are loading a package icon.
            try {
                if (spec.startsWith("icon/")) {
                    String[] splits = spec.split("/");
                    if (splits.length != 2) {
                        return null;
                    }
                    final String pkg = splits[1];
                    final PackageManager pm = getApplicationContext().getPackageManager();
                    final Drawable d = pm.getApplicationIcon(pkg);
                    final Bitmap bitmap = BitmapUtils.getBitmapFromDrawable(d);
                    return new BitmapConnection(bitmap);
                }
            } catch (Exception ex) {
                Log.e(LOGTAG, "error", ex);
            }

            // if the colon got stripped, put it back
            int colon = spec.indexOf(':');
            if (colon == -1 || colon > spec.indexOf('/')) {
                spec = spec.replaceFirst("/", ":/");
            }
        } catch (Exception ex) {
            return null;
        }
        return null;
    }

    @WrapForJNI(allowMultithread = true, narrowChars = true)
    static String connectionGetMimeType(URLConnection connection) {
        return connection.getContentType();
    }

    /**
     * Retrieve the absolute path of an external storage directory.
     *
     * @param type The type of directory to return
     * @return Absolute path of the specified directory or null on failure
     */
    @WrapForJNI
    static String getExternalPublicDirectory(final String type) {
        final String state = Environment.getExternalStorageState();
        if (!Environment.MEDIA_MOUNTED.equals(state) &&
            !Environment.MEDIA_MOUNTED_READ_ONLY.equals(state)) {
            // External storage is not available.
            return null;
        }

        if ("sdcard".equals(type)) {
            // SD card has a separate path.
            return Environment.getExternalStorageDirectory().getAbsolutePath();
        }

        final String systemType;
        if ("downloads".equals(type)) {
            systemType = Environment.DIRECTORY_DOWNLOADS;
        } else if ("pictures".equals(type)) {
            systemType = Environment.DIRECTORY_PICTURES;
        } else if ("videos".equals(type)) {
            systemType = Environment.DIRECTORY_MOVIES;
        } else if ("music".equals(type)) {
            systemType = Environment.DIRECTORY_MUSIC;
        } else if ("apps".equals(type)) {
            File appInternalStorageDirectory = getApplicationContext().getFilesDir();
            return new File(appInternalStorageDirectory, "mozilla").getAbsolutePath();
        } else {
            return null;
        }
        return Environment.getExternalStoragePublicDirectory(systemType).getAbsolutePath();
    }

    @WrapForJNI
    static int getMaxTouchPoints() {
        PackageManager pm = getApplicationContext().getPackageManager();
        if (pm.hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN_MULTITOUCH_JAZZHAND)) {
            // at least, 5+ fingers.
            return 5;
        } else if (pm.hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN_MULTITOUCH_DISTINCT)) {
            // at least, 2+ fingers.
            return 2;
        } else if (pm.hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN_MULTITOUCH)) {
            // 2 fingers
            return 2;
        } else if (pm.hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN)) {
            // 1 finger
            return 1;
        }
        return 0;
    }

    public static synchronized void resetScreenSize() {
        sScreenSize = null;
    }

    @WrapForJNI
    public static synchronized Rect getScreenSize() {
        if (sScreenSize == null) {
            final WindowManager wm = (WindowManager)
                    getApplicationContext().getSystemService(Context.WINDOW_SERVICE);
            final Display disp = wm.getDefaultDisplay();
            sScreenSize = new Rect(0, 0, disp.getWidth(), disp.getHeight());
        }
        return sScreenSize;
    }
}
